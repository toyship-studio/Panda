#include "Panda/TerrainEditor/TerrainTool.hpp"

#include <PandaEditorKit/EditorHost.hpp>

#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/Input.hpp>
#include <Panda/Terrain/TerrainCommands.hpp>
#include <Panda/Terrain/TerrainComponents.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>

namespace Panda {

namespace {

    constexpr float HALO_LIFT = 0.08f; // lift of the rings above the surface (against z-fighting)
    constexpr int HALO_SEGMENTS = 48;
    constexpr float TWO_PI = 6.28318530718f;

    // Hash → [0, 1): planting positions of the Trees brush (seed — the stroke's attempt counter).
    float hash01(uint32_t a, uint32_t b) {
        uint32_t h = a * 374761393u + b * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFFu) / 16777216.f;
    }

    // World → window; false — the point is behind the camera.
    bool projectPoint(
        const glm::mat4 &viewProj, PandaUI::Rect viewportFrame, const glm::vec3 &point,
        PandaUI::Point &out
    ) {
        const glm::vec4 clip = viewProj * glm::vec4(point, 1.f);
        if (clip.w <= 0.f) { return false; }
        out.x = viewportFrame.origin.x + (clip.x / clip.w * 0.5f + 0.5f) * viewportFrame.size.width;
        out.y = viewportFrame.origin.y +
                (1.f - (clip.y / clip.w * 0.5f + 0.5f)) * viewportFrame.size.height;
        return true;
    }

    // Strokes are undo commands of the terrain DOCUMENT, not of the world (the document model): the
    // host provides the stack.
    CommandHistory *strokeUndoStack(AssetId terrainId) {
        return &Editor::editorHost().assetUndoStack(terrainId);
    }

} // namespace

bool TerrainTool::resolveTarget(
    World &world,
    const glm::vec3 &cameraPosition,
    const glm::vec3 &worldDir,
    float maxDistance,
    Target &outTarget
) {
    // The brush belongs to the terrain editor tab: the target is ITS asset; there may be several
    // instances in the world, the one under the cursor gets painted (the data is shared by all).
    if (m_settings.assetId == 0) { return false; }
    Target base;
    base.terrain2d = AssetRef<Terrain2DAsset>{AssetId(m_settings.assetId)};
    if (base.terrain2d.get() == nullptr || !base.terrain2d->getData().isValid()) {
        base.terrain2d = {};
        base.terrain3d = AssetRef<Terrain3DAsset>{AssetId(m_settings.assetId)};
        if (base.terrain3d.get() == nullptr || !base.terrain3d->getData().isValid()) {
            return false;
        }
    }
    const bool is2d = base.terrain2d.get() != nullptr;

    bool found = false;
    float bestDistance = 0.f;
    auto consider = [&](UUID entityId, const AssetRef<Asset> &componentRef) {
        if (componentRef.getId() != AssetId(m_settings.assetId)) { return; }
        Entity entity = world.getById(entityId);
        if (!entity.isValid()) { return; }
        Target candidate = base;
        candidate.entity = entity;
        candidate.transform = world.getWorldSpaceTransformMatrix(entity);
        candidate.inverseTransform = glm::inverse(candidate.transform);
        const glm::vec3 localOrigin =
            glm::vec3(candidate.inverseTransform * glm::vec4(cameraPosition, 1.f));
        const glm::vec3 localDir =
            glm::normalize(glm::vec3(candidate.inverseTransform * glm::vec4(worldDir, 0.f)));
        glm::vec3 hitPoint, hitNormal;
        const bool hit =
            is2d ? base.terrain2d->getData().raycast(
                       localOrigin, localDir, maxDistance, hitPoint, hitNormal
                   )
                 : base.terrain3d->getData().raycast(
                       localOrigin, localDir, maxDistance, hitPoint, hitNormal
                   );
        if (hit) {
            const glm::vec3 worldHit = glm::vec3(candidate.transform * glm::vec4(hitPoint, 1.f));
            const float distance = glm::length(worldHit - cameraPosition);
            if (!m_hasHit || distance < bestDistance) {
                bestDistance = distance;
                m_hasHit = true;
                m_hitLocal = hitPoint;
                m_hitNormalLocal = hitNormal;
                outTarget = candidate;
            }
        } else if (!found) {
            outTarget = candidate; // fallback without a hit: the LMB gate is there, the halo is not
        }
        found = true;
    };
    if (is2d) {
        auto view2d = world.getAllEntitiesWith<IdComponent, Terrain2DComponent>();
        for (auto handle : view2d) {
            consider(
                view2d.get<IdComponent>(handle).id, view2d.get<Terrain2DComponent>(handle).terrain
            );
        }
    } else {
        auto view3d = world.getAllEntitiesWith<IdComponent, Terrain3DComponent>();
        for (auto handle : view3d) {
            consider(
                view3d.get<IdComponent>(handle).id, view3d.get<Terrain3DComponent>(handle).terrain
            );
        }
    }
    return found;
}

