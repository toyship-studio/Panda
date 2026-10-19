#include "Panda/Renderer/OcclusionVolume.hpp"

#include "Panda/Application/Application.hpp"
#include "Panda/Assets/Base/AssetHandler.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <Foundation/Profile.hpp>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>

namespace Panda {

// Snapshot and result of the bake of one chunk (the job holds a shared_ptr; the main thread reads
// it after done). The density is computed on the padded region (the chunk + the margin on each
// side), the tile gets the chunk cells and a one-cell border. Transmittance is one channel per
// sun: a full bake computes both channels (once, if the suns of the channels are equal), the sun
// pass — only its own channel, the second one and the visibility come from the slot cache.
struct OcclusionVolume::ChunkBake {
    glm::ivec2 chunk{0};
    int slotIndex = 0;
    float cellSize = 2.f;
    float extinction = 0.7f;
    glm::vec3 sun[2] = {glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f)}; // sun of the G and B channels
    uint64_t version = 0;
    bool sunOnly = false;
    int sunChannel = 0;      // the channel computed by the sun pass
    float originY = 0.f;
    glm::ivec2 paddedMin{0}; // world cell (xz) of the padded region corner
    int padded = 0;          // cells of the padded region per side
    int margin = 0;          // margin on each side of the chunk (cells): travel + border + the bilinear sample cell
    int travel = 0;          // horizontal travel of the ray to the sun (cells), shared by all cells and both suns
    std::vector<Occluder> instances;
    std::vector<float> density;           // padded region: index (y·padded + z)·padded + x
    std::vector<uint8_t> visibility;      // tile: sky visibility ×255
    std::vector<uint8_t> transmittance[2]; // transmittance tiles of the G and B channels ×255
    std::vector<uint8_t> pixels;          // RGBA8 tile: (z·SLICES + y)·TILE + x — the layout of a 3D texture region
    std::atomic<bool> done{false};
    bool retired = false; // main thread: the in-flight place is freed

    size_t index(int x, int y, int z) const {
        return (static_cast<size_t>(y) * static_cast<size_t>(padded) + static_cast<size_t>(z)) *
                   static_cast<size_t>(padded) +
               static_cast<size_t>(x);
    }
    static size_t tileIndex(int x, int y, int z) {
        return (static_cast<size_t>(z) * SLICES + static_cast<size_t>(y)) * TILE + static_cast<size_t>(x);
    }
    static size_t tileSize() {
        return static_cast<size_t>(TILE) * SLICES * TILE;
    }
};

namespace {

    constexpr int TRAVEL_CAP = 48;                     // ray travel cap (cells): longer shadows of a low sun are cut off
    constexpr int COLLECT_MARGIN = TRAVEL_CAP + 2 + 4; // margin + a reserve for crowns whose center is beyond the margin

    int floorMod(int value, int modulus) {
        const int result = value % modulus;
        return result < 0 ? result + modulus : result;
    }

    float snap(float value, float cell) {
        return std::floor(value / cell) * cell;
    }

    glm::vec3 normalizeSun(const glm::vec3 &direction) {
        return glm::dot(direction, direction) > 1.e-6f ? glm::normalize(direction) : glm::vec3(0.f, 1.f, 0.f);
    }

