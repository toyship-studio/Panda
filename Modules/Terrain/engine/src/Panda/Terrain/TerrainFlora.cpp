#include "Panda/Terrain/TerrainFlora.hpp"
#include "Panda/Terrain/TerrainResources.hpp"

#include "FloraPlacementMath.hpp"
#include "Panda/Renderer/FloraImposter.hpp"
#include "Panda/Terrain/TerrainGrainTexture.hpp"

#include "Panda/Application/Application.hpp"
#include "Panda/Assets/Base/AssetHandler.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/MeshData.hpp"
#include "Panda/Renderer/SceneWind.hpp"
#include <Foundation/Profile.hpp>
#include "Panda/Renderer/Renderer3D.hpp"
#include "Panda/Renderer/RuntimeMesh.hpp"

#include <Miren/Miren.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Panda {

namespace {

    using namespace FloraPlacementMath;

    constexpr int REBUILD_BUDGET_PER_FRAME = 4;
    constexpr float FREE_MARGIN = 32.f; // blocks farther than maxDistance+margin are released

    // Range of block indices around the camera. The margin of half a block diagonal
    // is MANDATORY and must match the per-block test (which accepts a block by its near
    // face: center − size·0.71), otherwise a block with its center slightly beyond the radius
    // would pass the test but not get into the traversal. For the mesh zone this resulted in
    // missing plants: the billboard vertex shader mirrors the same criterion and treats such
    // blocks as the mesh zone, so NOTHING was drawn in a ring ~0.71 of a block wide.
    struct BlockRange {
        int min = 0;
        int max = -1; // empty by default
    };

    BlockRange blockRange(float cameraCoord, float distance, float blockSize, uint32_t blockCount) {
        const float radius = distance + blockSize * 0.71f;
        BlockRange range;
        range.min = std::max(0, static_cast<int>((cameraCoord - radius) / blockSize));
        range.max = std::min(
            static_cast<int>(blockCount) - 1, static_cast<int>((cameraCoord + radius) / blockSize)
        );
        return range;
    }

    // Tuft shapes: number of blades per variant (the variant is chosen by a hash when scattering).
    constexpr int TUFT_BLADES[TerrainFlora::MAX_VARIANTS] = {9, 7, 5};
    // Tuft density at density = 1 (tufts/m²): a dense carpet, scattering step ~18 cm.
    constexpr float TUFT_MAX_DENSITY = 32.f;

    // Step of the layer's jittered scattering grid; 0 — the layer is empty (zero density).
    float grassCell(const GrassLayer &layer) {
        if (layer.kind == GrassKind::Mesh) {
            // A floor of 0.2 m (25/m²): below it — an explosion of block instances.
            return std::max(layer.spacing, 0.2f);
        }
        const float density = glm::clamp(layer.density, 0.f, 1.f) * TUFT_MAX_DENSITY;
        return density < 0.01f ? 0.f : 1.f / std::sqrt(density);
    }

    uint32_t grassSeed(size_t layerIndex) {
        return static_cast<uint32_t>(layerIndex) * 7919u; // layers are independent: own scattering pattern
    }

    // End of grass mesh dissolve: Grass Dissolve of the world graphics, 0 = auto (Grass Distance).
    float grassMeshFadeEnd(const FloraDistances &distances) {
        return distances.grassDissolve > 0.f ? distances.grassDissolve : distances.grass;
    }

    TerrainFlora::FrameStats s_frameStats; // sum over the terrains per frame (read by the editor)

    // Tuft vertex. h — the normalized flexibility fraction (0 root, 1 top):
    // the factor of the wind bend and of the root→tip color gradient; spreadDir — the direction
    // of spreading out from the plant center (micro-motion); phase — the phase shift of the blade.
    struct FloraVertex {
        glm::vec3 position;
        float h;
        glm::vec2 spreadDir;
        float phase;
    };

    // Tuft instance: posScale = root in terrain local space + scale; params = yaw,
    // wind phase, tint, distance thinning threshold (stable — no flicker).
    struct FloraInstance {
        glm::vec4 posScale;
        glm::vec4 params;
    };

    // Weight of the layer's placement rules at a grid VERTEX — without the relief height (it is
    // known only after the surface is sampled at the plant point).
    float grassRuleWeight(
        const GrassLayer &layer, uint32_t layerSeed, const TerrainData2D &data, uint32_t vx, uint32_t vz,
        float px, float pz
    ) {
        if (layer.placement == GrassPlacement::Painted) {
            // The brush is the artist's direct intent: the weight comes only from the mask channel
            // (soil/height/noise patches do not interfere).
            return static_cast<float>(data.floraMaskAt(vx, vz, layer.maskChannel)) / 255.f;
        }
        return soilWeightAt(layer.soilLayer, data, vx, vz) *
               noisePatchWeightAt(layer.noiseCoverage, px, pz, layerSeed);
    }

    bool grassHeightAllowed(const GrassLayer &layer, float height) {
        return layer.placement == GrassPlacement::Painted ||
               (height >= layer.minHeight && height <= layer.maxHeight);
    }

    // FULL placement weight (except the slope): it is used to bake the far coverage map and to
    // scatter the Combined layers — the distance matches the scattering.
    float grassWeightAt(
        const GrassLayer &layer, uint32_t layerSeed, const TerrainData2D &data, uint32_t vx,
        uint32_t vz, float px, float pz, float height
    ) {
        float weight = grassRuleWeight(layer, layerSeed, data, vx, vz, px, pz);
        if (!grassHeightAllowed(layer, height)) { weight = 0.f; }
        if (layer.placement == GrassPlacement::Combined) {
            // The brush edits the procedural base with priority: an offset from the neutral 128
            // (above — add, below — erase). The height filter enters the base as a zero (not as a
            // rejection): a full stroke plants outside the range as well.
            const float shift =
                (static_cast<float>(data.floraMaskAt(vx, vz, layer.maskChannel)) - 128.f) / 127.f;
            weight = glm::clamp(weight + shift, 0.f, 1.f);
        }
        return weight;
    }

    // SCATTERING DECISION for the cell (ix, iz) of the layer's global jittered grid. true — there
    // is a plant; position/height/scale are in the output.
    struct GrassPlant {
        float px = 0.f;
        float pz = 0.f;
        float height = 0.f;
        float scale = 1.f;
    };
    bool scatterGrass(
        const GrassLayer &layer, const TerrainData2D &data, float cell, uint32_t layerSeed,
        float minNormalY, int ix, int iz, GrassPlant &out
    ) {
        if (ix < 0 || iz < 0) { return false; }
        const uint32_t ux = static_cast<uint32_t>(ix);
        const uint32_t uz = static_cast<uint32_t>(iz);
        out.px = (static_cast<float>(ix) + hash01(ux, uz, 11u + layerSeed)) * cell;
        out.pz = (static_cast<float>(iz) + hash01(ux, uz, 12u + layerSeed)) * cell;
        // Weight at the nearest vertex = probability of a plant.
        const uint32_t vx = glm::min(static_cast<uint32_t>(out.px / data.cellSize + 0.5f), data.cellsX);
        const uint32_t vz = glm::min(static_cast<uint32_t>(out.pz / data.cellSize + 0.5f), data.cellsZ);
        glm::vec3 normal;
        const bool combined = layer.placement == GrassPlacement::Combined;
        float weight;
        if (combined) { // the weight depends on the height — the surface is needed before rejection
            if (!data.sampleSurface(out.px, out.pz, out.height, normal)) { return false; }
            weight = grassWeightAt(layer, layerSeed, data, vx, vz, out.px, out.pz, out.height);
        } else {
            weight = grassRuleWeight(layer, layerSeed, data, vx, vz, out.px, out.pz);
        }
        if (hash01(ux, uz, 13u + layerSeed) >= weight) { return false; }
        // The surface is sampled only for the accepted ones: on sparse layers rejection is cheap.
        if (!combined && !data.sampleSurface(out.px, out.pz, out.height, normal)) { return false; }
        if (!combined && !grassHeightAllowed(layer, out.height)) { return false; }
        if (normal.y < minNormalY) { return false; }
        out.scale = glm::mix(layer.scaleMin, layer.scaleMax, hash01(ux, uz, 21u + layerSeed));
        return true;
    }