int TerrainTool::resolveGrassChannel(const Target &target) const {
    if (!target.terrain2d.isValid()) { return -1; }
    const auto &layers = target.terrain2d->getFloraSettings().grass; // flora lives in the asset
    if (m_settings.grassLayer < 0 || m_settings.grassLayer >= static_cast<int>(layers.size())) {
        return -1;
    }
    const GrassLayer &layer = layers[m_settings.grassLayer];
    if (layer.placement == GrassPlacement::Procedural) { return -1; } // the channel is not read
    return static_cast<int>(layer.maskChannel);
}

const TreeLayer *TerrainTool::resolveTreeLayer(const Target &target) const {
    if (!target.terrain2d.isValid()) { return nullptr; }
    for (const TreeLayer &layer : target.terrain2d->getFloraSettings().trees) {
        if (layer.id == m_settings.treeLayer) { return &layer; }
    }
    return nullptr;
}

// Tick of the Trees brush. Planting is spraying: rate attempts per second, the position is uniform
// within the brush circle; a point closer than treeSpacing to any tree is not placed (a held brush
// saturates the circle and does not densify it further). Erasing — all the trees of the circle.
void TerrainTool::tickTrees(const Target &target, double deltaTime) {
    Terrain2DAsset &asset = *target.terrain2d.get();
    if (m_settings.treeErase) {
        asset.eraseTrees(
            m_hitLocal.x, m_hitLocal.z, m_settings.radius,
            m_settings.treeEraseAll ? 0u : m_settings.treeLayer, m_strokeTreesRemoved
        );
        return;
    }
    const TreeLayer *layer = resolveTreeLayer(target);
    if (layer == nullptr) { return; }
    const TerrainData2D &data = asset.getData();
    const float extentX = static_cast<float>(data.cellsX) * data.cellSize;
    const float extentZ = static_cast<float>(data.cellsZ) * data.cellSize;
    m_treeBudget += m_settings.treeRate * static_cast<float>(deltaTime);
    while (m_treeBudget >= 1.f) {
        m_treeBudget -= 1.f;
        const uint32_t seed = ++m_treeSeed;
        const float distance = m_settings.radius * std::sqrt(hash01(seed, 1u)); // uniform over the area
        const float angle = TWO_PI * hash01(seed, 2u);
        TreeInstance tree;
        tree.x = m_hitLocal.x + distance * cosf(angle);
        tree.z = m_hitLocal.z + distance * sinf(angle);
        if (tree.x < 0.f || tree.z < 0.f || tree.x >= extentX || tree.z >= extentZ) { continue; }
        if (asset.getTrees().anyWithin(tree.x, tree.z, m_settings.treeSpacing)) { continue; }
        tree.yaw = TWO_PI * hash01(seed, 3u);
        tree.scale = glm::mix(layer->scaleMin, layer->scaleMax, hash01(seed, 4u));
        tree.layer = layer->id;
        asset.plantTree(tree);
        m_strokeTreesAdded.push_back(tree);
    }
}