    // ρ per cell of the padded region: the sum of the ellipsoid contributions with a soft falloff
    // (1 − r²) over the normalized radius; the clamp — several crowns in a cell do not give an
    // infinite shadow.
    void computeDensity(OcclusionVolume::ChunkBake &bake) {
        const int padded = bake.padded;
        bake.density.assign(static_cast<size_t>(padded) * padded * OcclusionVolume::SLICES, 0.f);
        const float cell = bake.cellSize;
        const glm::vec3 origin(
            static_cast<float>(bake.paddedMin.x) * cell, bake.originY, static_cast<float>(bake.paddedMin.y) * cell
        );
        for (const Occluder &instance : bake.instances) {
            if (instance.density <= 0.f || instance.radiusXZ <= 0.f || instance.radiusY <= 0.f) { continue; }
            const glm::vec3 local = (instance.center - origin) / cell;
            const float rx = instance.radiusXZ / cell;
            const float ry = instance.radiusY / cell;
            const int x0 = std::max(0, static_cast<int>(std::floor(local.x - rx)));
            const int x1 = std::min(padded - 1, static_cast<int>(std::ceil(local.x + rx)));
            const int y0 = std::max(0, static_cast<int>(std::floor(local.y - ry)));
            const int y1 = std::min(OcclusionVolume::SLICES - 1, static_cast<int>(std::ceil(local.y + ry)));
            const int z0 = std::max(0, static_cast<int>(std::floor(local.z - rx)));
            const int z1 = std::min(padded - 1, static_cast<int>(std::ceil(local.z + rx)));
            for (int y = y0; y <= y1; y++) {
                const float dy = (static_cast<float>(y) + 0.5f - local.y) / ry;
                for (int z = z0; z <= z1; z++) {
                    const float dz = (static_cast<float>(z) + 0.5f - local.z) / rx;
                    for (int x = x0; x <= x1; x++) {
                        const float dx = (static_cast<float>(x) + 0.5f - local.x) / rx;
                        const float r2 = dx * dx + dy * dy + dz * dz;
                        if (r2 >= 1.f) { continue; }
                        float &value = bake.density[bake.index(x, y, z)];
                        value = std::min(value + instance.density * (1.f - r2), 4.f);
                    }
                }
            }
        }
    }

