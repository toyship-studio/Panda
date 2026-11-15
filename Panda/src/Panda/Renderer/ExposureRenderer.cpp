#include "Panda/Renderer/ExposureRenderer.hpp"
#include "Panda/Renderer/Std140Buffer.hpp"
#include "Panda/Assets/StaticResources.hpp"

#include <Foundation/Profile.hpp>

#include <algorithm>
#include <cmath>

namespace Panda {

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
    Miren::ProgramHandle measureShader = StaticResources::exposureMeasureShader->getMirenHandle();
    const Miren::ViewId measureView = m_measure.addView(64, 64);
    Miren::setState(0);
    Miren::addInputTexture("inputTexture", sceneColor);
    quad.submit(measureView, measureShader);

    // 2. Reduction 64×64 → 1×1.
    Miren::ProgramHandle reduceShader = StaticResources::exposureReduceShader->getMirenHandle();
    const Miren::ViewId reduceView = m_reduce.addView(1, 1);
    Miren::setState(0);
    Miren::addInputTexture("inputTexture", m_measure.color());
    quad.submit(reduceView, reduceShader);

    // 3. Adaptation: 1×1 ping-pong; k = 1 − exp(−dt·speed); the first frame is a snap (k = 1).
    const int previousIndex = m_adaptedIndex;
    const int nextIndex = 1 - m_adaptedIndex;
    m_adaptedIndex = nextIndex;
    const float k =
        m_adaptedValid ? 1.f - std::exp(-deltaTime * std::max(settings.speed, 0.f)) : 1.f;
    m_adaptedValid = true;
    Miren::ProgramHandle adaptShader = StaticResources::exposureAdaptShader->getMirenHandle();
    const Miren::ViewId adaptView = m_adapted[nextIndex].addView(1, 1);
    // The first frame snaps and does not read the previous value: the other target may not exist yet.
    Miren::TextureHandle previous = m_adapted[previousIndex].color();
    if (!previous.isValid()) { previous = m_reduce.color(); }
    Miren::setState(0);
    Miren::addInputTexture("previousTexture", previous);
    Miren::addInputTexture("currentTexture", m_reduce.color());
    Std140Buffer adaptData;
    adaptData.addVec4(k, 0.f, 0.f, 0.f);
    Miren::addInputUniformBuffer("EXPOSURE_DATA", adaptData.getData(), adaptData.getSize());
    quad.submit(adaptView, adaptShader);

    // 4. Application: full-res × exposure.
    Miren::ProgramHandle applyShader = StaticResources::exposureApplyShader->getMirenHandle();
    const Miren::ViewId applyView = m_output.addView(width, height);
    Miren::setState(0);
    Miren::addInputTexture("inputTexture", sceneColor);
    Miren::addInputTexture("adaptedTexture", m_adapted[nextIndex].color());
    Std140Buffer applyData;
    applyData.addVec4(settings.key, settings.minExposure, settings.maxExposure, 0.f);
    Miren::addInputUniformBuffer("EXPOSURE_DATA", applyData.getData(), applyData.getSize());
    quad.submit(applyView, applyShader);
    return m_output.color();
}

} // namespace Panda
