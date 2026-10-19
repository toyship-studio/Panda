#include "Panda/Renderer/ExposureRenderer.hpp"
#include "Panda/Renderer/MirenViewDistribution.hpp"
#include "Panda/Renderer/Std140Buffer.hpp"
#include "Panda/Assets/StaticResources.hpp"

#include <Foundation/Profile.hpp>

#include <algorithm>
#include <cmath>

namespace Panda {

ExposureRenderer::ExposureRenderer() {}

ExposureRenderer::~ExposureRenderer() {
    for (Target *target : {&m_measure, &m_reduce, &m_adapted[0], &m_adapted[1], &m_output}) {
        if (!target->created) { continue; }
        Miren::deleteFrameBuffer(target->frameBuffer);
        Miren::deleteTexture(target->color);
    }
}


void ExposureRenderer::ensureTarget(Target &target, uint32_t width, uint32_t height) {
    if (target.created && target.width == width && target.height == height) { return; }
    if (target.created) {
        Miren::deleteFrameBuffer(target.frameBuffer);
        Miren::deleteTexture(target.color);
    }
    Miren::TextureCreate create;
    create.m_width = (uint16_t)width;
    create.m_height = (uint16_t)height;
    create.m_format = Miren::TextureFormat::RGBA16F;
    create.m_minFiltering = Miren::LINEAR;
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP;
    target.color = Miren::createTexture(create);
    Miren::FrameBufferAttachment attachments[] = {target.color};
    target.specification = Miren::FrameBufferSpecification(attachments, 1);
    target.frameBuffer = Miren::createFrameBuffer(target.specification);
    target.width = width;
    target.height = height;
    target.created = true;
}


Miren::TextureHandle ExposureRenderer::apply(
    Miren::TextureHandle sceneColor, uint32_t width, uint32_t height,
    const ExposureSettings &settings, float time, FullscreenQuad &quad
) {
    PANDA_PROFILE_SCOPE_N("Exposure::apply");
    const float deltaTime = std::clamp(time - m_lastTime, 0.f, 0.25f);
    m_lastTime = time;
    if (!settings.enabled || width == 0 || height == 0 || !sceneColor.isValid()) {
        m_adaptedValid = false; // enabling after a pause — a snap, without creeping from stale data
        return sceneColor;
    }
    if (!StaticResources::exposureMeasureShader || !StaticResources::exposureReduceShader ||
        !StaticResources::exposureAdaptShader || !StaticResources::exposureApplyShader) {
        return sceneColor;
    }
    // 1. Measurement: the full frame → 64×64 luminances.
    ensureTarget(m_measure, 64, 64);
    Miren::ProgramHandle measureShader = StaticResources::exposureMeasureShader->getMirenHandle();
    Miren::setViewport(Views::EXPOSURE_MEASURE_VIEW, Miren::Rect(0, 0, 64, 64));
    Miren::setViewFrameBuffer(Views::EXPOSURE_MEASURE_VIEW, m_measure.frameBuffer);
    Miren::setState(0);
    Miren::addInputTexture("inputTexture", sceneColor);
    quad.submit(Views::EXPOSURE_MEASURE_VIEW, measureShader);

    // 2. Reduction 64×64 → 1×1.
    ensureTarget(m_reduce, 1, 1);
    Miren::ProgramHandle reduceShader = StaticResources::exposureReduceShader->getMirenHandle();
    Miren::setViewport(Views::EXPOSURE_REDUCE_VIEW, Miren::Rect(0, 0, 1, 1));
    Miren::setViewFrameBuffer(Views::EXPOSURE_REDUCE_VIEW, m_reduce.frameBuffer);
    Miren::setState(0);
    Miren::addInputTexture("inputTexture", m_measure.color);
    quad.submit(Views::EXPOSURE_REDUCE_VIEW, reduceShader);

    // 3. Adaptation: 1×1 ping-pong; k = 1 − exp(−dt·speed); the first frame is a snap (k = 1).
    ensureTarget(m_adapted[0], 1, 1);
    ensureTarget(m_adapted[1], 1, 1);
    const int previousIndex = m_adaptedIndex;
    const int nextIndex = 1 - m_adaptedIndex;
    m_adaptedIndex = nextIndex;
    const float k =
        m_adaptedValid ? 1.f - std::exp(-deltaTime * std::max(settings.speed, 0.f)) : 1.f;
    m_adaptedValid = true;
    Miren::ProgramHandle adaptShader = StaticResources::exposureAdaptShader->getMirenHandle();
    Miren::setViewport(Views::EXPOSURE_ADAPT_VIEW, Miren::Rect(0, 0, 1, 1));
    Miren::setViewFrameBuffer(Views::EXPOSURE_ADAPT_VIEW, m_adapted[nextIndex].frameBuffer);
    Miren::setState(0);
    Miren::addInputTexture("previousTexture", m_adapted[previousIndex].color);
    Miren::addInputTexture("currentTexture", m_reduce.color);
    Std140Buffer adaptData;
    adaptData.addVec4(k, 0.f, 0.f, 0.f);
    Miren::addInputUniformBuffer("EXPOSURE_DATA", adaptData.getData(), adaptData.getSize());
    quad.submit(Views::EXPOSURE_ADAPT_VIEW, adaptShader);

    // 4. Application: full-res × exposure.
    ensureTarget(m_output, width, height);
    Miren::ProgramHandle applyShader = StaticResources::exposureApplyShader->getMirenHandle();
    Miren::setViewport(Views::EXPOSURE_APPLY_VIEW, Miren::Rect(0, 0, width, height));
    Miren::setViewFrameBuffer(Views::EXPOSURE_APPLY_VIEW, m_output.frameBuffer);
    Miren::setState(0);
    Miren::addInputTexture("inputTexture", sceneColor);
    Miren::addInputTexture("adaptedTexture", m_adapted[nextIndex].color);
    Std140Buffer applyData;
    applyData.addVec4(settings.key, settings.minExposure, settings.maxExposure, 0.f);
    Miren::addInputUniformBuffer("EXPOSURE_DATA", applyData.getData(), applyData.getSize());
    quad.submit(Views::EXPOSURE_APPLY_VIEW, applyShader);
    return m_output.color;
}

} // namespace Panda
