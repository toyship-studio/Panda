#pragma once

#include "Panda/Renderer/WorldLighting.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <Rain/Rain.hpp>

namespace Panda {

// The crown of a single tree for the occlusion volume: an ellipsoid of revolution (center,
// horizontal and vertical semi-axes) × foliage density (0..1). Provided by the world systems
// (WorldSystem::collectOccluders — flora scattering: mesh AABB × scale × layer Canopy density).
struct Occluder {
    glm::vec3 center;
    float radiusXZ;
    float radiusY;
    float density;
};

// Occlusion Volume (context/lighting.md): crown density → sky visibility (9 cones
// upward, exponential attenuation) and sun transmittance (the integral along the ray to the
// sun) per cell. The grid of CHUNKS is anchored to the world: a chunk = CHUNK_CELLS² cells ×
// SLICES slices, baked independently of its neighbors (the margin around it covers the canopy
// height and the shadow length; above the canopy a ray accumulates nothing), the resident window
// of windowChunks² chunks around the focus lives in a single RGBA8 3D texture: slot = chunk
// index modulo windowChunks (toroidal, no indirection tables), a slot's tile has a one-cell
// border — a hardware trilinear sample is seamless at the joint. R — sky visibility, G and B —
// two transmittance buffers.
// Temporal stability: a chunk that has entered the window fades in with a weight ramp; a rebake
// on a source version change replaces the tile in place (the old one is read until the new one
// arrives); a sun rotation by SUN_STEP_DEGREES — a transmittance-only pass over the whole window
// (cache of crowns and visibility), the results are accumulated and published into the free
// channel together, then the cross-fade weight is driven for FADE_SECONDS; the falloff to open
// at the window edge is computed from the focus (shader: occlusionFocus). The ramps run on the
// application clock (Application::getTime — the anchor of the previous frame, like SceneWind and
// ExposureRenderer): a rendering effect, not a world simulation; without Application (headless)
// — a snap. Bakes are PandaAsync background jobs (up to MAX_JOBS in flight, the nearest chunks
// first); the main thread uploads a finished tile as a region (updateTextureRegion3D).
// Owner — World; Lab bakes a window of 2 × 2 chunks around the tree.
class OcclusionVolume final {
public:
    struct Settings : public Rain::Codable {
        bool enabled = true;
        float aoStrength = 0.8f;      // ambient dimming by sky visibility
        float sunStrengthNear = 0.3f; // sun dimming by transmittance within the CSM distance
        float sunStrengthFar = 0.8f;  // … beyond the CSM distance
        float cellSize = 2.f;         // m
        int windowChunks = 8;         // window side in chunks (2..8): window = windowChunks · 64 cells
        // Extinction per unit of density per meter. Calibration: a birch crown (density 0.5,
        // semi-axis 3 m) — sky from the center ~0.45, at the base ~0.2; spruce (0.9) —
        // 0.24 / 0.06. 1.5 gave black foliage inside any crown.
        float extinction = 0.7f;
        // Offset of the sample from the surface outward along the normal (m): crown and
        // billboard — in full, objects — half, ground and grass — no offset. Smaller — the
        // occlusion is closer to the crown boundary, larger — a more open shell. Live, no rebake.
        float surfaceOffset = 2.f;
        // Exponent of the sky visibility before the dimming: > 1 darkens the thick of the mass,
        // leaves the open untouched.
        float contrast = 1.f;

