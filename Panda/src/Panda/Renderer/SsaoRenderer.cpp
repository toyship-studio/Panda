#include "Panda/Renderer/SsaoRenderer.hpp"

#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/Renderer/MirenViewDistribution.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Foundation/Profile.hpp>

#include <algorithm>
#include <cmath>

namespace Panda {

namespace {

    // The block of both passes (ssao.frag, ssao_blur.frag).
    struct PassUbo {
        glm::vec4 projection; // x, y — the scale of the projection (P00, P11), z — 1: depth is -1..1, w — 1: rows go bottom-up
        glm::vec4 params;     // x — radius (m), y — intensity, z — 1 / near, w — 1 / far
        glm::vec4 size;       // xy — the size of the depth in pixels, zw — of the target
        glm::vec4 budget;     // x — slices, y — steps along a slice each way, z — depth texels per pixel of the target
    };

} // namespace

SsaoRenderer::~SsaoRenderer() {
    releaseTargets();
    if (m_program.isValid()) { Miren::deleteProgram(m_program); }
    if (m_blurProgram.isValid()) { Miren::deleteProgram(m_blurProgram); }
}

void SsaoRenderer::releaseTargets() {
    for (Target *target : {&m_raw, &m_result}) {
        if (!target->color.isValid()) { continue; }
        Miren::deleteFrameBuffer(target->frameBuffer);
        Miren::deleteTexture(target->color);
        *target = Target{};
    }
}

void SsaoRenderer::ensureTargets(uint32_t width, uint32_t height) {
    if (m_raw.color.isValid() && m_width == width && m_height == height) { return; }
    releaseTargets();
    for (Target *target : {&m_raw, &m_result}) {
        Miren::TextureCreate create;
        create.m_width = width;
        create.m_height = height;
        create.m_format = Miren::TextureFormat::RGBA16F; // the depth of a point next to its occlusion
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        target->color = Miren::createTexture(create);
        Miren::FrameBufferAttachment attachments[] = {target->color};
        target->frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
    }
    m_width = width;
    m_height = height;
}

SsaoData SsaoRenderer::apply(
    Miren::TextureHandle depth, uint32_t width, uint32_t height, const glm::mat4 &projection,
    const glm::mat4 &viewProj, const SsaoSettings &settings, FullscreenQuad &quad
) {
    PANDA_PROFILE_SCOPE_N("Ssao::apply");
    // A perspective projection has -1 in its third row of the fourth column; the horizon search
    // is written for it.
    const bool perspective = std::abs(projection[2][3] + 1.f) < 1.e-3f;
    if (!settings.enabled || settings.intensity <= 0.f || settings.radius <= 0.f || !depth.isValid() || width < 4 ||
        height < 4 || !perspective) {
        return SsaoData{};
    }
    if (!m_program.isValid()) {
        m_program = Miren::createProgram(
            AssetImporterBase::loadShaderProgram("default-shaders/postprocess.vert", "default-shaders/ssao.frag")
        );
        m_blurProgram = Miren::createProgram(
            AssetImporterBase::loadShaderProgram("default-shaders/postprocess.vert", "default-shaders/ssao_blur.frag")
        );
    }
    const uint32_t scale = settings.fullResolution ? 1 : 2;
    const uint32_t targetWidth = std::max(1u, width / scale);
    const uint32_t targetHeight = std::max(1u, height / scale);
    ensureTargets(targetWidth, targetHeight);

    // The clip planes from the projection, in doubles: the shader turns a depth into a distance
    // with their reciprocals.
    const double p22 = projection[2][2];
    const double p32 = projection[3][2];
    const bool homogeneous = Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth;
    const double nearPlane = homogeneous ? p32 / (p22 - 1.0) : p32 / p22;
    const double farPlane = p32 / (p22 + 1.0);
    if (!(nearPlane > 0.0) || !(farPlane > nearPlane)) { return SsaoData{}; }
    PassUbo block{};
    block.projection = glm::vec4(
        projection[0][0], projection[1][1], Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth ? 1.f : 0.f,
        Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft ? 1.f : 0.f
    );
    block.params = glm::vec4(
        settings.radius, settings.intensity, static_cast<float>(1.0 / nearPlane), static_cast<float>(1.0 / farPlane)
    );
    block.size = glm::vec4(
        static_cast<float>(width), static_cast<float>(height), static_cast<float>(targetWidth),
        static_cast<float>(targetHeight)
    );
    block.budget = glm::vec4(
        static_cast<float>(std::clamp(settings.slices, 1, SsaoSettings::MAX_SLICES)),
        static_cast<float>(std::clamp(settings.steps, 1, SsaoSettings::MAX_STEPS)), static_cast<float>(scale), 0.f
    );

    const Miren::Rect viewport(0, 0, targetWidth, targetHeight);
    Miren::setViewport(Views::SSAO_VIEW, viewport);
    Miren::setViewClear(Views::SSAO_VIEW, 0xffffffff);
    Miren::setViewFrameBuffer(Views::SSAO_VIEW, m_raw.frameBuffer);
    Miren::setState(0);
    Miren::addInputUniformBuffer("SSAO_PASS", &block, sizeof(block));
    Miren::addInputTexture("depthTexture", depth);
    quad.submit(Views::SSAO_VIEW, m_program);

    Miren::setViewport(Views::SSAO_BLUR_VIEW, viewport);
    Miren::setViewClear(Views::SSAO_BLUR_VIEW, 0xffffffff);
    Miren::setViewFrameBuffer(Views::SSAO_BLUR_VIEW, m_result.frameBuffer);
    Miren::setState(0);
    Miren::addInputUniformBuffer("SSAO_PASS", &block, sizeof(block));
    Miren::addInputTexture("inputTexture", m_raw.color);
    quad.submit(Views::SSAO_BLUR_VIEW, m_blurProgram);

    SsaoData data;
    data.map = m_result.color;
    data.viewProj = viewProj;
    data.intensity = settings.intensity;
    return data;
}

} // namespace Panda