void TerrainTool::beginStroke(const Target &target, const glm::vec3 &hitLocal) {
    m_strokeActive = true;
    m_strokeTool = m_settings.tool;
    m_strokeAssetId = target.terrain2d.isValid() ? target.terrain2d.getId() : target.terrain3d.getId();
    m_flattenTarget = hitLocal.y;
    if (m_strokeTool == TerrainToolKind::Trees) {
        m_strokeTreesAdded.clear();
        m_strokeTreesRemoved.clear();
        m_treeBudget = 1.f; // the first tick plants right away — a single click places a tree
    } else if (m_strokeTool == TerrainToolKind::Grass) {
        m_strokeBeforeFloraMask = target.terrain2d->getData().floraMask;
        m_strokeRegion2D = {};
    } else if (target.terrain2d.isValid()) {
        m_strokeBeforeHeights = target.terrain2d->getData().heights;
        m_strokeBeforeLayers = target.terrain2d->getData().layers;
        m_strokeRegion2D = {};
    } else {
        m_strokeBeforeVoxels = target.terrain3d->getData().materials;
        m_strokeRegion3D = {};
        m_brush3d.beginStroke(hitLocal.y);
    }
}

void TerrainTool::finishStroke() {
    if (!m_strokeActive) { return; }
    m_strokeActive = false;
    m_brush3d.endStroke();

    // Stroke kind = asset kind (the 2D branches are flora and heightmap, otherwise voxels).
    Terrain2DAsset *asset2d = AssetRef<Terrain2DAsset>{m_strokeAssetId}.get();
    Terrain3DAsset *asset3d =
        asset2d != nullptr ? nullptr : AssetRef<Terrain3DAsset>{m_strokeAssetId}.get();
    if (asset2d == nullptr && asset3d == nullptr) { return; }

    if (m_strokeTool == TerrainToolKind::Trees) {
        if (asset2d == nullptr || (m_strokeTreesAdded.empty() && m_strokeTreesRemoved.empty())) { return; }
        asset2d->commitTreeEdits(); // crowns of the occlusion volume — once per stroke
        EditTerrainTreesCommand command(
            m_strokeAssetId, std::move(m_strokeTreesRemoved), std::move(m_strokeTreesAdded)
        );
        m_strokeTreesRemoved.clear();
        m_strokeTreesAdded.clear();
        // The edits are already applied by the stroke ticks — the command is only for undo/redo.
        if (CommandHistory *stack = strokeUndoStack(m_strokeAssetId)) { stack->SAVE(command, false); }
        return;
    }

    if (m_strokeTool == TerrainToolKind::Grass) {
        if (asset2d == nullptr || !m_strokeRegion2D.touched) { return; }
        const TerrainData2D &data = asset2d->getData();
        if (m_strokeBeforeFloraMask.size() != data.floraMask.size()) {
            return; // resize in the middle of a stroke
        }
        const TerrainBrushRegion &region = m_strokeRegion2D;
        const uint32_t channel = static_cast<uint32_t>(m_strokeGrassChannel);
        std::vector<uint8_t> before;
        std::vector<uint8_t> after;
        // The stroke region is in vertex coordinates, while the mask lives on its own coarse grid:
        // the index is converted, the command still works per vertex (setFloraMaskAt itself
        // maps the write to the right node).
        const size_t floraStride = data.floraCountX();
        before.reserve(
            static_cast<size_t>(region.maxX - region.minX + 1) * (region.maxZ - region.minZ + 1)
        );
        after.reserve(before.capacity());
        for (uint32_t z = region.minZ; z <= region.maxZ; z++) {
            for (uint32_t x = region.minX; x <= region.maxX; x++) {
                const size_t base =
                    ((static_cast<size_t>(z >> data.floraShift)) * floraStride +
                     (x >> data.floraShift)) *
                    TerrainData2D::FLORA_MASK_CHANNELS;
                before.push_back(m_strokeBeforeFloraMask[base + channel]);
                after.push_back(data.floraMask[base + channel]);
            }
        }
        m_strokeBeforeFloraMask.clear();

        UpdateTerrainFloraCommand command(
            m_strokeAssetId, channel, region.minX, region.minZ, region.maxX, region.maxZ,
            std::move(before), std::move(after)
        );
        // The edits are already applied by the stroke ticks — the command is only for undo/redo.
        if (CommandHistory *stack = strokeUndoStack(m_strokeAssetId)) { stack->SAVE(command, false); }
        return;
    }

    if (asset2d != nullptr) {
        if (!m_strokeRegion2D.touched) { return; }
        const TerrainData2D &data = asset2d->getData();
        if (m_strokeBeforeHeights.size() != data.heights.size() ||
            m_strokeBeforeLayers.size() != data.layers.size()) {
            return; // resize in the middle of a stroke
        }

        const TerrainBrushRegion &region = m_strokeRegion2D;
        std::vector<float> before;
        std::vector<float> after;
        std::vector<uint8_t> beforeLayers;
        std::vector<uint8_t> afterLayers;
        const size_t rowStride = data.vertexCountX(); // heights — on the vertex grid
        const size_t layerStride = data.layerCountX(); // layers — on their own, coarser one
        before.reserve(
            static_cast<size_t>(region.maxX - region.minX + 1) * (region.maxZ - region.minZ + 1)
        );
        after.reserve(before.capacity());
        beforeLayers.reserve(before.capacity());
        afterLayers.reserve(before.capacity());
        for (uint32_t z = region.minZ; z <= region.maxZ; z++) {
            for (uint32_t x = region.minX; x <= region.maxX; x++) {
                // The command stores heights in meters — quantization remains a detail of the data.
                before.push_back(TerrainData2D::decodeHeight(m_strokeBeforeHeights[z * rowStride + x]));
                after.push_back(TerrainData2D::decodeHeight(data.heights[z * rowStride + x]));
                const size_t layerIndex =
                    (static_cast<size_t>(z >> data.layerShift)) * layerStride +
                    (x >> data.layerShift);
                beforeLayers.push_back(m_strokeBeforeLayers[layerIndex]);
                afterLayers.push_back(data.layers[layerIndex]);
            }
        }
        m_strokeBeforeHeights.clear();
        m_strokeBeforeLayers.clear();

        UpdateTerrainCommand command(
            m_strokeAssetId, region.minX, region.minZ, region.maxX, region.maxZ, std::move(before),
            std::move(after), std::move(beforeLayers), std::move(afterLayers)
        );
        // The edits are already applied by the stroke ticks — the command is only for undo/redo.
        if (CommandHistory *stack = strokeUndoStack(m_strokeAssetId)) { stack->SAVE(command, false); }
        return;
    }

    if (!m_strokeRegion3D.touched) { return; }
    const TerrainData3D &data = asset3d->getData();
    if (m_strokeBeforeVoxels.size() != data.materials.size()) { return; }

    const TerrainBrushRegion3D &region = m_strokeRegion3D;
    std::vector<uint8_t> before;
    std::vector<uint8_t> after;
    const size_t count = static_cast<size_t>(region.maxX - region.minX + 1) *
                         (region.maxY - region.minY + 1) * (region.maxZ - region.minZ + 1);
    before.reserve(count);
    after.reserve(count);
    auto voxelIndex = [&data](int x, int y, int z) {
        return (static_cast<size_t>(y) * data.cellsZ + static_cast<size_t>(z)) * data.cellsX +
               static_cast<size_t>(x);
    };
    for (int y = region.minY; y <= region.maxY; y++) {
        for (int z = region.minZ; z <= region.maxZ; z++) {
            for (int x = region.minX; x <= region.maxX; x++) {
                before.push_back(m_strokeBeforeVoxels[voxelIndex(x, y, z)]);
                after.push_back(data.materials[voxelIndex(x, y, z)]);
            }
        }
    }
    m_strokeBeforeVoxels.clear();

    UpdateTerrain3DCommand command(
        m_strokeAssetId, region.minX, region.minY, region.minZ, region.maxX, region.maxY,
        region.maxZ, std::move(before), std::move(after)
    );
    if (CommandHistory *stack = strokeUndoStack(m_strokeAssetId)) {
        stack->SAVE(command, false);
    }
}