    // Matrix of a mat4 instance: position · yaw · scale · pivot (the bottom of the mesh AABB — the
    // plant "stands" on the ground regardless of the exported pivot).
    glm::mat4 plantMatrix(float x, float y, float z, float yaw, float scale, float meshMinY) {
        glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(x, y, z));
        model = glm::rotate(model, yaw, glm::vec3(0.f, 1.f, 0.f));
        model = glm::scale(model, glm::vec3(scale));
        return glm::translate(model, glm::vec3(0.f, -meshMinY, 0.f));
    }

    // A circular arc of length length with the tip tilt angle bendAngle: the position of the point
    // at the fraction t of the stem length. The curvature is constant — the bend is one smooth arc,
    // the tip goes down by itself as the bend grows (the stem length is preserved).
    glm::vec2 arcPoint(float length, float bendAngle, float t) {
        if (std::fabs(bendAngle) < 0.001f) { return {0.f, length * t}; } // straight line
        const float radius = length / bendAngle;
        return {radius * (1.f - cosf(bendAngle * t)), radius * sinf(bendAngle * t)};
    }

    MeshData packMesh(
        Miren::VertexLayoutHandle layoutHandle,
        const std::vector<FloraVertex> &vertices,
        const std::vector<uint32_t> &indices
    ) {
        const uint32_t vertexBufferSize = static_cast<uint32_t>(vertices.size() * sizeof(FloraVertex));
        MeshData mesh(
            layoutHandle,
            Foundation::Memory::copying(vertices.data(), vertexBufferSize),
            vertexBufferSize,
            Foundation::Memory::copying(indices.data(), indices.size() * sizeof(uint32_t)),
            static_cast<uint32_t>(indices.size())
        );
        mesh.vertexStride = sizeof(FloraVertex);
        return mesh;
    }

    // A tuft of unit height: tapering ribbons spread out from the center. The variants have a
    // different number of blades and their own shape seed; the tuft scale is in the instance.
    // Brownian controls the disorder: the blade heights and the strength of the bend as ONE smooth
    // arc (constant curvature, outward from the tuft center). The number of segments grows with the
    // bend — the arc needs points; the vertices get more expensive only for the near tufts (far-LOD
    // is always 1 triangle).
    MeshData buildTuftMeshData(Miren::VertexLayoutHandle layoutHandle, int variant, float brownian) {
        std::vector<FloraVertex> vertices;
        std::vector<uint32_t> indices;
        const int bladeCount = TUFT_BLADES[variant];
        const int segments = brownian >= 1.6f ? 4 : brownian >= 0.8f ? 3 : 2;
        const uint32_t formSeed = static_cast<uint32_t>(variant) * 17u;
        vertices.reserve(bladeCount * (2 * segments + 1));
        indices.reserve(bladeCount * (2 * segments - 1) * 3);

        for (int blade = 0; blade < bladeCount; blade++) {
            const uint32_t b = static_cast<uint32_t>(blade);
            const float angle = static_cast<float>(blade) * 2.399963f + // golden angle
                                hash01(b, formSeed, 29u) * 0.8f;
            const glm::vec2 spread(cosf(angle), sinf(angle));
            const glm::vec2 right(-spread.y, spread.x);
            const float rootRadius = 0.05f + 0.09f * hash01(b, 7u + formSeed, 3u);
            const float bladeHeight = 1.f - 0.28f * brownian * hash01(b, 31u + formSeed, 15u);
            // Arc tip angle: a slight base one + the brownian spread (at 3 — up to ~75°).
            const float bendAngle =
                0.1f + (0.1f + 0.34f * hash01(b, 11u + formSeed, 5u)) * brownian;
            const float baseWidth = 0.05f - 0.015f * hash01(b, 13u + formSeed, 9u);
            const float phase = static_cast<float>(blade) * 0.7f;
            const glm::vec2 root = spread * rootRadius;

            const uint32_t base = static_cast<uint32_t>(vertices.size());
            for (int row = 0; row <= segments; row++) {
                const float t = static_cast<float>(row) / segments; // 0 root .. 1 tip
                const float h = t * bladeHeight;                    // length along the stem
                const glm::vec2 arc = arcPoint(bladeHeight, bendAngle, t);
                const glm::vec2 center = root + spread * arc.x;
                if (row == segments) {
                    vertices.push_back({{center.x, arc.y, center.y}, h, spread, phase});
                    break;
                }
                const glm::vec2 half = right * (baseWidth * (1.f - 0.4f * t) * 0.5f);
                vertices.push_back({{center.x - half.x, arc.y, center.y - half.y}, h, spread, phase});
                vertices.push_back({{center.x + half.x, arc.y, center.y + half.y}, h, spread, phase});
            }
            // A ribbon of quads + the tip; double-sided material — the winding does not matter.
            for (int segment = 0; segment < segments - 1; segment++) {
                const uint32_t r = base + static_cast<uint32_t>(segment) * 2;
                indices.insert(indices.end(), {r, r + 1, r + 2, r + 1, r + 3, r + 2});
            }
            const uint32_t last = base + static_cast<uint32_t>(segments - 1) * 2;
            indices.insert(indices.end(), {last, last + 1, last + 2});
        }
        return packMesh(layoutHandle, vertices, indices);
    }

    bool aabbInsideFrustum(const glm::vec4 *planes, const glm::vec3 &min, const glm::vec3 &max) {
        if (!planes) { return true; }
        for (int i = 0; i < 5; i++) {
            const glm::vec4 &plane = planes[i];
            const glm::vec3 positive(
                plane.x >= 0.f ? max.x : min.x,
                plane.y >= 0.f ? max.y : min.y,
                plane.z >= 0.f ? max.z : min.z
            );
            if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.f) { return false; }
        }
        return true;
    }

} // namespace

bool TerrainFlora::GrassKey::operator==(const GrassKey &other) const {
    return kind == other.kind && placement == other.placement && maskChannel == other.maskChannel &&
           meshKey == other.meshKey && density == other.density && spacing == other.spacing &&
           soilLayer == other.soilLayer && minHeight == other.minHeight &&
           maxHeight == other.maxHeight && maxSlopeDeg == other.maxSlopeDeg &&
           noiseCoverage == other.noiseCoverage && scaleMin == other.scaleMin &&
           scaleMax == other.scaleMax && enabled == other.enabled;
}

TerrainFlora::FrameStats TerrainFlora::consumeFrameStats() {
    const FrameStats stats = s_frameStats;
    s_frameStats = FrameStats{};
    return stats;
}

TerrainFlora::TerrainFlora() = default;

TerrainFlora::~TerrainFlora() {
    releaseAllBlocks();
    if (m_instanceLayout.isValid()) { Miren::deleteVertexLayout(m_instanceLayout); }
    if (m_vertexLayout.isValid()) { Miren::deleteVertexLayout(m_vertexLayout); }
}