    // Sky visibility: 9 lattice directions upward (the vertical + 8 at 45°), each one is the
    // sum of ρ along the ray (a top-down sweep: S(c) = ρ(c)·len + S(c + d)); V = the mean of
    // exp(−k·S). It is computed on the padded region (a ray beyond the chunk edge runs through
    // the margin), the chunk with the border is cut out into the tile.
    void computeVisibility(OcclusionVolume::ChunkBake &bake) {
        const int padded = bake.padded;
        const int slices = OcclusionVolume::SLICES;
        const size_t sliceCells = static_cast<size_t>(padded) * padded;
        std::vector<float> accum(sliceCells * slices, 0.f);
        std::vector<float> previous(sliceCells);
        std::vector<float> current(sliceCells);
        const float k = bake.extinction;
        int directionCount = 0;
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                const float length = bake.cellSize * std::sqrt(1.f + static_cast<float>(dx * dx + dz * dz));
                std::fill(previous.begin(), previous.end(), 0.f);
                for (int y = slices - 1; y >= 0; y--) {
                    for (int z = 0; z < padded; z++) {
                        const int pz = z + dz;
                        for (int x = 0; x < padded; x++) {
                            const int px = x + dx;
                            float sum = bake.density[bake.index(x, y, z)] * length;
                            if (y + 1 < slices && px >= 0 && px < padded && pz >= 0 && pz < padded) {
                                sum += previous[static_cast<size_t>(pz) * padded + px];
                            }
                            current[static_cast<size_t>(z) * padded + x] = sum;
                            accum[bake.index(x, y, z)] += std::exp(-k * sum);
                        }
                    }
                    std::swap(previous, current);
                }
                directionCount++;
            }
        }
        bake.visibility.resize(OcclusionVolume::ChunkBake::tileSize());
        const float normalize = 255.f / static_cast<float>(directionCount);
        const int offset = bake.margin - 1; // the tile starts with the border — the cell before the chunk
        for (int z = 0; z < OcclusionVolume::TILE; z++) {
            for (int y = 0; y < slices; y++) {
                for (int x = 0; x < OcclusionVolume::TILE; x++) {
                    const float value = accum[bake.index(x + offset, y, z + offset)] * normalize;
                    bake.visibility[OcclusionVolume::ChunkBake::tileIndex(x, y, z)] =
                        static_cast<uint8_t>(std::clamp(value, 0.f, 255.f));
                }
            }
        }
    }

    // Sun transmittance: the integral of ρ along the ray to the sun by a top-down sweep over the
    // slices. The ray shift between slices is an INTEGER one, by the world index of the slice
    // (Bresenham): the chain of ray cells is one and the same world line for neighboring chunks
    // and for different volume bottoms, and the truncation by the travel (shared by all cells)
    // is an exact subtraction of the sum taken at the chain point up slices higher. This way the
    // transmittance of chunks with different margins matches at the seam; shadows longer than the
    // travel are lost — this is the cap. A low sun — open.
    // The result is a tile ×255 (the chunk with the border from the padded region).
    void computeTransmittance(const OcclusionVolume::ChunkBake &bake, const glm::vec3 &sunDirection, std::vector<uint8_t> &tile) {
        const int padded = bake.padded;
        const int slices = OcclusionVolume::SLICES;
        std::vector<float> transmittance(bake.density.size(), 1.f);
        const glm::vec3 sun = glm::normalize(sunDirection);
        if (sun.y >= 0.08f) {
            const float ox = sun.x / sun.y; // ray shift per slice (cells)
            const float oz = sun.z / sun.y;
            const float length = bake.cellSize / sun.y;
            const int baseSlice = static_cast<int>(std::lround(bake.originY / bake.cellSize));
            const auto lattice = [&](int absoluteSlice) {
                return glm::ivec2(
                    static_cast<int>(std::lround(ox * static_cast<float>(absoluteSlice))),
                    static_cast<int>(std::lround(oz * static_cast<float>(absoluteSlice)))
                );
            };
            const auto inside = [&](int x, int z) { return x >= 0 && z >= 0 && x < padded && z < padded; };
            std::vector<float> sums(bake.density.size(), 0.f); // sum of ρ·len along the chain from the cell to the top
            for (int y = slices - 1; y >= 0; y--) {
                const glm::ivec2 step = lattice(baseSlice + y + 1) - lattice(baseSlice + y);
                for (int z = 0; z < padded; z++) {
                    for (int x = 0; x < padded; x++) {
                        float sum = bake.density[bake.index(x, y, z)] * length;
                        if (y + 1 < slices && inside(x + step.x, z + step.y)) {
                            sum += sums[bake.index(x + step.x, y + 1, z + step.y)];
                        }
                        sums[bake.index(x, y, z)] = sum;
                    }
                }
            }
            const float horizontal = glm::length(glm::vec2(sun.x, sun.z));
            const int up = horizontal > 1.e-4f
                               ? static_cast<int>(std::floor(static_cast<float>(bake.travel) * sun.y / horizontal))
                               : slices;
            const float k = bake.extinction;
            for (int y = 0; y < slices; y++) {
                const bool truncate = y + up < slices;
                const glm::ivec2 shift =
                    truncate ? lattice(baseSlice + y + up) - lattice(baseSlice + y) : glm::ivec2(0);
                for (int z = 0; z < padded; z++) {
                    for (int x = 0; x < padded; x++) {
                        float sum = sums[bake.index(x, y, z)];
                        if (truncate && inside(x + shift.x, z + shift.y)) {
                            sum = std::max(0.f, sum - sums[bake.index(x + shift.x, y + up, z + shift.y)]);
                        }
                        transmittance[bake.index(x, y, z)] = std::exp(-k * sum);
                    }
                }
            }
        }
        tile.resize(OcclusionVolume::ChunkBake::tileSize());
        const int offset = bake.margin - 1;
        for (int z = 0; z < OcclusionVolume::TILE; z++) {
            for (int y = 0; y < slices; y++) {
                for (int x = 0; x < OcclusionVolume::TILE; x++) {
                    tile[OcclusionVolume::ChunkBake::tileIndex(x, y, z)] = static_cast<uint8_t>(
                        std::clamp(transmittance[bake.index(x + offset, y, z + offset)] * 255.f, 0.f, 255.f)
                    );
                }
            }
        }
    }

    // An RGBA8 tile in the layout of a 3D texture region (the z slices in a row, each one
    // TILE × SLICES): R — sky visibility, G and B — the transmittance of the channels, A — unused.
    void writePixels(OcclusionVolume::ChunkBake &bake) {
        const size_t size = OcclusionVolume::ChunkBake::tileSize();
        bake.pixels.resize(size * 4);
        for (size_t i = 0; i < size; i++) {
            bake.pixels[i * 4 + 0] = bake.visibility[i];
            bake.pixels[i * 4 + 1] = bake.transmittance[0][i];
            bake.pixels[i * 4 + 2] = bake.transmittance[1][i];
            bake.pixels[i * 4 + 3] = 255;
        }
    }

    void bakeChunk(OcclusionVolume::ChunkBake &bake) {
        PANDA_PROFILE_SCOPE_N("OcclusionVolume: chunk");
        computeDensity(bake);
        if (bake.sunOnly) {
            computeTransmittance(bake, bake.sun[bake.sunChannel], bake.transmittance[bake.sunChannel]);
        } else {
            computeVisibility(bake);
            computeTransmittance(bake, bake.sun[0], bake.transmittance[0]);
            if (bake.sun[1] == bake.sun[0]) {
                bake.transmittance[1] = bake.transmittance[0];
            } else {
                computeTransmittance(bake, bake.sun[1], bake.transmittance[1]);
            }
        }
        writePixels(bake);
        bake.density.clear();
        bake.density.shrink_to_fit();
        bake.done.store(true);
    }

    // Ray travel: the height of the canopy (the visibility rays at 45° accumulate nothing above
    // the canopy) and the length of the shadow towards the sun (for both suns of the channels),
    // with a cap; margin = travel + border + the cell of the bilinear sample of the truncation
    // point.
    void prepareField(OcclusionVolume::ChunkBake &bake) {
        const float cell = bake.cellSize;
        int occluderCells = 1;
        for (const Occluder &instance : bake.instances) {
            occluderCells = std::max(
                occluderCells,
                static_cast<int>(std::ceil((instance.center.y + instance.radiusY - bake.originY) / cell))
            );
        }
        occluderCells = std::clamp(occluderCells, 1, OcclusionVolume::SLICES);
        int travel = occluderCells;
        for (const glm::vec3 &sun : bake.sun) {
            const float horizontal = glm::length(glm::vec2(sun.x, sun.z));
            const float vertical = std::max(sun.y, 0.08f);
            travel = std::max(
                travel, static_cast<int>(std::ceil(static_cast<float>(occluderCells) * horizontal / vertical))
            );
        }
        bake.travel = std::min(travel, TRAVEL_CAP);
        bake.margin = bake.travel + 2;
        bake.padded = OcclusionVolume::CHUNK_CELLS + 2 * bake.margin;
        bake.paddedMin = bake.chunk * OcclusionVolume::CHUNK_CELLS - glm::ivec2(bake.margin);
    }

    void submit(const std::shared_ptr<OcclusionVolume::ChunkBake> &bake) {
        Application *application = Application::get();
        if (application == nullptr) { // headless — synchronously
            bakeChunk(*bake);
            return;
        }
        application->getScheduler().submit<int>(
            PandaAsync::JobDesc{"occlusion-chunk"},
            [bake](PandaAsync::Context &) -> int {
                bakeChunk(*bake);
                return 0;
            },
            [](int &&) {}
        );
    }

    float approach(float value, float target, float step) {
        return value + std::clamp(target - value, -step, step);
    }

} // namespace