void TerrainTool::settingsChanged() {
    Editor::EditorHost &host = Editor::editorHost();
    if (!m_settings.isActive()) {
        host.endViewportEditing();
        return;
    }
    const char *action = "Sculpting Terrain — ";
    switch (m_settings.tool) {
        case TerrainToolKind::Paint: action = "Painting Soil — "; break;
        case TerrainToolKind::Grass: action = "Painting Grass — "; break;
        case TerrainToolKind::Trees: action = "Planting Trees — "; break;
        case TerrainToolKind::Sculpt:
        case TerrainToolKind::None: break;
    }
    const AssetId document(m_settings.assetId);
    host.beginViewportEditing(action + host.assetTitle(document), document, [this] { release(); });
}

void TerrainTool::release() {
    const AssetId document(m_settings.assetId);
    m_settings.tool = TerrainToolKind::None;
    m_settings.assetId = 0;
    settingsChanged(); // the viewport session ends
    if (document != AssetId(0)) { Editor::editorHost().refreshAssetEditors(document); }
}

void TerrainTool::update(const Editor::ViewportToolFrame &frame) {
    World &world = frame.world;
    Camera &camera = frame.camera;
    const glm::mat4 &view = frame.view;
    const glm::vec3 &cameraPosition = frame.cameraPosition;
    const bool viewportHovered = frame.viewportHovered;
    const double deltaTime = frame.deltaTime;
    m_hasHit = false;
    m_driving = false;
    m_viewportHovered = viewportHovered;

    // Ray from the cursor: viewport-local position → far point → direction.
    const Vec2 screen(
        static_cast<float>(Input::getMouseViewportPositionX()),
        static_cast<float>(Input::getMouseViewportPositionY())
    );
    const Vec3 farPoint = camera.screenCoordToWorld(view, screen, camera.getFar());
    const glm::vec3 worldDir =
        glm::normalize(glm::vec3(farPoint.x, farPoint.y, farPoint.z) - cameraPosition);

    Target target;
    const bool hasTarget =
        m_settings.isActive() && resolveTarget(world, cameraPosition, worldDir, camera.getFar(), target);
    const bool mouseDown = Input::isMouseButtonPressed(Fern::MouseButton::LEFT);
    if (!hasTarget || (m_strokeActive && AssetId(m_settings.assetId) != m_strokeAssetId)) {
        finishStroke();
        if (!hasTarget) { return; }
    }
    m_driving = true;
    m_hitTerrain2d = target.terrain2d;
    m_hitTerrain3d = target.terrain3d;
    m_hitTransform = target.transform;

    if (mouseDown && (viewportHovered || m_strokeActive)) {
        if (!m_strokeActive && m_hasHit && viewportHovered) {
            // Vegetation and soil tools exist only for a heightmap terrain; the Grass brush
            // paints only on a layer with a mask, Trees — on an existing layer (erasing is
            // possible without one as well): without a valid target the stroke does not start.
            bool canStart = true;
            if (m_settings.tool != TerrainToolKind::Sculpt && !target.terrain2d.isValid()) {
                canStart = false;
            } else if (m_settings.tool == TerrainToolKind::Grass) {
                const int channel = resolveGrassChannel(target);
                canStart = channel >= 0;
                m_strokeGrassChannel = channel;
            } else if (m_settings.tool == TerrainToolKind::Trees) {
                canStart = m_settings.treeErase || resolveTreeLayer(target) != nullptr;
            }
            if (canStart) { beginStroke(target, m_hitLocal); }
        }
        if (m_strokeActive && m_hasHit) {
            if (m_strokeTool == TerrainToolKind::Trees) {
                tickTrees(target, deltaTime);
            } else if (m_strokeTool == TerrainToolKind::Grass) {
                const TerrainBrushRegion tick = TerrainBrush2D::applyFloraPaint(
                    target.terrain2d->getData(), m_hitLocal.x, m_hitLocal.z, m_settings.radius,
                    static_cast<uint32_t>(m_strokeGrassChannel), m_settings.grassErase,
                    m_settings.grassStrength, static_cast<float>(deltaTime)
                );
                if (tick.touched) {
                    m_strokeRegion2D.extendRegion(tick);
                    // The mask touches neither the geometry nor the splatmap — only a reseed of
                    // flora blocks.
                    target.terrain2d->markFloraMaskDirty(tick.minX, tick.minZ, tick.maxX, tick.maxZ);
                }
            } else if (target.terrain2d.isValid()) {
                const bool paint = m_strokeTool == TerrainToolKind::Paint;
                const TerrainBrushRegion tick =
                    paint ? TerrainBrush2D::applyPaint(
                                target.terrain2d->getData(), m_hitLocal.x, m_hitLocal.z,
                                m_settings.radius, static_cast<uint8_t>(m_settings.paintLayer),
                                m_settings.paintStrength, static_cast<float>(deltaTime), ++m_paintSeed
                            )
                          : TerrainBrush2D::applyTick(
                                target.terrain2d->getData(), m_settings.sculptMode, m_settings.subtract,
                                m_hitLocal.x, m_hitLocal.z, m_settings.radius, m_settings.strength,
                                m_flattenTarget, static_cast<float>(deltaTime)
                            );
                if (tick.touched) {
                    m_strokeRegion2D.extendRegion(tick);
                    if (paint) {
                        // Layers live in the splatmap — painting does not remesh the geometry.
                        target.terrain2d->markLayersDirty(tick.minX, tick.minZ, tick.maxX, tick.maxZ);
                    } else {
                        target.terrain2d->markVerticesDirty(
                            tick.minX, tick.minZ, tick.maxX, tick.maxZ
                        );
                    }
                    target.terrain2d->setChanged(true);
                }
            } else {
                const TerrainBrushRegion3D tick = m_brush3d.applyTick(
                    target.terrain3d->getData(), m_settings.sculptMode3d, m_settings.subtract, m_hitLocal,
                    m_settings.radius, m_settings.strength, static_cast<float>(deltaTime)
                );
                if (tick.touched) {
                    if (m_strokeRegion3D.touched) {
                        m_strokeRegion3D.extendVoxel(tick.minX, tick.minY, tick.minZ);
                        m_strokeRegion3D.extendVoxel(tick.maxX, tick.maxY, tick.maxZ);
                    } else {
                        m_strokeRegion3D = tick;
                    }
                    target.terrain3d->markVoxelsDirty(
                        tick.minX, tick.minY, tick.minZ, tick.maxX, tick.maxY, tick.maxZ
                    );
                    target.terrain3d->setChanged(true);
                }
            }
        }
    } else {
        finishStroke();
    }
}