// A change of any layer's settings (or of the composition of the lists) = different block contents.
void TerrainFlora::syncKeys(const FloraSettings &settings) {
    std::vector<GrassKey> grass(settings.grass.size());
    for (size_t i = 0; i < settings.grass.size(); i++) {
        const GrassLayer &layer = settings.grass[i];
        // Mesh key = the id of the LOADED asset (0 — still loading): the pivot is baked into the
        // matrices, the completed load changes the key and the blocks are rebuilt by themselves.
        AssetId meshKey = 0;
        if (layer.kind == GrassKind::Mesh && layer.mesh.as<ImportedMeshAsset>().get() != nullptr) {
            meshKey = layer.mesh.getId();
        }
        grass[i] = GrassKey{
            static_cast<uint32_t>(layer.kind), static_cast<uint32_t>(layer.placement),
            layer.maskChannel, meshKey, layer.density, layer.spacing, layer.soilLayer,
            layer.minHeight, layer.maxHeight, layer.maxSlopeDeg, layer.noiseCoverage,
            layer.scaleMin, layer.scaleMax, layer.enabled
        };
    }
    std::vector<TreeKey> trees(settings.trees.size());
    for (size_t i = 0; i < settings.trees.size(); i++) {
        const TreeLayer &layer = settings.trees[i];
        AssetId meshKey = 0;
        if (layer.mesh.as<ImportedMeshAsset>().get() != nullptr) { meshKey = layer.mesh.getId(); }
        trees[i] = TreeKey{layer.id, meshKey, layer.imposter};
    }
    if (grass == m_builtGrass && trees == m_builtTrees) { return; }

    // Stale blocks are drawn until the rebuild (against blinking), but this is valid only
    // while block.grass[i] corresponds to settings.grass[i] (and the same for the trees). A change
    // of the COMPOSITION — a removal/insertion in the middle or a change of the kind at the same
    // index — breaks the correspondence: Tuft instances (2×vec4) would go into the mat4 instancing
    // of a foreign layer — garbage geometry and a read past the end of the buffer. Such blocks are
    // dropped immediately; the smooth replacement remains for parameter edits and for
    // adding/removing the tail of the list.
    bool structural = false;
    const size_t commonGrass = std::min(grass.size(), m_builtGrass.size());
    for (size_t i = 0; i < commonGrass; i++) {
        if (grass[i].kind != m_builtGrass[i].kind) { structural = true; }
    }
    const size_t commonTrees = std::min(trees.size(), m_builtTrees.size());
    for (size_t i = 0; i < commonTrees; i++) {
        if (trees[i].id != m_builtTrees[i].id) { structural = true; }
    }
    if (structural) { releaseAllBlocks(); }
    m_builtGrass = std::move(grass);
    m_builtTrees = std::move(trees);
    m_version++;
    m_occluderCache.clear();
}

FloraFarView TerrainFlora::farView(const FloraSettings &settings) {
    syncKeys(settings);
    FloraFarView view;
    if (!m_grainTexture) { m_grainTexture = std::make_unique<TerrainGrainTexture>(); }
    view.grainTexture = m_grainTexture->ensure(settings.grain);
    // While the first mosaic bake is in flight — no far view (the fade is beyond the horizon).
    if (view.grainTexture.get() == nullptr) { return view; }
    // The terrain crossfade into the far view ends where the grass disappears.
    const float distance = std::max(m_distances.grass, 1.f);
    view.fadeStart = distance * 0.45f; // a long zone: the grass thins out, the mosaic shows through
    view.fadeEnd = distance;
    return view;
}

void TerrainFlora::collectOccluders(
    const FloraSettings &settings,
    const TerrainData2D &data,
    const TerrainTrees &trees,
    const glm::vec3 &localMin,
    const glm::vec3 &localMax,
    const glm::mat4 &transform,
    std::vector<Occluder> &out
) const {
    PANDA_PROFILE_SCOPE_N("TerrainFlora: collectOccluders");
    const float worldScale = glm::length(glm::vec3(transform[0]));
    const float extentX = static_cast<float>(data.cellsX) * data.cellSize;
    const float extentZ = static_cast<float>(data.cellsZ) * data.cellSize;
    const float x0 = std::max(localMin.x, 0.f);
    const float z0 = std::max(localMin.z, 0.f);
    const float x1 = std::min(localMax.x, extentX);
    const float z1 = std::min(localMax.z, extentZ);
    if (x1 <= x0 || z1 <= z0) { return; }
    // Hash of the layers' crown settings: a change of density/mesh/enabled state — a new cache.
    uint64_t layersHash = 1469598103934665603ull;
    for (const TreeLayer &layer : settings.trees) {
        uint32_t densityBits = 0;
        std::memcpy(&densityBits, &layer.canopyDensity, sizeof(densityBits));
        for (const uint64_t part :
             {static_cast<uint64_t>(densityBits), static_cast<uint64_t>(layer.enabled ? 1u : 0u),
              static_cast<uint64_t>(layer.id), static_cast<uint64_t>(layer.mesh.getId())}) {
            layersHash = (layersHash ^ part) * 1099511628211ull;
        }
    }
    const int superX0 = std::max(0, static_cast<int>(std::floor(x0 / SUPER_BLOCK_SIZE)));
    const int superZ0 = std::max(0, static_cast<int>(std::floor(z0 / SUPER_BLOCK_SIZE)));
    const int superX1 = static_cast<int>(std::floor((x1 - 0.001f) / SUPER_BLOCK_SIZE));
    const int superZ1 = static_cast<int>(std::floor((z1 - 0.001f) / SUPER_BLOCK_SIZE));
    for (int superZ = superZ0; superZ <= superZ1; superZ++) {
        for (int superX = superX0; superX <= superX1; superX++) {
            const uint64_t key = (static_cast<uint64_t>(superZ) << 32) | static_cast<uint32_t>(superX);
            OccluderCacheEntry &entry = m_occluderCache[key];
            if (entry.version != m_version || entry.layersHash != layersHash) {
                entry.instances.clear();
                collectOccludersSuperBlock(settings, data, trees, superX, superZ, entry.instances);
                entry.version = m_version;
                entry.layersHash = layersHash;
            }
            for (const Occluder &local : entry.instances) {
                if (local.center.x < x0 || local.center.x >= x1 || local.center.z < z0 || local.center.z >= z1) {
                    continue;
                }
                Occluder instance;
                instance.center = glm::vec3(transform * glm::vec4(local.center, 1.f));
                instance.radiusXZ = local.radiusXZ * worldScale;
                instance.radiusY = local.radiusY * worldScale;
                instance.density = local.density;
                out.push_back(instance);
            }
        }
    }
}

// Tree crowns over the area of a superblock (terrain local space): the crown is the upper 2/3 of
// the mesh height (center 0.675 H, semi-axis 0.325 H).
void TerrainFlora::collectOccludersSuperBlock(
    const FloraSettings &settings,
    const TerrainData2D &data,
    const TerrainTrees &trees,
    int superX,
    int superZ,
    std::vector<Occluder> &out
) const {
    PANDA_PROFILE_SCOPE_N("TerrainFlora: collectOccludersSuperBlock");
    const float x0 = static_cast<float>(superX) * SUPER_BLOCK_SIZE;
    const float z0 = static_cast<float>(superZ) * SUPER_BLOCK_SIZE;
    trees.forEachIn(x0, z0, x0 + SUPER_BLOCK_SIZE, z0 + SUPER_BLOCK_SIZE, [&](const TreeInstance &tree) {
        for (size_t i = 0; i < settings.trees.size() && i < m_treeBounds.size(); i++) {
            const TreeLayer &layer = settings.trees[i];
            if (layer.id != tree.layer) { continue; }
            if (!layer.enabled || layer.canopyDensity <= 0.f || m_treeBounds[i].meshId == 0) { return; }
            float surfaceHeight;
            glm::vec3 normal;
            if (!data.sampleSurface(tree.x, tree.z, surfaceHeight, normal)) { return; }
            const float height = m_treeBounds[i].height * tree.scale;
            Occluder instance;
            instance.center = glm::vec3(tree.x, surfaceHeight + 0.675f * height, tree.z);
            instance.radiusXZ = m_treeBounds[i].halfWidth * tree.scale;
            instance.radiusY = 0.325f * height;
            instance.density = glm::clamp(layer.canopyDensity, 0.f, 1.f);
            out.push_back(instance);
            return;
        }
    });
}

float TerrainFlora::imposterRange() const {
    const float diagonal = glm::length(glm::vec2(m_blocksX * BLOCK_SIZE, m_blocksZ * BLOCK_SIZE));
    return glm::min(diagonal, glm::max(m_distances.imposter, 0.f));
}

void TerrainFlora::invalidate() {
    m_version++;
    m_occluderCache.clear();
}

