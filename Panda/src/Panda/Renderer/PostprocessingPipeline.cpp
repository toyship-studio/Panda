#include "Panda/Renderer/PostprocessingPipeline.hpp"
#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/GameLogic/Components/BaseComponents.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/Renderer/WorldView.hpp"
#include "Panda/Renderer/ShaderFeatures.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Foundation/Profile.hpp>

#include <algorithm>
#include <cmath>

namespace Panda {

glm::vec4 projectSunToScreen(const glm::mat4 &viewProj, const glm::vec3 &toSun) {
    const glm::vec4 clip = viewProj * glm::vec4(toSun, 0.f);
    if (clip.w < 1.e-4f) { return glm::vec4(0.f); } // the sun is behind the camera
    glm::vec2 uv = glm::vec2(clip) / clip.w * 0.5f + 0.5f;
    // The UV is in the TEXTURE convention of the backend, the same one in which the passes get
    // fragTexCoord and sample the frame. ndc·0.5+0.5 is GL ("bottom = 0"); on Metal/D3D the
    // origin is at the top, and without the flip Y was mirrored: the flare rays diverged from the
    // sun vertically, and the occlusion sampled emissive past the disc — the effect "disappeared
    // early" as the sun moved away from the center.
    if (!Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft) { uv.y = 1.f - uv.y; }
    // The margin beyond the frame edge is generous: the ghosts and the rays stretch from the
    // center — it is too early to turn the effect off as soon as the disc touches the border.
    if (uv.x < -0.25f || uv.x > 1.25f || uv.y < -0.25f || uv.y > 1.25f) {
        return glm::vec4(0.f);
    }
    return glm::vec4(uv, 1.f, 0.f);
}

const char *tonemapName(Tonemap curve) {
    switch (curve) {
        case Tonemap::AgX: return "agx";
        case Tonemap::Aces: return "aces";
        case Tonemap::Neutral: return "neutral";
        case Tonemap::Reinhard: return "reinhard";
        case Tonemap::None: return "none";
    }
    return "agx";
}

Tonemap tonemapFromName(const std::string &name) {
    for (Tonemap curve : {Tonemap::AgX, Tonemap::Aces, Tonemap::Neutral, Tonemap::Reinhard, Tonemap::None}) {
        if (name == tonemapName(curve)) { return curve; }
    }
    return Tonemap::AgX;
}

void PostprocessingFrameInputs::setCamera(const CameraComponent &camera) {
    bloom.enabled = camera.bloomEnabled;
    bloom.intensity = camera.bloomIntensity;
    bloom.radius = camera.bloomRadius;
    exposure.enabled = camera.autoExposureEnabled;
    exposure.key = camera.autoExposureKey;
    exposure.speed = camera.autoExposureSpeed;
    exposure.minExposure = camera.autoExposureMin;
    exposure.maxExposure = camera.autoExposureMax;
    tonemap.curve = camera.tonemap;
    tonemap.exposure = camera.exposure;
    tonemap.antialiasing = camera.antialiasing;
    cameraNear = camera.camera.getNear();
    cameraFar = camera.camera.getFar();
}

void PostprocessingFrameInputs::setView(const WorldView &view, World &world) {
    sunScreen = view.sunScreen();
    projection = view.camera().projection;
    viewProj = view.camera().viewProj;
    const WorldGraphicsSettings &graphics = world.getGraphicsSettings();
    ssao = graphics.ssao;
    fog.settings = graphics.fog;
    fog.viewProj = viewProj;
    fog.scene = &view.sceneBlocks();
}

namespace {