void TerrainTool::appendHalo2D(
    std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame,
    const PandaUI::Color &color
) const {
    const Terrain2DAsset *asset = m_hitTerrain2d.get();
    if (asset == nullptr) { return; }
    const TerrainData2D &data = asset->getData();

    // A ring point: a circle in local XZ, seated on the surface (the halo follows the relief).
    auto surfacePoint = [&](float angle, float radius, glm::vec3 &outWorld) {
        const float x = m_hitLocal.x + radius * cosf(angle);
        const float z = m_hitLocal.z + radius * sinf(angle);
        float height;
        glm::vec3 normal;
        if (!data.sampleSurface(x, z, height, normal)) { return false; }
        outWorld = glm::vec3(m_hitTransform * glm::vec4(x, height + HALO_LIFT, z, 1.f));
        return true;
    };
    auto addRing = [&](float radius) {
        glm::vec3 previous;
        bool hasPrevious = surfacePoint(0.f, radius, previous);
        for (int i = 1; i <= HALO_SEGMENTS; i++) {
            glm::vec3 point;
            const bool hasPoint = surfacePoint(TWO_PI * i / HALO_SEGMENTS, radius, point);
            if (hasPrevious && hasPoint) {
                HelperLine line;
                line.color = color;
                if (projectPoint(viewProj, viewportFrame, previous, line.a) &&
                    projectPoint(viewProj, viewportFrame, point, line.b)) {
                    lines.push_back(line);
                }
            }
            previous = point;
            hasPrevious = hasPoint;
        }
    };
    addRing(m_settings.radius);        // brush boundary
    addRing(m_settings.radius * 0.5f); // full-strength zone (falloff)
}