void TerrainFlora::invalidateRegion(
    uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ, float cellSize
) {
    if (m_blocks.empty()) { return; }
    // A 1 m margin: the scattering reads neighboring vertices (nearest texel + bilinear sample).
    const float minMeterX = static_cast<float>(minX) * cellSize - 1.f;
    const float minMeterZ = static_cast<float>(minZ) * cellSize - 1.f;
    const float maxMeterX = static_cast<float>(maxX) * cellSize + 1.f;
    const float maxMeterZ = static_cast<float>(maxZ) * cellSize + 1.f;
    const int blockX0 = std::max(0, static_cast<int>(minMeterX / BLOCK_SIZE));
    const int blockZ0 = std::max(0, static_cast<int>(minMeterZ / BLOCK_SIZE));
    const int blockX1 = std::min(static_cast<int>(m_blocksX) - 1, static_cast<int>(maxMeterX / BLOCK_SIZE));
    const int blockZ1 = std::min(static_cast<int>(m_blocksZ) - 1, static_cast<int>(maxMeterZ / BLOCK_SIZE));
    for (int bz = blockZ0; bz <= blockZ1; bz++) {
        for (int bx = blockX0; bx <= blockX1; bx++) {
            m_blocks[static_cast<size_t>(bz) * m_blocksX + bx].dirty = true;
        }
    }
    // The superblocks of the imposter zone follow the same edits: without a reset new trees
    // would appear only in the mesh zone.
    if (!m_superBlocks.empty()) {
        const int superX0 = std::max(0, static_cast<int>(minMeterX / SUPER_BLOCK_SIZE));
        const int superZ0 = std::max(0, static_cast<int>(minMeterZ / SUPER_BLOCK_SIZE));
        const int superX1 =
            std::min(static_cast<int>(m_superBlocksX) - 1, static_cast<int>(maxMeterX / SUPER_BLOCK_SIZE));
        const int superZ1 =
            std::min(static_cast<int>(m_superBlocksZ) - 1, static_cast<int>(maxMeterZ / SUPER_BLOCK_SIZE));
        for (int bz = superZ0; bz <= superZ1; bz++) {
            for (int bx = superX0; bx <= superX1; bx++) {
                m_superBlocks[static_cast<size_t>(bz) * m_superBlocksX + bx].builtVersion = 0;
            }
        }
    }
}

void TerrainFlora::ensureLayout() {
    if (m_vertexLayout.isValid()) { return; }
    Miren::VertexBufferLayoutData vertexLayoutData;
    vertexLayoutData.pushVec3();   // position
    vertexLayoutData.pushFloat(1); // h
    vertexLayoutData.pushVec2();   // spreadDir
    vertexLayoutData.pushFloat(1); // phase
    m_vertexLayout = Miren::createVertexLayout(vertexLayoutData);

    Miren::VertexBufferLayoutData instanceLayoutData;
    instanceLayoutData.pushVec4(); // posScale
    instanceLayoutData.pushVec4(); // params
    m_instanceLayout = Miren::createVertexLayout(instanceLayoutData);
}

void TerrainFlora::ensureLayerResources(const FloraSettings &settings) {
    // Bounds of the imported mesh: the pivot (bottom of the AABB) and the sizes are baked into the
    // instance matrices and the crowns; a change/completed load of the asset changes the layer
    // key → block rebuild.
    const auto updateBounds = [](MeshBounds &bounds, const AssetRef<Asset> &meshRef) {
        AssetRef<ImportedMeshAsset> mesh = meshRef.as<ImportedMeshAsset>();
        const AssetId meshId = mesh.isValid() ? meshRef.getId() : AssetId(0);
        if (bounds.meshId == meshId) { return; }
        bounds = MeshBounds{};
        bounds.meshId = meshId;
        glm::vec3 boundsMin, boundsMax;
        if (meshId != 0 && mesh->getMeshBounds(boundsMin, boundsMax)) {
            bounds.minY = boundsMin.y;
            bounds.height = std::max(boundsMax.y - boundsMin.y, 0.1f);
            bounds.halfWidth =
                std::max(std::max(boundsMax.x - boundsMin.x, boundsMax.z - boundsMin.z) * 0.5f, 0.1f);
        }
    };
    m_grassRenders.resize(settings.grass.size());
    for (size_t i = 0; i < settings.grass.size(); i++) {
        const GrassLayer &layer = settings.grass[i];
        GrassRender &render = m_grassRenders[i];
        if (layer.kind == GrassKind::Mesh) {
            updateBounds(render.bounds, layer.mesh);
            continue;
        }
        // A brownian change is only a regeneration of the tiny tuft meshes (the block instances
        // are not touched: the scattering does not depend on the plant shape).
        const float brownian = glm::clamp(layer.brownian, 0.f, 3.f);
        if (brownian == render.builtBrownian) { continue; }
        render.builtBrownian = brownian;
        for (int variant = 0; variant < MAX_VARIANTS; variant++) {
            MeshData mesh = buildTuftMeshData(m_vertexLayout, variant, brownian);
            if (!render.tufts[variant]) { render.tufts[variant] = std::make_unique<RuntimeMesh>(); }
            render.tufts[variant]->update(mesh);
        }
    }
    m_treeBounds.resize(settings.trees.size());
    for (size_t i = 0; i < settings.trees.size(); i++) {
        updateBounds(m_treeBounds[i], settings.trees[i].mesh);
    }
}

void TerrainFlora::ensureGrid(const TerrainData2D &data) {
    const uint32_t blocksX =
        static_cast<uint32_t>(std::ceil(static_cast<float>(data.cellsX) * data.cellSize / BLOCK_SIZE));
    const uint32_t blocksZ =
        static_cast<uint32_t>(std::ceil(static_cast<float>(data.cellsZ) * data.cellSize / BLOCK_SIZE));
    if (blocksX == m_blocksX && blocksZ == m_blocksZ) { return; }
    releaseAllBlocks();
    m_blocksX = blocksX;
    m_blocksZ = blocksZ;
    m_blocks.clear();
    m_blocks.resize(static_cast<size_t>(blocksX) * blocksZ);
    m_superBlocksX = static_cast<uint32_t>(
        std::ceil(static_cast<float>(data.cellsX) * data.cellSize / SUPER_BLOCK_SIZE)
    );
    m_superBlocksZ = static_cast<uint32_t>(
        std::ceil(static_cast<float>(data.cellsZ) * data.cellSize / SUPER_BLOCK_SIZE)
    );
    m_superBlocks.clear();
    m_superBlocks.resize(static_cast<size_t>(m_superBlocksX) * m_superBlocksZ);
}

void TerrainFlora::releaseInstances(MeshInstances &instances) {
    if (instances.buffer.isValid()) { Miren::deleteVertexBuffer(instances.buffer); }
    instances = MeshInstances{};
}

void TerrainFlora::releaseBlock(Block &block) {
    for (GrassBlock &grass : block.grass) {
        for (int variant = 0; variant < MAX_VARIANTS; variant++) {
            if (grass.tufts[variant].isValid()) { Miren::deleteVertexBuffer(grass.tufts[variant]); }
        }
        releaseInstances(grass.mesh);
    }
    for (MeshInstances &trees : block.trees) { releaseInstances(trees); }
    block.grass.clear();
    block.trees.clear();
    block.builtVersion = 0;
    block.dirty = false; // there is no content — builtVersion itself will request the rebuild
}

void TerrainFlora::releaseSuperBlock(SuperBlock &block) {
    for (MeshInstances &trees : block.trees) { releaseInstances(trees); }
    block.trees.clear();
    block.builtVersion = 0;
}

void TerrainFlora::releaseAllBlocks() {
    for (Block &block : m_blocks) { releaseBlock(block); }
    for (SuperBlock &block : m_superBlocks) { releaseSuperBlock(block); }
}

namespace {

