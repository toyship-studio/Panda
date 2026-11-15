#pragma once

#include <glm/glm.hpp>

#include "Panda/Assets/PostprocessingAsset.hpp"
#include "Panda/Renderer/BloomRenderer.hpp"
#include "Panda/Renderer/ExposureRenderer.hpp"
#include "Panda/Renderer/FogRenderer.hpp"
#include "Panda/Renderer/FullscreenQuad.hpp"
#include "Panda/Renderer/PassTarget.hpp"
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
    // Screen position of the sun (WorldView::sunScreen): bloom_apply flares. z = 0 — off.
    glm::vec4 sunScreen = glm::vec4(0.f);
    // The camera of the frame (WorldView::camera) and the ambient occlusion settings of the
    // world: the occlusion is computed from the depth of this frame for the next one of the view
    // (PostprocessingPipeline::screenOcclusion). Disabled by default.
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
    // What the frame takes from the view it was drawn through and from the world it shows: the
    // camera of the frame, the sun on screen, the ambient occlusion and the fog. The one place
    // that maps a drawn world to the pipeline (WorldView::postprocessingInputs).
    void setView(const class WorldView &view, class World &world);

private:
    static SsaoSettings disabledSsao() {
        SsaoSettings settings;
        settings.enabled = false;
        return settings;
    }
};

// Executes the postprocessing chain of a view (WorldView::develop owns and runs it): every
// enabled pass (an ordinary MaterialAsset) renders a fullscreen quad into a ping-pong FBO; the
// input of the first pass is the scene frame. The chain works in LINEAR HDR (RGBA16F targets); at
// the end the pipeline itself executes the implicit Tonemap pass (the exposure, the curve of the
// camera, sRGB) — it always runs, even with an empty/missing chain.
// Convention for the uniforms the pipeline substitutes (the rest are material fields):
//   inputTexture    — the frame from the previous pass,
//   originalTexture — the frame before the chain,
//   emissiveTexture — the scene emissive buffer,
//   depthTexture    — the scene depth,
//   PANDA_POST      — the PostUbo block (`#include "panda/post.hlsli"`).
// The pass vertex shader gets the quad as position/uv as is (see the postprocess.vert template).
// Every pass is a view of the frame being built, made in the order the passes run: any number of
// pipelines work in one frame, each after the scene it develops was rendered.
class PostprocessingPipeline final {
public:
    static constexpr int MAX_PASSES = 8; // the user passes of a chain that are run; the rest is ignored

    PostprocessingPipeline() = default;
    ~PostprocessingPipeline();
    PostprocessingPipeline(const PostprocessingPipeline &) = delete;
    PostprocessingPipeline &operator=(const PostprocessingPipeline &) = delete;

    // Runs the chain + Tonemap; returns the display-ready result texture.
    // width/height — the frame size in pixels (the targets are recreated on change).
    Miren::TextureHandle apply(
        AssetRef<PostprocessingAsset> chain,
        const PostprocessingFrameInputs &inputs,
        uint32_t width,
        uint32_t height,
        float time
    );
    // The same, but the final pass is drawn into a target of the caller — `output` describes its
    // view, whose viewport is the size of the frame (the runtime outputs straight to the screen,
    // without a copy pass). Answers with the view of that pass.
    Miren::ViewId applyToView(
        AssetRef<PostprocessingAsset> chain,
        const PostprocessingFrameInputs &inputs,
        float time,
        const Miren::ViewDescription &output
    );
    // The screen-space ambient occlusion of the frame the last apply worked on: the next frame
    // of the view shades with it. Empty — none.
    const SsaoData &screenOcclusion() const {
        return m_screenOcclusion;
    }

private:
    struct ChainResult {
        Miren::TextureHandle color; // what the last pass into a target of the pipeline left
        int outputView = -1;        // the view of the pass drawn into the output of the caller; -1 — none was
    };
    // output — where the last pass goes: nullptr — into a target of the pipeline.
    ChainResult
    runChain(AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, uint32_t width,
             uint32_t height, float time, const Miren::ViewDescription *output);

    // Ping-pong pairs by the pass resolution divisor (1/2/4/8): index = log2(divisor).
    // Created lazily — only for the divisors that actually occur in the chain.
    PassTarget m_targets[4][2];
    FullscreenQuad m_quad; // the single quad of the post effects (bloom/exposure get the same one)
    FogRenderer m_fog;     // the fog of the world: first, the rest sees the frame with the air in it
    ExposureRenderer m_exposure;
    BloomRenderer m_bloom; // built-in bloom: the pyramid before the user chain
    Miren::ProgramHandle m_antialiasing; // fxaa.frag: the pass after the Tonemap
    SsaoRenderer m_ssao;   // the ambient occlusion of this frame, for the next one
    SsaoData m_screenOcclusion;
};

} // namespace Panda