void TerrainTool::appendHalo3D(
    std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame,
    const PandaUI::Color &color
) const {
    // Rings in the plane perpendicular to the hit normal (sculpting works on walls as well).
    const glm::vec3 normal = glm::normalize(m_hitNormalLocal);
    const glm::vec3 helper = std::abs(normal.y) < 0.95f ? glm::vec3(0.f, 1.f, 0.f)
                                                        : glm::vec3(1.f, 0.f, 0.f);
    const glm::vec3 axisA = glm::normalize(glm::cross(normal, helper));
    const glm::vec3 axisB = glm::cross(normal, axisA);
    const glm::vec3 center = m_hitLocal + normal * HALO_LIFT;

    auto addRing = [&](float radius) {
        glm::vec3 previous =
            glm::vec3(m_hitTransform * glm::vec4(center + axisA * radius, 1.f));
        for (int i = 1; i <= HALO_SEGMENTS; i++) {
            const float angle = TWO_PI * i / HALO_SEGMENTS;
            const glm::vec3 point = glm::vec3(
                m_hitTransform *
                glm::vec4(center + (axisA * cosf(angle) + axisB * sinf(angle)) * radius, 1.f)
            );
            HelperLine line;
            line.color = color;
            if (projectPoint(viewProj, viewportFrame, previous, line.a) &&
                projectPoint(viewProj, viewportFrame, point, line.b)) {
                lines.push_back(line);
            }
            previous = point;
        }
    };
    addRing(m_settings.radius);
    addRing(m_settings.radius * 0.5f);
}