OcclusionVolume::OcclusionVolume() = default;
OcclusionVolume::~OcclusionVolume() = default;

bool OcclusionVolume::ready() const {
    if (m_windowChunks == 0 || m_sunPass) { return false; }
    for (int i = 0; i < m_windowChunks * m_windowChunks; i++) {
        if (!m_slots[i].ready || m_slots[i].job) { return false; }
    }
    return true;
}

void OcclusionVolume::reset() {
    for (Slot &slot : m_slots) { slot = Slot(); } // bakes in flight live out in their jobs and are discarded
    m_sunPass.reset();
    m_inFlight = 0;
    m_data = OcclusionVolumeData();
    m_windowChunks = 0;
    m_configured = false;
    m_front = 0;
    m_blend = 0.f;
    m_lastTime = -1.f;
}

// The window volume: (windowChunks · TILE) × SLICES × (windowChunks · TILE) RGBA8 without data —
// the slot contents appear as the baking goes, until it is ready the shader does not read a slot.
void OcclusionVolume::ensureVolume() {
    Miren::TextureCreate create;
    create.m_format = Miren::TextureFormat::RGBA8;
    create.m_width = static_cast<uint16_t>(m_windowChunks * TILE);
    create.m_height = static_cast<uint16_t>(SLICES);
    create.m_depth = static_cast<uint16_t>(m_windowChunks * TILE);
    create.m_minFiltering = Miren::LINEAR;
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP;
    if (m_data.volume) {
        m_data.volume->create(create); // the same asset — the shader references stay alive
    } else {
        m_data.volume = GameContext::getAssetHandler()->createRuntimeAsset<TextureAsset>(create);
    }
}