    // Pass resolution divisor → index of the target pair; non-powers of two are brought down to
    // the nearest lower one.
    int downsampleIndex(uint32_t downsample) {
        if (downsample >= 8) { return 3; }
        if (downsample >= 4) { return 2; }
        if (downsample >= 2) { return 1; }
        return 0;
    }

} // namespace

PostprocessingPipeline::~PostprocessingPipeline() {
    if (m_antialiasing.isValid()) { Miren::deleteProgram(m_antialiasing); }
}

Miren::TextureHandle PostprocessingPipeline::apply(
    AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, uint32_t width, uint32_t height,
    float time
) {
    return runChain(chain, inputs, width, height, time, nullptr).color;
}

Miren::ViewId PostprocessingPipeline::applyToView(
    AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, float time,
    const Miren::ViewDescription &output
) {
    const uint32_t width = static_cast<uint32_t>(std::max(output.viewport.size.width, 0));
    const uint32_t height = static_cast<uint32_t>(std::max(output.viewport.size.height, 0));
    const ChainResult result = runChain(chain, inputs, width, height, time, &output);
    // Nothing reached the target (no size, no Tonemap program yet): it is cleared all the same.
    return result.outputView >= 0 ? static_cast<Miren::ViewId>(result.outputView) : Miren::addView(output);
}

PostprocessingPipeline::ChainResult PostprocessingPipeline::runChain(
    AssetRef<PostprocessingAsset> chain, const PostprocessingFrameInputs &inputs, uint32_t width, uint32_t height,
    float time, const Miren::ViewDescription *output
) {
    PANDA_PROFILE_SCOPE_N("Postprocessing::runChain");
    // isValid() of the Miren handles is non-const — working copies.
    Miren::TextureHandle input = inputs.color;
    Miren::TextureHandle emissive = inputs.emissive;
    Miren::TextureHandle depth = inputs.depth;
    if (width == 0 || height == 0) { return {input}; }
    // The ambient occlusion of this frame for the next one: from the depth the scene has just
    // been drawn into.
    m_screenOcclusion = ShaderFeatures::enabled().has(ShaderFeature::Ssao)
                            ? m_ssao.apply(depth, width, height, inputs.projection, inputs.viewProj, inputs.ssao, m_quad)
                            : SsaoData{};
    // The order of the engine part: exposure → bloom (the pipeline: HDR → luminance → adaptation →
    // bloom; everything after bloom is the user chain + the fallback tonemap).
    // The fog is a part of the scene: the exposure measures the frame with it, the bloom glows
    // through it.
    input = m_fog.apply(input, depth, width, height, inputs.fog, m_quad);
    input = m_exposure.apply(input, width, height, inputs.exposure, time, m_quad);
    // The built-in camera bloom goes before the chain: the passes (and originalTexture) see the
    // frame already with the glow.
    input = m_bloom.apply(input, emissive, width, height, inputs.bloom, m_quad);
    // Collect the active passes with valid shaders (no more than MAX_PASSES).
    // The user passes, the Tonemap, the antialiasing. A pass without a material is the engine's
    // own program: it has no fields to bind.
    Miren::ProgramHandle shaders[MAX_PASSES + 2];
    AssetRef<MaterialAsset> materials[MAX_PASSES + 2];
    uint32_t downsamples[MAX_PASSES + 2];
    int passCount = 0;
    if (chain) {
        for (PostprocessingPass &pass : chain->getPasses()) {
            if (!pass.enabled || !pass.material || passCount >= MAX_PASSES) { continue; }
            AssetRef<MaterialAsset> material = pass.material.as<MaterialAsset>();
            if (!material) { continue; }
            Miren::ProgramHandle shader = material->getShaderAsset()->getMirenHandle();
            if (!shader.isValid()) { continue; }
            shaders[passCount] = shader;
            materials[passCount] = material;
            downsamples[passCount] = std::max(1u, pass.downsample);
            ++passCount;
        }
    }
    // The implicit final pass: the chain works in linear HDR, Tonemap converts to display
    // space. A chain with its OWN tonemap (handlesTonemap) gets no fallback:
    // its passes after the tonemap pass already work in display (glare, lens flare, afterimage).
    const bool needsImplicitTonemap = !(chain && chain->handlesTonemap() && passCount > 0);
    AssetRef<MaterialAsset> tonemapMaterial = StaticResources::tonemapMaterial;
    if (needsImplicitTonemap && tonemapMaterial) {
        Miren::ProgramHandle tonemapShader = tonemapMaterial->getShaderAsset()->getMirenHandle();
        if (tonemapShader.isValid()) {
            shaders[passCount] = tonemapShader;
            materials[passCount] = tonemapMaterial;
            downsamples[passCount] = 1;
            ++passCount;
        }
    }
    // Antialiasing works on the picture, so it follows whatever brought the frame to the display.
    int antialiasingPass = -1;
    if (inputs.tonemap.antialiasing && passCount > 0) {
        if (!m_antialiasing.isValid()) {
            m_antialiasing = Miren::createProgram(
                AssetImporterBase::loadShaderProgram("default-shaders/postprocess.vert", "default-shaders/fxaa.frag")
            );
        }
        antialiasingPass = passCount;
        shaders[passCount] = m_antialiasing;
        materials[passCount] = AssetRef<MaterialAsset>();
        downsamples[passCount] = 1;
        ++passCount;
    }
    if (passCount == 0) { return {input}; }

    ChainResult result;
    Miren::TextureHandle current = input;
    for (int i = 0; i < passCount; ++i) {
        const bool isOutputPass = output != nullptr && i == passCount - 1;
        // Pass target size: full / downsample; the output (the screen) is not downsampled.
        uint32_t passWidth = width;
        uint32_t passHeight = height;
        PassTarget *target = nullptr;
        Miren::ViewId viewId = 0;
        if (isOutputPass) {
            viewId = Miren::addView(*output);
            result.outputView = viewId;
        } else {
            passWidth = std::max(1u, width / downsamples[i]);
            passHeight = std::max(1u, height / downsamples[i]);
            // The global parity of i guarantees: neighboring passes of the same divisor do not
            // read/write one target.
            target = &m_targets[downsampleIndex(downsamples[i])][i % 2];
            viewId = target->addView(passWidth, passHeight);
        }

        PANDA_PROFILE_SCOPE_N("Postprocessing: pass");
        Miren::setState(0);
        if (materials[i]) { materials[i]->bindFields(); }
        Miren::addInputTexture("inputTexture", current);
        Miren::addInputTexture("originalTexture", input);
        if (emissive.isValid()) { Miren::addInputTexture("emissiveTexture", emissive); }
        if (depth.isValid()) { Miren::addInputTexture("depthTexture", depth); }
        PostUbo postData{};
        // resolutionTime.xy is the target size of THIS pass: texelSize in the shader becomes the
        // target one, so the downsample itself widens the blur step.
        postData.resolutionTime = glm::vec4((float)passWidth, (float)passHeight, time, 0.f);
        postData.cameraNearFar = glm::vec4(inputs.cameraNear, inputs.cameraFar, 0.f, 0.f);
        // The screen position of the sun (xy — UV, z — "in frame"): for the user passes —
        // flares/glare; the occlusion — by a sample of emissiveTexture at this point.
        postData.sunScreen = inputs.sunScreen;
        postData.tonemapParams = glm::vec4(
            static_cast<float>(inputs.tonemap.curve), std::exp2(inputs.tonemap.exposure), 0.f, 0.f
        );
        bindBlock(postData);
        m_quad.submit(viewId, shaders[i]);
        if (target) { current = target->color(); }
    }
    result.color = current;
    return result;
}

} // namespace Panda