    // mat4 instances of the trees of the area [x0, x1) × [z0, z1) by layers (index = layer of
    // settings.trees). A shared gatherer for a block of the mesh zone and a superblock of the
    // imposter far zone: the billboard stands exactly in the place of the mesh.
    // accept(layerIndex) — which layers to gather.
    template<typename Accept>
    void gatherTreeMatrices(
        const FloraSettings &settings, const TerrainData2D &data, const TerrainTrees &trees,
        const std::vector<float> &meshMinY, float x0, float z0, float x1, float z1, Accept &&accept,
        std::vector<std::vector<glm::mat4>> &outMatrices, float &minY, float &maxY, bool &any
    ) {
        outMatrices.assign(settings.trees.size(), {});
        trees.forEachIn(x0, z0, x1, z1, [&](const TreeInstance &tree) {
            for (size_t i = 0; i < settings.trees.size(); i++) {
                if (settings.trees[i].id != tree.layer) { continue; }
                if (!accept(i)) { return; }
                float height;
                glm::vec3 normal;
                if (!data.sampleSurface(tree.x, tree.z, height, normal)) { return; }
                outMatrices[i].push_back(plantMatrix(tree.x, height, tree.z, tree.yaw, tree.scale, meshMinY[i]));
                minY = any ? std::min(minY, height) : height;
                maxY = any ? std::max(maxY, height) : height;
                any = true;
                return;
            }
        });
    }

    Miren::VertexBufferHandle createMatrixBuffer(const std::vector<glm::mat4> &matrices) {
        const uint32_t size = static_cast<uint32_t>(matrices.size() * sizeof(glm::mat4));
        return Miren::createVertexBuffer(
            Foundation::Memory::copying(matrices.data(), size), size, StaticResources::instanceMat4Layout
        );
    }

} // namespace

// Billboards of the imposter layers over the area of a superblock: the same points as in the
// small blocks.
void TerrainFlora::rebuildSuperBlock(
    SuperBlock &block,
    uint32_t superX,
    uint32_t superZ,
    const FloraSettings &settings,
    const TerrainData2D &data,
    const TerrainTrees &trees
) {
    releaseSuperBlock(block);
    block.builtVersion = m_version;
    block.trees.resize(settings.trees.size());

    const float x0 = static_cast<float>(superX) * SUPER_BLOCK_SIZE;
    const float z0 = static_cast<float>(superZ) * SUPER_BLOCK_SIZE;
    std::vector<float> meshMinY(settings.trees.size(), 0.f);
    for (size_t i = 0; i < m_treeBounds.size() && i < meshMinY.size(); i++) { meshMinY[i] = m_treeBounds[i].minY; }
    std::vector<std::vector<glm::mat4>> matrices;
    float minY = 0.f;
    float maxY = 0.f;
    bool any = false;
    gatherTreeMatrices(
        settings, data, trees, meshMinY, x0, z0, x0 + SUPER_BLOCK_SIZE, z0 + SUPER_BLOCK_SIZE,
        [&](size_t i) { return settings.trees[i].enabled && settings.trees[i].imposter; }, matrices,
        minY, maxY, any
    );
    for (size_t i = 0; i < matrices.size(); i++) {
        if (matrices[i].empty()) { continue; }
        block.trees[i].count = static_cast<uint32_t>(matrices[i].size());
        block.trees[i].buffer = createMatrixBuffer(matrices[i]);
    }
    block.minY = minY;
    block.maxY = maxY;
}

void TerrainFlora::rebuildBlock(
    Block &block,
    uint32_t blockX,
    uint32_t blockZ,
    const FloraSettings &settings,
    const TerrainData2D &data,
    const TerrainTrees &trees
) {
    PANDA_PROFILE_SCOPE_N("TerrainFlora: rebuildBlock");
    releaseBlock(block);
    block.builtVersion = m_version;
    block.dirty = false;
    block.grass.resize(settings.grass.size());
    block.trees.resize(settings.trees.size());

    const float x0 = static_cast<float>(blockX) * BLOCK_SIZE;
    const float z0 = static_cast<float>(blockZ) * BLOCK_SIZE;
    const float x1 = x0 + BLOCK_SIZE;
    const float z1 = z0 + BLOCK_SIZE;

    float minY = 0.f;
    float maxY = 0.f;
    bool any = false;

    // Trees: the points of the TerrainTrees cell that coincides with the block.
    {
        std::vector<float> meshMinY(settings.trees.size(), 0.f);
        for (size_t i = 0; i < m_treeBounds.size() && i < meshMinY.size(); i++) { meshMinY[i] = m_treeBounds[i].minY; }
        std::vector<std::vector<glm::mat4>> matrices;
        gatherTreeMatrices(
            settings, data, trees, meshMinY, x0, z0, x1, z1,
            [&](size_t i) { return settings.trees[i].enabled && m_treeBounds[i].meshId != 0; },
            matrices, minY, maxY, any
        );
        for (size_t i = 0; i < matrices.size(); i++) {
            if (matrices[i].empty()) { continue; }
            block.trees[i].count = static_cast<uint32_t>(matrices[i].size());
            block.trees[i].buffer = createMatrixBuffer(matrices[i]);
        }
    }

    // Grass: scattering of the layer over the global jittered grid (the block joints are
    // seamless): the cells whose range intersects the block; a plant belongs to the block its
    // position falls into.
    for (size_t layerIndex = 0; layerIndex < settings.grass.size(); layerIndex++) {
        const GrassLayer &layer = settings.grass[layerIndex];
        if (!layer.enabled) { continue; }
        const float cell = grassCell(layer);
        if (cell <= 0.f) { continue; }
        const bool meshKind = layer.kind == GrassKind::Mesh;
        const MeshBounds &bounds = m_grassRenders[layerIndex].bounds;
        if (meshKind && bounds.meshId == 0) { continue; } // the mesh is not assigned/is loading
        GrassBlock &grassBlock = block.grass[layerIndex];
        const uint32_t layerSeed = grassSeed(layerIndex);
        const float minNormalY = slopeMinNormalY(layer.maxSlopeDeg);

        const int ix0 = static_cast<int>(std::floor(x0 / cell)) - 1;
        const int iz0 = static_cast<int>(std::floor(z0 / cell)) - 1;
        const int ix1 = static_cast<int>(std::floor(x1 / cell)) + 1;
        const int iz1 = static_cast<int>(std::floor(z1 / cell)) + 1;

        std::vector<FloraInstance> tufts[MAX_VARIANTS];
        std::vector<glm::mat4> matrices;
        for (int iz = iz0; iz <= iz1; iz++) {
            for (int ix = ix0; ix <= ix1; ix++) {
                GrassPlant plant;
                if (!scatterGrass(layer, data, cell, layerSeed, minNormalY, ix, iz, plant)) { continue; }
                if (plant.px < x0 || plant.px >= x1 || plant.pz < z0 || plant.pz >= z1) { continue; }
                const uint32_t ux = static_cast<uint32_t>(ix);
                const uint32_t uz = static_cast<uint32_t>(iz);
                minY = any ? std::min(minY, plant.height) : plant.height;
                maxY = any ? std::max(maxY, plant.height) : plant.height;
                any = true;
                const float yaw = hash01(ux, uz, 31u + layerSeed) * glm::two_pi<float>();
                if (meshKind) {
                    matrices.push_back(plantMatrix(plant.px, plant.height, plant.pz, yaw, plant.scale, bounds.minY));
                    continue;
                }
                FloraInstance instance;
                instance.posScale = {plant.px, plant.height, plant.pz, plant.scale};
                instance.params = {
                    yaw, hash01(ux, uz, 37u + layerSeed) * glm::two_pi<float>(),
                    hash01(ux, uz, 41u + layerSeed), hash01(ux, uz, 43u + layerSeed)
                };
                const int variant = std::min(
                    static_cast<int>(hash01(ux, uz, 53u + layerSeed) * MAX_VARIANTS), MAX_VARIANTS - 1
                );
                tufts[variant].push_back(instance);
            }
        }

        if (meshKind) {
            // Meshes have no prefix thinning: the block is drawn as a whole up to the grass
            // distance; veg_grass tufts dissolve per tuft IN THE SHADER (grassFade).
            if (!matrices.empty()) {
                grassBlock.mesh.count = static_cast<uint32_t>(matrices.size());
                grassBlock.mesh.buffer = createMatrixBuffer(matrices);
            }
            continue;
        }
        for (int variant = 0; variant < MAX_VARIANTS; variant++) {
            std::vector<FloraInstance> &list = tufts[variant];
            // Sorting by the fade threshold in descending order: the survivors at any distance are
            // a prefix of the buffer. prefixCounts[k] = how many of the first instances have a
            // threshold >= k/FADE_BUCKETS.
            std::sort(list.begin(), list.end(), [](const FloraInstance &a, const FloraInstance &b) {
                return a.params.w > b.params.w;
            });
            size_t cursor = 0;
            for (int bucket = FADE_BUCKETS; bucket >= 0; bucket--) {
                const float threshold = static_cast<float>(bucket) / FADE_BUCKETS;
                while (cursor < list.size() && list[cursor].params.w >= threshold) { cursor++; }
                grassBlock.prefixCounts[variant][bucket] =
                    static_cast<uint16_t>(std::min<size_t>(cursor, UINT16_MAX));
            }
            if (list.empty()) { continue; }
            const uint32_t size = static_cast<uint32_t>(list.size() * sizeof(FloraInstance));
            grassBlock.tufts[variant] = Miren::createVertexBuffer(
                Foundation::Memory::copying(list.data(), size), size, m_instanceLayout
            );
        }
    }
    block.minY = minY;
    block.maxY = maxY;
}

