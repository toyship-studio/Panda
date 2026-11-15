#pragma once

#include <glm/glm.hpp>

#include "Panda/Assets/PostprocessingAsset.hpp"
#include "Panda/Renderer/BloomRenderer.hpp"
#include "Panda/Renderer/ExposureRenderer.hpp"
#include "Panda/Renderer/FogRenderer.hpp"
#include "Panda/Renderer/FullscreenQuad.hpp"
#include "Panda/Renderer/SsaoRenderer.hpp"
#include "Panda/Renderer/Tonemap.hpp"

#include <Miren/Miren.hpp>

#include <cstdint>

namespace Panda {

// Frame inputs for the chain: the scene color is required, the rest — if present (handle validity).
// Projection of a directional light onto the screen (a point at infinity, w = 0): xy — UV,
// z — "in frame" (0/1). Shared by World and the Lab preview — the flare formula is the same.
glm::vec4 projectSunToScreen(const glm::mat4 &viewProj, const glm::vec3 &toSun);

struct PostprocessingFrameInputs {
    Miren::TextureHandle color;    // scene frame (input of the first pass and originalTexture)
    Miren::TextureHandle emissive; // scene emissive buffer (what glows) — the bloom input
    Miren::TextureHandle depth;    // scene depth (non-linear; linearized in the pass shader)
    float cameraNear = 0.1f;
    float cameraFar = 1000.f;
    // Auto exposure of the camera: executed FIRST (before bloom) — from there on the whole
    // pipeline sees an already exposed frame, and the user tonemap need not know about exposure.
    ExposureSettings exposure;
    BloomSettings bloom; // built-in bloom of the camera: executed BEFORE the user chain
    // Screen position of the sun (World::getFrameSunScreen): bloom_apply flares. z = 0 — off.
    glm::vec4 sunScreen = glm::vec4(0.f);
    // The camera of the frame (World::getFrameProjection / getFrameViewProj) and the ambient
    // occlusion settings of the world: the occlusion is computed from the depth of this frame for
    // the next one (PostprocessingPipeline::screenOcclusion). Disabled by default.
    glm::mat4 projection = glm::mat4(1.f);
    glm::mat4 viewProj = glm::mat4(1.f);
    SsaoSettings ssao = disabledSsao();
    // The fog of the world over the scene, before everything else of the pipeline (FogRenderer).
    // Disabled by default.
    FogFrame fog;
    // The exposure compensation and the curve of the final Tonemap pass. A chain with
    // handlesTonemap develops the frame itself: it reads the same values from PANDA_POST.
    TonemapSettings tonemap;

    // The look of a camera: its bloom, auto exposure, tonemap and clip planes. The one place that
    // maps a camera to the pipeline — the editor and the runtime call it.
    void setCamera(const struct CameraComponent &camera);
    // What the frame takes from the world it was drawn from: the camera matrices of its last
    // update, the sun on screen, the ambient occlusion and the fog. The one place that maps a
    // world to the pipeline — the editor and the runtime call it after the world is drawn.
    void setWorld(class World &world);

private:
    static SsaoSettings disabledSsao() {
        SsaoSettings settings;
        settings.enabled = false;
        return settings;
    }
};

// Executes the postprocessing chain: every enabled pass (an ordinary MaterialAsset) renders a
// fullscreen quad into a ping-pong FBO; the input of the first pass is the scene frame. The chain
// works in LINEAR HDR (RGBA16F targets); at the end the pipeline itself executes the implicit
// Tonemap pass (the exposure, the curve of the camera, sRGB) — it always runs, even with an
// empty/missing chain.
// Convention for the uniforms the pipeline substitutes (the rest are material fields):
//   inputTexture    — the frame from the previous pass,
//   originalTexture — the frame before the chain,
//   emissiveTexture — the scene emissive buffer,
//   depthTexture    — the scene depth,
//   PANDA_POST      — the PostUbo block (`#include "panda/post.hlsli"`).
// The pass vertex shader gets the quad as position/uv as is (see the postprocess.vert template).
// The views of the passes are one set per frame (MirenViewDistribution): ONE pipeline per frame
// may run a chain of several passes — the viewport of the frame. Any other pipeline of the same
// frame (a preview, a thumbnail) must end in a single pass submitted to its own view: an empty
// chain, no antialiasing.
class PostprocessingPipeline final {
public:
    static constexpr int MAX_PASSES = 8; // user passes; +1 Tonemap slot (view id layout)

    PostprocessingPipeline();
    ~PostprocessingPipeline();

    // Runs the chain + Tonemap; returns the display-ready result texture.
    // width/height — the frame size in pixels (the targets are recreated on change).
    Miren::TextureHandle apply(
        AssetRef<PostprocessingAsset> chain,
        const PostprocessingFrameInputs &inputs,
        uint32_t width,
        uint32_t height,
        float time
    );
    // The same, but the final pass (Tonemap) is submitted into outputView (its viewport/clear/FB
    // are set up by the caller — the runtime outputs straight to the screen, without a copy pass).
    void applyToView(
        AssetRef<PostprocessingAsset> chain,
        const PostprocessingFrameInputs &inputs,
        uint32_t width,
        uint32_t height,
        float time,
        Miren::ViewId outputView
    );
    // The screen-space ambient occlusion of the frame the last apply worked on: hand it to the
    // world (World::setScreenOcclusion), its next frame shades with it. Empty — none.
    const SsaoData &screenOcclusion() const {
        return m_screenOcclusion;
    }

private:
    Miren::TextureHandle
    runChain(AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, uint32_t width,
             uint32_t height, float time, bool lastPassToOutput, Miren::ViewId outputView);

    struct Target {
        Miren::FrameBufferHandle frameBuffer;
        Miren::FrameBufferSpecification specification;
        Miren::TextureHandle color;
        uint32_t width = 0;
        uint32_t height = 0;
        bool created = false;
    };
    void ensureTarget(Target &target, uint32_t width, uint32_t height);

    // Ping-pong pairs by the pass resolution divisor (1/2/4/8): index = log2(divisor).
    // Created lazily — only for the divisors that actually occur in the chain.
    Target m_targets[4][2];
    FullscreenQuad m_quad; // the single quad of the post effects (bloom/exposure get the same one)
    FogRenderer m_fog;     // the fog of the world: first, the rest sees the frame with the air in it
    ExposureRenderer m_exposure;
    BloomRenderer m_bloom; // built-in bloom: the pyramid before the user chain
    Miren::ProgramHandle m_antialiasing; // fxaa.frag: the pass after the Tonemap
    SsaoRenderer m_ssao;   // the ambient occlusion of this frame, for the next one
    SsaoData m_screenOcclusion;
};

} // namespace Panda
