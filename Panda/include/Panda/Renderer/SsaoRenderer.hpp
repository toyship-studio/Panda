#pragma once

#include "Panda/Renderer/FullscreenQuad.hpp"
#include "Panda/Renderer/PassTarget.hpp"
#include "Panda/Renderer/RenderingSettings.hpp"
#include "Panda/Renderer/WorldLighting.hpp"

#include <Miren/Miren.hpp>
#include <Rain/Rain.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

// The ambient occlusion of a frame: what the camera asks for (CameraComponent: whether it has
// it, how far an occluder reaches, how dark) and what the project lets it cost
// (AmbientOcclusionSettings). Made for a frame, not kept.
struct SsaoSettings final {
    bool enabled = true;
    float radius = 0.6f;   // how far an occluder reaches (m)
    float intensity = 1.f; // 0 — none, 1 — the computed occlusion, above — darker (a power of it)
    int slices = 3;        // AmbientOcclusionSettings
    int steps = 4;
    bool fullResolution = false;
};

// Screen-space ambient occlusion (shader feature `ssao`, context/lighting.md): how much of the
// sky a point sees past the geometry around it, found in the depth buffer — the contact
// darkening no shadow map gives. The horizon-based integral of GTAO (Jimenez et al., "Practical
// Real-Time Strategies for Accurate Indirect Occlusion") at half resolution or at the full one,
// then a depth-aware blur. What it costs is the budget of the project (AmbientOcclusionSettings).
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

    // After the scene pass of the frame. depth — the depth of the scene (width × height
    // pixels), projection and viewProj — its camera. Returns what the next frame shades with;
    // disabled or impossible — an empty SsaoData (no occlusion).
    SsaoData apply(
        Miren::TextureHandle depth, uint32_t width, uint32_t height, const glm::mat4 &projection,
        const glm::mat4 &viewProj, const SsaoSettings &settings, FullscreenQuad &quad
    );

private:
    // RGBA16F: the depth of a point next to its occlusion.
    PassTarget m_raw;    // the integral, noisy
    PassTarget m_result; // blurred: r — the share of the sky a point sees, g — its view depth (m)
    Miren::ProgramHandle m_program;
    Miren::ProgramHandle m_blurProgram;
};

} // namespace Panda