template<typename Pick>
void TerrainFlora::submitMeshBlocks(
    Renderer3D &renderer,
    AssetRef<ImportedMeshAsset> mesh,
    AssetRef<MaterialAsset> material,
    float drawDistance,
    float plantHeight,
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    glm::mat4 &transform,
    bool countStats,
    Pick &&pick
) {
    const BlockRange rangeX = blockRange(localCameraPosition.x, drawDistance, BLOCK_SIZE, m_blocksX);
    const BlockRange rangeZ = blockRange(localCameraPosition.z, drawDistance, BLOCK_SIZE, m_blocksZ);
    for (int bz = rangeZ.min; bz <= rangeZ.max; bz++) {
        for (int bx = rangeX.min; bx <= rangeX.max; bx++) {
            Block &block = m_blocks[static_cast<size_t>(bz) * m_blocksX + bx];
            if (block.builtVersion == 0) { continue; }
            const MeshInstances *instances = pick(block);
            if (instances == nullptr || instances->count == 0 || !instances->buffer.isValid()) { continue; }
            const glm::vec2 center(
                (static_cast<float>(bx) + 0.5f) * BLOCK_SIZE, (static_cast<float>(bz) + 0.5f) * BLOCK_SIZE
            );
            const float toCamera = glm::distance(center, glm::vec2(localCameraPosition.x, localCameraPosition.z));
            if (toCamera - BLOCK_SIZE * 0.71f > drawDistance) { continue; }

            const glm::vec3 aabbMin(
                static_cast<float>(bx) * BLOCK_SIZE, block.minY, static_cast<float>(bz) * BLOCK_SIZE
            );
            const glm::vec3 aabbMax(aabbMin.x + BLOCK_SIZE, block.maxY + plantHeight + 0.5f, aabbMin.z + BLOCK_SIZE);
            if (!aabbInsideFrustum(frustumPlanes, aabbMin, aabbMax)) { continue; }

            renderer.submitInstanced(
                transform, mesh, material, instances->buffer, StaticResources::instanceMat4Layout,
                instances->count
            );
            if (!countStats) { continue; }
            s_frameStats.instances += instances->count;
            if (block.visibleStamp != m_frameStamp) {
                block.visibleStamp = m_frameStamp;
                s_frameStats.visibleBlocks++;
            }
        }
    }
}

// Imposter zone of a tree layer: beyond the tree distance the plant continues as a billboard with
// a baked atlas (color+normals). While the atlas is being baked (the first frames) — the zone is
// empty; the disappearance at the end is a dissolve in the billboard shader. In the depth pass —
// a quad facing the light (the shadowVariant of the shader): without it the forest in the distance
// would stand on brightly lit ground.
void TerrainFlora::submitBillboards(
    Renderer3D &renderer,
    const TreeLayer &layer,
    size_t treeIndex,
    bool shadowPass,
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    glm::mat4 &transform
) {
    AssetRef<ImportedMeshAsset> mesh = layer.mesh.as<ImportedMeshAsset>();
    if (!mesh.isValid()) { return; }
    if (!m_imposter) {
        if (shadowPass) { return; }
        m_imposter = std::make_unique<FloraImposter>();
    }
    const FloraImposter::View imposterView = m_imposter->ensure(mesh, layer.meshMaterial.as<MaterialAsset>());
    if (!imposterView.ready) { return; }
    const float imposterDistance = imposterRange();
    // Runtime slots of the billboard material — per layer, before the series of superblock submits.
    // cameraLocal.w — the mesh zone threshold: closer than it the instances are collapsed by the
    // vertex shader (the SCENE camera in the depth pass too — the threshold mirrors the main
    // billboard).
    AssetRef<MaterialAsset> billboard = StaticResources::floraBillboardMaterial;
    billboard->setRuntimeField("imposterColorTexture", imposterView.colorAtlas.asBaseAsset());
    billboard->setRuntimeField(
        "sizeParams",
        Vec4(imposterView.width, imposterView.height, imposterView.minY, static_cast<float>(FloraImposter::TILES))
    );
    billboard->setRuntimeField(
        "cameraLocal",
        Vec4(localCameraPosition.x, localCameraPosition.y, localCameraPosition.z, m_distances.tree)
    );
    if (!shadowPass) {
        billboard->setRuntimeField("imposterNormalTexture", imposterView.normalAtlas.asBaseAsset());
        // The last 20% of the radius — a dissolve in the billboard shader.
        billboard->setRuntimeField("fadeParams", Vec4(imposterDistance * 0.8f, imposterDistance, 0.f, 0.f));
        billboard->setRuntimeField(
            "surfaceParams",
            Vec4(imposterView.crownMetallic, imposterView.crownRoughness, imposterView.crownTranslucency, 0.f)
        );
    }
    const RuntimeMesh &billboardQuad = StaticResources::floraBillboardMesh->getMesh();
    const float billboardHeight = imposterView.height * layer.scaleMax;
    // Depth pass: the coverage is the cascade frustum, itself no wider than Shadow Distance.
    const BlockRange rangeX =
        shadowPass ? BlockRange{0, static_cast<int>(m_superBlocksX) - 1}
                   : blockRange(localCameraPosition.x, imposterDistance, SUPER_BLOCK_SIZE, m_superBlocksX);
    const BlockRange rangeZ =
        shadowPass ? BlockRange{0, static_cast<int>(m_superBlocksZ) - 1}
                   : blockRange(localCameraPosition.z, imposterDistance, SUPER_BLOCK_SIZE, m_superBlocksZ);
    for (int bz = rangeZ.min; bz <= rangeZ.max; bz++) {
        for (int bx = rangeX.min; bx <= rangeX.max; bx++) {
            SuperBlock &block = m_superBlocks[static_cast<size_t>(bz) * m_superBlocksX + bx];
            if (block.builtVersion == 0 || block.trees.size() <= treeIndex) { continue; }
            const MeshInstances &instances = block.trees[treeIndex];
            if (instances.count == 0 || !instances.buffer.isValid()) { continue; }
            if (!shadowPass) {
                const glm::vec2 center(
                    (static_cast<float>(bx) + 0.5f) * SUPER_BLOCK_SIZE,
                    (static_cast<float>(bz) + 0.5f) * SUPER_BLOCK_SIZE
                );
                const float toCamera =
                    glm::distance(center, glm::vec2(localCameraPosition.x, localCameraPosition.z));
                if (toCamera - SUPER_BLOCK_SIZE * 0.71f > imposterDistance) { continue; }
            }
            const glm::vec3 aabbMin(
                static_cast<float>(bx) * SUPER_BLOCK_SIZE, block.minY, static_cast<float>(bz) * SUPER_BLOCK_SIZE
            );
            const glm::vec3 aabbMax(
                aabbMin.x + SUPER_BLOCK_SIZE, block.maxY + billboardHeight + 0.5f, aabbMin.z + SUPER_BLOCK_SIZE
            );
            if (!aabbInsideFrustum(frustumPlanes, aabbMin, aabbMax)) { continue; }
            renderer.submitInstanced(
                transform, billboardQuad, billboard, instances.buffer, StaticResources::instanceMat4Layout,
                instances.count
            );
            if (!shadowPass) { s_frameStats.instances += instances.count; }
        }
    }
}