void TerrainTool::appendHelperLines(
    std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame
) const {
    // The cursor is outside the viewport (Input clamps to the edge) — the halo is not shown; during
    // an active stroke it is kept.
    if (!m_driving || !m_hasHit || (!m_viewportHovered && !m_strokeActive)) { return; }

    const PandaUI::Color addColor(0.62f, 0.88f, 0.42f, 1.f);      // adding material — green
    const PandaUI::Color subtractColor(0.92f, 0.52f, 0.42f, 1.f); // removing it — red
    bool subtracting = false;
    switch (m_settings.tool) {
        case TerrainToolKind::Sculpt:
            subtracting = m_settings.subtract &&
                          (m_hitTerrain2d.isValid() ? m_settings.sculptMode == TerrainBrushMode2D::Raise
                                                    : m_settings.sculptMode3d != TerrainBrushMode3D::Flatten);
            break;
        case TerrainToolKind::Grass: subtracting = m_settings.grassErase; break;
        case TerrainToolKind::Trees: subtracting = m_settings.treeErase; break;
        case TerrainToolKind::Paint:
        case TerrainToolKind::None: break;
    }
    const PandaUI::Color color = subtracting ? subtractColor : addColor;

    if (m_hitTerrain2d.isValid()) {
        appendHalo2D(lines, viewProj, viewportFrame, color);
    } else {
        appendHalo3D(lines, viewProj, viewportFrame, color);
    }
}

} // namespace Panda
