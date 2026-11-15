#pragma once

#include "Panda/Renderer/FullscreenQuad.hpp"
#include "Panda/Renderer/WorldLighting.hpp"

#include <Miren/Miren.hpp>
#include <Rain/Rain.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

// "graphics.ssao" of a world: the screen-space ambient occlusion.
struct SsaoSettings final : public Rain::Codable {
    bool enabled = true;
    float radius = 0.6f;    // how far an occluder reaches (m)
    float intensity = 1.f;  // 0 — none, 1 — the computed occlusion, above — darker (a power of it)
    // The budget — what the occlusion may cost. A pixel reads the depth 2 × slices × steps times
    // (and five more for its own point and normal); at full resolution there are four times the
    // pixels. More slices — less grain for the blur to hide; more steps — an occluder between
    // two samples is not missed; full resolution — the occlusion follows thin details and
    // silhouettes.
    int slices = 3;              // directions the surroundings of a pixel are cut in (1..MAX_SLICES)
    int steps = 4;               // depth samples along a direction, each way (1..MAX_STEPS)
    bool fullResolution = false; // at the resolution of the frame instead of half of it

    static constexpr int MAX_SLICES = 8;
    static constexpr int MAX_STEPS = 12;

    RAIN_FIELDS_BEGIN(SsaoSettings)
    RAIN_FIELD(enabled)
    RAIN_FIELD(radius)
    RAIN_FIELD(intensity)
    RAIN_FIELD(slices)
    RAIN_FIELD(steps)
    RAIN_FIELD(fullResolution)
    RAIN_FIELDS_END
};

// Screen-space ambient occlusion (shader feature `ssao`, context/lighting.md): how much of the
// sky a point sees past the geometry around it, found in the depth buffer — the contact
// darkening no shadow map gives. The horizon-based integral of GTAO (Jimenez et al., "Practical
// Real-Time Strategies for Accurate Indirect Occlusion") at half resolution or at the full one,
// then a depth-aware blur. What it costs is the budget of SsaoSettings.
//
// The occlusion belongs to the ambient light, so a lit shader must have it WHILE it shades,
// before the depth of its own frame exists. The engine does not draw the scene twice for that:
// the occlusion is computed from the depth of the frame that has just been drawn and read by the
// NEXT frame, which finds its point in it with the view-projection of that frame
// (WorldLightingData::ssao, panda/ssao.hlsli). A point the last frame did not see — a
// disocclusion, a moving object — is recognized by its depth and stays open for one frame.
// Perspective cameras only: an orthographic frame is left without occlusion.
class SsaoRenderer final {
public:
    SsaoRenderer() = default;
    ~SsaoRenderer();
    SsaoRenderer(const SsaoRenderer &) = delete;
    SsaoRenderer &operator=(const SsaoRenderer &) = delete;

    // After the scene of the frame is submitted. depth — the depth of the scene (width × height
    // pixels), projection and viewProj — its camera. Returns what the next frame shades with;
    // disabled or impossible — an empty SsaoData (no occlusion).
    SsaoData apply(
        Miren::TextureHandle depth, uint32_t width, uint32_t height, const glm::mat4 &projection,
        const glm::mat4 &viewProj, const SsaoSettings &settings, FullscreenQuad &quad
    );

private:
    struct Target {
        Miren::FrameBufferHandle frameBuffer;
        Miren::TextureHandle color;
    };
    void ensureTargets(uint32_t width, uint32_t height);
    void releaseTargets();

    Target m_raw;     // the integral, noisy
    Target m_result;  // blurred: r — the share of the sky a point sees, g — its view depth (m)
    Miren::ProgramHandle m_program;
    Miren::ProgramHandle m_blurProgram;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace Panda