void TerrainFlora::renderShadowCasters(
    Renderer3D &renderer,
    const FloraSettings &settings,
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    glm::mat4 &transform
) {
    if (renderer.getMode() != RenderingMode::SHADOW_DEPTH) { return; }
    PANDA_PROFILE_SCOPE_N("TerrainFlora::renderShadowCasters");
    for (size_t i = 0; i < settings.trees.size() && i < m_treeBounds.size(); i++) {
        const TreeLayer &layer = settings.trees[i];
        AssetRef<ImportedMeshAsset> mesh = layer.mesh.as<ImportedMeshAsset>();
        if (!layer.enabled || !mesh.isValid()) { continue; }
        submitMeshBlocks(
            renderer, mesh, layer.meshMaterial.as<MaterialAsset>(), m_distances.tree,
            m_treeBounds[i].height * layer.scaleMax, localCameraPosition, frustumPlanes, transform, false,
            [i](const Block &block) { return i < block.trees.size() ? &block.trees[i] : nullptr; }
        );
        if (layer.imposter) {
            submitBillboards(renderer, layer, i, true, localCameraPosition, frustumPlanes, transform);
        }
    }
    for (size_t i = 0; i < settings.grass.size() && i < m_grassRenders.size(); i++) {
        const GrassLayer &layer = settings.grass[i];
        if (!layer.enabled || layer.kind != GrassKind::Mesh || !layer.castShadows) { continue; }
        AssetRef<ImportedMeshAsset> mesh = layer.mesh.as<ImportedMeshAsset>();
        if (!mesh.isValid()) { continue; }
        submitMeshBlocks(
            renderer, mesh, layer.meshMaterial.as<MaterialAsset>(), grassMeshFadeEnd(m_distances),
            m_grassRenders[i].bounds.height * layer.scaleMax, localCameraPosition, frustumPlanes, transform,
            false, [i](const Block &block) { return i < block.grass.size() ? &block.grass[i].mesh : nullptr; }
        );
    }
}