// The frame by the application clock; a clamp of 0.1 s — a hitch or a pause does not teleport the
// ramp. Without Application (headless) the ramps snap, just as the bakes run synchronously.
float OcclusionVolume::rampStep() {
    Application *application = Application::get();
    if (application == nullptr) { return 1.f; }
    const float now = application->getTime();
    const float deltaTime = m_lastTime < 0.f ? 0.f : std::clamp(now - m_lastTime, 0.f, 0.1f);
    m_lastTime = now;
    return deltaTime / FADE_SECONDS;
}

void OcclusionVolume::update(
    const Settings &settings,
    const glm::vec3 &focus,
    const glm::vec3 &directionToLight,
    uint64_t sourceVersion,
    const Collector &collect
) {
    if (!settings.enabled) {
        if (m_configured || m_data.volume) { reset(); }
        return;
    }
    const int window = std::clamp(settings.windowChunks, 2, MAX_WINDOW);
    const float cell = std::max(settings.cellSize, 0.5f);
    const glm::vec3 sun = normalizeSun(directionToLight);
    const bool reconfigure = !m_configured || m_settings.cellSize != cell || m_windowChunks != window ||
                             m_settings.extinction != settings.extinction;
    if (reconfigure) {
        for (Slot &slot : m_slots) { slot = Slot(); }
        m_sunPass.reset();
        m_inFlight = 0;
        m_windowChunks = window;
        m_configured = true;
        m_sun[0] = m_sun[1] = sun;
        m_front = 0;
        m_blend = 0.f;
        ensureVolume();
    }
    m_settings = settings;
    m_settings.cellSize = cell;
    m_settings.windowChunks = window;

    const float chunkWorld = static_cast<float>(CHUNK_CELLS) * cell;
    const glm::ivec2 center(
        static_cast<int>(std::floor(focus.x / chunkWorld)), static_cast<int>(std::floor(focus.z / chunkWorld))
    );
    // The window is symmetric around the focus by position: the edge shifts when the focus crosses
    // the middle of a chunk. The nearest edge is always ≥ (window/2 − 0.5) chunks away — beyond
    // this radius the shader fades the occlusion out (the falloff from the focus is continuous as
    // the camera moves).
    const glm::ivec2 windowMin(
        static_cast<int>(std::floor(focus.x / chunkWorld - static_cast<float>(window) * 0.5f + 0.5f)),
        static_cast<int>(std::floor(focus.z / chunkWorld - static_cast<float>(window) * 0.5f + 0.5f))
    );
    const float fadeRadius = (static_cast<float>(window) * 0.5f - 0.5f) * chunkWorld;

    // Finished full bakes go into the volume: the tile is replaced in place, the old one is read
    // until this frame.
    for (int i = 0; i < window * window; i++) {
        Slot &slot = m_slots[i];
        if (slot.job && slot.job->done.load()) { publish(slot, i); }
    }
    driveSunPass();

    // The needed chunks — the nearest to the focus first: the visible is filled before the distant.
    struct Candidate {
        glm::ivec2 chunk;
        int distance;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(static_cast<size_t>(window) * window);
    for (int z = 0; z < window; z++) {
        for (int x = 0; x < window; x++) {
            const glm::ivec2 chunk = windowMin + glm::ivec2(x, z);
            const glm::ivec2 delta = chunk - center;
            candidates.push_back({chunk, delta.x * delta.x + delta.y * delta.y});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.distance < b.distance;
    });
    for (const Candidate &candidate : candidates) {
        const int slotIndex = floorMod(candidate.chunk.y, window) * window + floorMod(candidate.chunk.x, window);
        Slot &slot = m_slots[slotIndex];
        if (slot.job) { continue; }
        if (slot.ready && slot.chunk == candidate.chunk && slot.version == sourceVersion) { continue; }
        if (m_inFlight >= MAX_JOBS) { break; }
        startBake(slot, candidate.chunk, sourceVersion, collect);
    }

    // Sun pass: the bakes go into the remaining places; a new pass starts when the sun has moved
    // away from the sun of the current channel by a step and the previous pass is finished.
    if (m_sunPass && !m_sunPass->publishing) {
        while (m_inFlight < MAX_JOBS && !m_sunPass->pending.empty()) {
            const int slotIndex = m_sunPass->pending.back();
            m_sunPass->pending.pop_back();
            Slot &slot = m_slots[slotIndex];
            if (!slot.ready || slot.job) { continue; } // the slot is rebaked as a whole — that computes both channels
            const int channel = 1 - m_front;
            auto bake = std::make_shared<ChunkBake>();
            bake->chunk = slot.chunk;
            bake->slotIndex = slotIndex;
            bake->cellSize = cell;
            bake->extinction = std::max(m_settings.extinction, 0.f);
            bake->sun[0] = m_sun[0];
            bake->sun[1] = m_sun[1];
            bake->version = slot.version;
            bake->sunOnly = true;
            bake->sunChannel = channel;
            bake->originY = slot.originY;
            bake->instances = slot.instances;
            bake->visibility = slot.visibility;
            bake->transmittance[m_front] = slot.transmittance;
            prepareField(*bake);
            m_sunPass->jobs.push_back(bake);
            m_inFlight++;
            submit(bake);
        }
    } else if (!m_sunPass && glm::dot(m_sun[m_front], sun) < std::cos(glm::radians(SUN_STEP_DEGREES))) {
        startSunPass(sun, window);
    }

    // Ramps: the fade-in of chunks and the cross-fade of transmittance.
    const float step = rampStep();
    for (int i = 0; i < window * window; i++) {
        Slot &slot = m_slots[i];
        if (slot.ready) { slot.weight = approach(slot.weight, 1.f, step); }
    }
    m_blend = approach(m_blend, static_cast<float>(m_front), step);
    if (!m_sunPass && m_blend == static_cast<float>(m_front)) {
        m_sun[1 - m_front] = m_sun[m_front]; // the channels are equal — a full bake computes transmittance once
    }

    m_data.cellSize = cell;
    m_data.chunkCells = CHUNK_CELLS;
    m_data.tileSize = TILE;
    m_data.slices = SLICES;
    m_data.windowMin = windowMin;
    m_data.windowChunks = static_cast<uint32_t>(window);
    m_data.aoStrength = settings.aoStrength;
    m_data.sunStrengthNear = settings.sunStrengthNear;
    m_data.sunStrengthFar = settings.sunStrengthFar;
    m_data.surfaceOffset = settings.surfaceOffset;
    m_data.contrast = settings.contrast;
    m_data.focus = glm::vec2(focus.x, focus.z);
    m_data.fadeRadius = fadeRadius;
    m_data.transmittanceBlend = m_blend;
    for (size_t i = 0; i < m_data.chunks.size(); i++) {
        const Slot &slot = m_slots[i];
        OcclusionChunk &entry = m_data.chunks[i];
        entry.chunk = slot.chunk;
        entry.originY = slot.originY;
        entry.weight = static_cast<int>(i) < window * window && slot.ready ? slot.weight : 0.f;
    }
}

void OcclusionVolume::startBake(Slot &slot, const glm::ivec2 &chunk, uint64_t sourceVersion, const Collector &collect) {
    auto bake = std::make_shared<ChunkBake>();
    const float cell = m_settings.cellSize;
    bake->chunk = chunk;
    bake->cellSize = cell;
    bake->extinction = std::max(m_settings.extinction, 0.f);
    bake->sun[0] = m_sun[0];
    bake->sun[1] = m_sun[1];
    bake->version = sourceVersion;
    const glm::ivec2 chunkMinCell = chunk * CHUNK_CELLS;
    const float collectMargin = static_cast<float>(COLLECT_MARGIN) * cell;
    const glm::vec3 minWorld(
        static_cast<float>(chunkMinCell.x) * cell - collectMargin, -1.e9f,
        static_cast<float>(chunkMinCell.y) * cell - collectMargin
    );
    const glm::vec3 maxWorld(
        static_cast<float>(chunkMinCell.x + CHUNK_CELLS) * cell + collectMargin, 1.e9f,
        static_cast<float>(chunkMinCell.y + CHUNK_CELLS) * cell + collectMargin
    );
    if (collect) { collect(minWorld, maxWorld, bake->instances); }
    // The bottom of the chunk volume is the ground under the crowns of the chunk itself (center −
    // 2.2·ry ≈ the trunk base); with no crowns of its own — by the crowns of the margin (their
    // relief is closer to the chunk than zero). If it does not fit in height — the treetops get
    // the priority: a point above the volume reads the top slice (open), under the canopy — the
    // bottom one (under a crown), which is more honest than the opposite.
    float low = 1.e9f;
    float top = -1.e9f;
    bool any = false;
    const auto consider = [&](const Occluder &instance) {
        low = std::min(low, instance.center.y - 2.2f * instance.radiusY);
        top = std::max(top, instance.center.y + instance.radiusY);
        any = true;
    };
    for (const Occluder &instance : bake->instances) {
        const glm::vec2 local = glm::vec2(instance.center.x, instance.center.z) / cell - glm::vec2(chunkMinCell);
        if (local.x >= 0.f && local.y >= 0.f && local.x < static_cast<float>(CHUNK_CELLS) &&
            local.y < static_cast<float>(CHUNK_CELLS)) {
            consider(instance);
        }
    }
    if (!any) {
        for (const Occluder &instance : bake->instances) { consider(instance); }
    }
    float originY = 0.f;
    if (any) {
        low -= cell;
        top += cell;
        originY = std::max(low, top - static_cast<float>(SLICES) * cell);
    }
    bake->originY = snap(originY, cell);
    prepareField(*bake);
    // The slot keeps showing the previous tile until the publication: the chunk, the version and
    // the volume bottom change together with the tile in one frame.
    slot.job = bake;
    m_inFlight++;
    submit(bake);
}

void OcclusionVolume::upload(int slotIndex, const std::vector<uint8_t> &pixels) {
    if (!m_data.volume) { return; }
    const int slotX = slotIndex % m_windowChunks;
    const int slotZ = slotIndex / m_windowChunks;
    Miren::updateTextureRegion3D(
        m_data.volume->getMirenHandle(), static_cast<uint16_t>(slotX * TILE), 0, static_cast<uint16_t>(slotZ * TILE),
        TILE, SLICES, TILE, Foundation::Memory::copying(pixels.data(), static_cast<uint32_t>(pixels.size()))
    );
}

void OcclusionVolume::publish(Slot &slot, int slotIndex) {
    std::shared_ptr<ChunkBake> bake = std::move(slot.job);
    slot.job.reset();
    m_inFlight = std::max(0, m_inFlight - 1);
    upload(slotIndex, bake->pixels);
    if (!slot.ready || slot.chunk != bake->chunk) { slot.weight = 0.f; } // a new chunk fades in by the ramp
    slot.instances = std::move(bake->instances);
    slot.visibility = std::move(bake->visibility);
    slot.transmittance = std::move(bake->transmittance[m_front]);
    slot.originY = bake->originY;
    slot.version = bake->version;
    slot.chunk = bake->chunk;
    slot.ready = true;
}

void OcclusionVolume::startSunPass(const glm::vec3 &sun, int window) {
    m_sunPass = std::make_unique<SunPass>();
    m_sunPass->sun = sun;
    m_sun[1 - m_front] = sun; // from this moment full bakes compute both channels
    for (int i = window * window - 1; i >= 0; i--) { m_sunPass->pending.push_back(i); }
}

// Sun pass: while the bakes are in flight — frees the places of the finished ones; when all are
// done — publishes the tiles into the free channel in batches of PUBLISH_PER_FRAME (the old
// channel is read until the switch), then switches the channel: the cross-fade is driven by update.
void OcclusionVolume::driveSunPass() {
    if (!m_sunPass) { return; }
    SunPass &pass = *m_sunPass;
    if (!pass.publishing) {
        bool allDone = pass.pending.empty();
        for (const std::shared_ptr<ChunkBake> &job : pass.jobs) {
            if (!job->retired && job->done.load()) {
                job->retired = true;
                m_inFlight = std::max(0, m_inFlight - 1);
            }
            allDone = allDone && job->retired;
        }
        if (!allDone) { return; }
        pass.publishing = true;
    }
    const int channel = 1 - m_front;
    int uploaded = 0;
    while (!pass.jobs.empty() && uploaded < PUBLISH_PER_FRAME) {
        std::shared_ptr<ChunkBake> job = std::move(pass.jobs.back());
        pass.jobs.pop_back();
        Slot &slot = m_slots[job->slotIndex];
        // A slot rebaked as a whole after the snapshot already carries both channels — the result
        // of the pass is redundant.
        if (slot.ready && !slot.job && slot.chunk == job->chunk && slot.version == job->version) {
            upload(job->slotIndex, job->pixels);
            slot.transmittance = std::move(job->transmittance[channel]);
        }
        uploaded++;
    }
    if (pass.jobs.empty()) {
        m_front = channel;
        m_sunPass.reset();
    }
}

} // namespace Panda