        RAIN_FIELDS_BEGIN(Settings)
        RAIN_FIELD(enabled)
        RAIN_FIELD(aoStrength)
        RAIN_FIELD(sunStrengthNear)
        RAIN_FIELD(sunStrengthFar)
        RAIN_FIELD(cellSize)
        RAIN_FIELD(windowChunks)
        RAIN_FIELD(extinction)
        RAIN_FIELD(surfaceOffset)
        RAIN_FIELD(contrast)
        RAIN_FIELDS_END
    };
    static constexpr int CHUNK_CELLS = 64;         // chunk cells per side
    static constexpr int SLICES = 48;              // slices along the height
    static constexpr int TILE = CHUNK_CELLS + 2;   // slot tile: cells + border
    static constexpr int MAX_WINDOW = 8;           // OCCLUSION_MAX_CHUNKS = MAX_WINDOW²
    static constexpr int MAX_JOBS = 2;             // bakes in flight (PandaAsync has 2 workers)
    static constexpr float SUN_STEP_DEGREES = 15.f; // sun rotation that triggers the transmittance pass
    static constexpr float FADE_SECONDS = 0.5f;     // ramp of chunk fade-in and transmittance cross-fade
    static constexpr int PUBLISH_PER_FRAME = 16;    // sun pass tiles per frame (an 8 × 8 window — 4 frames)

    // Collector of crowns in a world AABB (called on the main thread when a chunk bake starts).
    using Collector =
        std::function<void(const glm::vec3 &minWorld, const glm::vec3 &maxWorld, std::vector<Occluder> &out)>;
    struct ChunkBake; // snapshot and result of a chunk bake (the definition is in the .cpp)

    OcclusionVolume();
    ~OcclusionVolume();
    OcclusionVolume(const OcclusionVolume &) = delete;
    OcclusionVolume &operator=(const OcclusionVolume &) = delete;

    // Once per frame: the window around focus (the camera; in Lab — the origin), publishes
    // finished chunks, schedules bakes, drives the ramps, fills data(). sourceVersion — the
    // version of the crown source (any change = a rebake of the window); directionToLight —
    // toward the sun (unit length; without a sun — up).
    void update(
        const Settings &settings,
        const glm::vec3 &focus,
        const glm::vec3 &directionToLight,
        uint64_t sourceVersion,
        const Collector &collect
    );
    const OcclusionVolumeData &data() const {
        return m_data;
    }
    bool ready() const; // all chunks of the window are published, no bakes and no sun pass

private:
    struct Slot {
        glm::ivec2 chunk{INT32_MIN, INT32_MIN}; // world chunk whose tile is published in the slot
        uint64_t version = 0;
        float originY = 0.f;
        float weight = 0.f;                    // fade-in 0..1 (ramp after a new chunk is published)
        bool ready = false;                    // the tile is published
        std::vector<Occluder> instances;       // crown snapshot (sun pass)
        std::vector<uint8_t> visibility;       // sky visibility tile (sun pass)
        std::vector<uint8_t> transmittance;    // transmittance tile of the current channel (sun pass)
        std::shared_ptr<ChunkBake> job;        // full bake in flight
    };
    // Sun transmittance pass: bakes of all ready slots for the new sun, the results are
    // accumulated and published into the free channel together.
    struct SunPass {
        glm::vec3 sun{0.f, 1.f, 0.f};
        std::vector<int> pending;                                 // slots whose bake is not scheduled yet
        std::vector<std::shared_ptr<ChunkBake>> jobs;             // bakes in flight and finished ones
        bool publishing = false;                                  // all ready — upload by PUBLISH_PER_FRAME
    };

    void reset();
    void ensureVolume();
    void startBake(Slot &slot, const glm::ivec2 &chunk, uint64_t sourceVersion, const Collector &collect);
    void publish(Slot &slot, int slotIndex);
    void startSunPass(const glm::vec3 &sun, int window);
    void driveSunPass();
    void upload(int slotIndex, const std::vector<uint8_t> &pixels);
    float rampStep(); // fraction of the ramp per frame by the application clock

    OcclusionVolumeData m_data;
    std::array<Slot, OCCLUSION_MAX_CHUNKS> m_slots; // index — slotZ · windowChunks + slotX
    Settings m_settings;
    int m_windowChunks = 0;
    bool m_configured = false;
    int m_inFlight = 0;
    glm::vec3 m_sun[2] = {glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f)}; // sun of the G and B channels
    int m_front = 0;                                                            // channel of the current sun
    float m_blend = 0.f;                                                        // channel B weight (ramp to m_front)
    float m_lastTime = -1.f;                                                    // Application::getTime anchor
    std::unique_ptr<SunPass> m_sunPass;
};

} // namespace Panda