void TerrainFlora::render(
    Renderer3D &renderer,
    const FloraSettings &settings,
    const TerrainData2D &data,
    const TerrainTrees &trees,
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    glm::mat4 &transform
) {
    if (renderer.getMode() != RenderingMode::DEFAULT) { return; }
    PANDA_PROFILE_SCOPE_N("TerrainFlora::render");
    // Regular blocks live only up to the grass and tree distances; the imposter far zone is served
    // by large superblocks — the fine grid does not bloat as the imposter radius grows.
    float maxDistance = 0.f;
    bool anyImposter = false;
    for (const GrassLayer &layer : settings.grass) {
        if (!layer.enabled) { continue; }
        maxDistance = std::max(maxDistance, m_distances.grass);
        // Grass meshes may dissolve beyond Grass Distance — the blocks are needed up to the end.
        if (layer.kind == GrassKind::Mesh) { maxDistance = std::max(maxDistance, grassMeshFadeEnd(m_distances)); }
    }
    for (const TreeLayer &layer : settings.trees) {
        if (!layer.enabled) { continue; }
        maxDistance = std::max(maxDistance, m_distances.tree);
        anyImposter = anyImposter || layer.imposter;
    }
    if (maxDistance <= 0.f) { // no enabled layers
        releaseAllBlocks();
        return;
    }
    if (!data.isValid()) { return; }
    ensureLayout();
    ensureLayerResources(settings);
    ensureGrid(data);
    syncKeys(settings);
    // Billboards are visible from any distance: the coverage is the whole terrain (up to the
    // ceiling of the imposter zone).
    const float imposterMaxDistance = anyImposter ? imposterRange() : 0.f;

    m_frameStamp++;
    if (m_imposter) { m_imposter->processBaking(); } // one atlas view angle per frame

    // Pass 1: incremental build-up of the missing blocks within the maximum radius of the layers
    // (per-frame budget).
    const BlockRange visX = blockRange(localCameraPosition.x, maxDistance, BLOCK_SIZE, m_blocksX);
    const BlockRange visZ = blockRange(localCameraPosition.z, maxDistance, BLOCK_SIZE, m_blocksZ);
    int rebuildBudget = REBUILD_BUDGET_PER_FRAME;
    for (int bz = visZ.min; bz <= visZ.max && rebuildBudget > 0; bz++) {
        for (int bx = visX.min; bx <= visX.max && rebuildBudget > 0; bx++) {
            Block &block = m_blocks[static_cast<size_t>(bz) * m_blocksX + bx];
            if (block.builtVersion == m_version && !block.dirty) { continue; }
            const glm::vec2 center(
                (static_cast<float>(bx) + 0.5f) * BLOCK_SIZE, (static_cast<float>(bz) + 0.5f) * BLOCK_SIZE
            );
            const float toCamera =
                glm::distance(center, glm::vec2(localCameraPosition.x, localCameraPosition.z));
            if (toCamera - BLOCK_SIZE * 0.71f > maxDistance) { continue; }
            rebuildBudget--;
            s_frameStats.rebuilds++;
            rebuildBlock(block, static_cast<uint32_t>(bx), static_cast<uint32_t>(bz), settings, data, trees);
        }
    }

    // Imposter zone superblocks: large and sparse — one rebuild per frame covers the far zone.
    if (imposterMaxDistance > 0.f) {
        const BlockRange supX =
            blockRange(localCameraPosition.x, imposterMaxDistance, SUPER_BLOCK_SIZE, m_superBlocksX);
        const BlockRange supZ =
            blockRange(localCameraPosition.z, imposterMaxDistance, SUPER_BLOCK_SIZE, m_superBlocksZ);
        bool superRebuilt = false;
        for (int bz = supZ.min; bz <= supZ.max && !superRebuilt; bz++) {
            for (int bx = supX.min; bx <= supX.max && !superRebuilt; bx++) {
                SuperBlock &block = m_superBlocks[static_cast<size_t>(bz) * m_superBlocksX + bx];
                if (block.builtVersion == m_version) { continue; }
                const glm::vec2 center(
                    (static_cast<float>(bx) + 0.5f) * SUPER_BLOCK_SIZE,
                    (static_cast<float>(bz) + 0.5f) * SUPER_BLOCK_SIZE
                );
                const float toCamera =
                    glm::distance(center, glm::vec2(localCameraPosition.x, localCameraPosition.z));
                if (toCamera - SUPER_BLOCK_SIZE * 0.71f > imposterMaxDistance) { continue; }
                superRebuilt = true;
                s_frameStats.rebuilds++;
                rebuildSuperBlock(
                    block, static_cast<uint32_t>(bx), static_cast<uint32_t>(bz), settings, data, trees
                );
            }
        }
    }

    // Pass 2a: trees — meshes up to the tree distance, beyond it the billboards of the layer.
    for (size_t i = 0; i < settings.trees.size(); i++) {
        const TreeLayer &layer = settings.trees[i];
        AssetRef<ImportedMeshAsset> mesh = layer.mesh.as<ImportedMeshAsset>();
        if (!layer.enabled || !mesh.isValid()) { continue; } // not assigned/loading
        submitMeshBlocks(
            renderer, mesh, layer.meshMaterial.as<MaterialAsset>(), m_distances.tree,
            m_treeBounds[i].height * layer.scaleMax, localCameraPosition, frustumPlanes, transform, true,
            [i](const Block &block) { return i < block.trees.size() ? &block.trees[i] : nullptr; }
        );
        if (layer.imposter) {
            submitBillboards(renderer, layer, i, false, localCameraPosition, frustumPlanes, transform);
        }
    }

    // Pass 2b: grass layer by layer — the material fields (colors/wind/fade) are set once
    // per layer, bindFields copies them into each draw call (the layers do not conflict).
    // The wind is the scene one (SceneWind): the parameters and phases are shared with all the
    // vegetation of the world.
    AssetRef<MaterialAsset> tuftMaterial = Terrain::Resources::grassMaterial;
    const SceneWindSettings &wind = SceneWind::settings();
    const Vec4 windPhase = SceneWind::phaseVector();
    // The grass thins out from 45% of the distance until it disappears completely.
    const float fadeStart = m_distances.grass * 0.45f;
    const float fadeEnd = m_distances.grass;
    for (size_t layerIndex = 0; layerIndex < settings.grass.size(); layerIndex++) {
        const GrassLayer &layer = settings.grass[layerIndex];
        if (!layer.enabled) { continue; }
        const GrassRender &grassRender = m_grassRenders[layerIndex];
        if (layer.kind == GrassKind::Mesh) {
            AssetRef<ImportedMeshAsset> mesh = layer.mesh.as<ImportedMeshAsset>();
            if (!mesh.isValid()) { continue; } // not assigned/loading
            AssetRef<MaterialAsset> meshMaterial = layer.meshMaterial.as<MaterialAsset>();
            // The per-tuft dissolve distances go as a runtime field of the veg_grass shader (not
            // serialized into .mat; for foreign materials — bushes on PBR — the setter is silent,
            // they disappear as a block). The thinning proportion is the same as for Tuft (45%).
            const float meshFadeEnd = grassMeshFadeEnd(m_distances);
            const Vec4 fade(meshFadeEnd * 0.45f, meshFadeEnd, 0.f, 0.f);
            if (meshMaterial.get()) { meshMaterial->setRuntimeField("grassFade", fade); }
            for (const AssetRef<Asset> &slot : mesh->getPrimitiveMaterials()) {
                AssetRef<MaterialAsset> material = slot.as<MaterialAsset>();
                if (material.get()) { material->setRuntimeField("grassFade", fade); }
            }
            submitMeshBlocks(
                renderer, mesh, meshMaterial, std::max(m_distances.grass, meshFadeEnd),
                grassRender.bounds.height * layer.scaleMax, localCameraPosition, frustumPlanes, transform,
                true,
                [layerIndex](const Block &block) {
                    return layerIndex < block.grass.size() ? &block.grass[layerIndex].mesh : nullptr;
                }
            );
            continue;
        }
        // Tuft: the blade color is the terrain grass palette (the same as for the far view grains).
        tuftMaterial->setFieldValue("colorRoot", settings.grain.shaded(settings.grain.root));
        tuftMaterial->setFieldValue("colorTip", settings.grain.shaded(settings.grain.tip));
        tuftMaterial->setFieldValue("colorExtra", settings.grain.shaded(settings.grain.extra));
        tuftMaterial->setFieldValue(
            "windParams",
            Vec4(windPhase.x, windPhase.y, wind.strength * glm::clamp(layer.windInfluence, 0.f, 1.f),
                 windPhase.w /* flutter phase */)
        );
        tuftMaterial->setFieldValue(
            "timeFade", Vec4(windPhase.z /* gust travel, m */, fadeStart, fadeEnd, wind.gustiness)
        );
        tuftMaterial->setFieldValue(
            "fadeCamera",
            Vec4(localCameraPosition.x, localCameraPosition.y, localCameraPosition.z,
                 glm::clamp(layer.brownian, 0.f, 3.f) /* random tilt multiplier */)
        );

        const BlockRange layerX = blockRange(localCameraPosition.x, fadeEnd, BLOCK_SIZE, m_blocksX);
        const BlockRange layerZ = blockRange(localCameraPosition.z, fadeEnd, BLOCK_SIZE, m_blocksZ);
        for (int bz = layerZ.min; bz <= layerZ.max; bz++) {
            for (int bx = layerX.min; bx <= layerX.max; bx++) {
                Block &block = m_blocks[static_cast<size_t>(bz) * m_blocksX + bx];
                if (block.builtVersion == 0 || block.grass.size() <= layerIndex) { continue; }
                const glm::vec2 center(
                    (static_cast<float>(bx) + 0.5f) * BLOCK_SIZE, (static_cast<float>(bz) + 0.5f) * BLOCK_SIZE
                );
                const float toCamera =
                    glm::distance(center, glm::vec2(localCameraPosition.x, localCameraPosition.z));
                if (toCamera - BLOCK_SIZE * 0.71f > fadeEnd) { continue; }

                const glm::vec3 aabbMin(
                    static_cast<float>(bx) * BLOCK_SIZE, block.minY, static_cast<float>(bz) * BLOCK_SIZE
                );
                const glm::vec3 aabbMax(
                    aabbMin.x + BLOCK_SIZE, block.maxY + layer.scaleMax + 0.5f, aabbMin.z + BLOCK_SIZE
                );
                if (!aabbInsideFrustum(frustumPlanes, aabbMin, aabbMax)) { continue; }

                // The minimum distance to the block (the camera clamped into its XZ rectangle)
                // gives a conservative fade quantum — only the prefix that survives the shader
                // fade is drawn.
                const float nearestX = glm::clamp(localCameraPosition.x, aabbMin.x, aabbMax.x);
                const float nearestZ = glm::clamp(localCameraPosition.z, aabbMin.z, aabbMax.z);
                const float minDistance = glm::distance(
                    glm::vec2(nearestX, nearestZ), glm::vec2(localCameraPosition.x, localCameraPosition.z)
                );
                const float fadeMin =
                    glm::clamp((minDistance - fadeStart) / std::max(fadeEnd - fadeStart, 0.001f), 0.f, 1.f);
                const int bucket = glm::clamp(static_cast<int>(fadeMin * FADE_BUCKETS), 0, FADE_BUCKETS);

                const GrassBlock &grassBlock = block.grass[layerIndex];
                bool blockDrawn = false;
                for (int variant = 0; variant < MAX_VARIANTS; variant++) {
                    const uint32_t count = grassBlock.prefixCounts[variant][bucket];
                    if (count == 0 || !grassBlock.tufts[variant].isValid() || !grassRender.tufts[variant]) {
                        continue;
                    }
                    renderer.submitInstanced(
                        transform, *grassRender.tufts[variant], tuftMaterial, grassBlock.tufts[variant],
                        m_instanceLayout, count
                    );
                    s_frameStats.instances += count;
                    blockDrawn = true;
                }
                if (blockDrawn && block.visibleStamp != m_frameStamp) {
                    block.visibleStamp = m_frameStamp;
                    s_frameStats.visibleBlocks++;
                }
            }
        }
    }

    // Residency: distant blocks release their buffers (they are restored on return).
    const float freeDistance = maxDistance + FREE_MARGIN;
    for (uint32_t bz = 0; bz < m_blocksZ; bz++) {
        for (uint32_t bx = 0; bx < m_blocksX; bx++) {
            Block &block = m_blocks[static_cast<size_t>(bz) * m_blocksX + bx];
            if (block.builtVersion == 0) { continue; }
            const glm::vec2 center(
                (static_cast<float>(bx) + 0.5f) * BLOCK_SIZE, (static_cast<float>(bz) + 0.5f) * BLOCK_SIZE
            );
            if (glm::distance(center, glm::vec2(localCameraPosition.x, localCameraPosition.z)) >
                freeDistance + BLOCK_SIZE) {
                releaseBlock(block);
            }
        }
    }
    const float superFreeDistance = imposterMaxDistance + SUPER_BLOCK_SIZE;
    for (uint32_t bz = 0; bz < m_superBlocksZ; bz++) {
        for (uint32_t bx = 0; bx < m_superBlocksX; bx++) {
            SuperBlock &block = m_superBlocks[static_cast<size_t>(bz) * m_superBlocksX + bx];
            if (block.builtVersion == 0) { continue; }
            const glm::vec2 center(
                (static_cast<float>(bx) + 0.5f) * SUPER_BLOCK_SIZE,
                (static_cast<float>(bz) + 0.5f) * SUPER_BLOCK_SIZE
            );
            if (glm::distance(center, glm::vec2(localCameraPosition.x, localCameraPosition.z)) >
                superFreeDistance + SUPER_BLOCK_SIZE) {
                releaseSuperBlock(block);
            }
        }
    }
}

} // namespace Panda
