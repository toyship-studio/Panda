#include "Panda/Renderer/FogRenderer.hpp"

#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/Renderer/SceneBlocks.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Foundation/Profile.hpp>

#include <algorithm>
#include <cmath>

namespace Panda {

namespace {

    // The block of fog.frag.
    struct PassUbo {
        glm::mat4 inverseViewProj;
        glm::vec4 density; // x — density at the base height (1/m), y — ln 2 / heightFalloff (0 — uniform), z — base height, w — start distance
        glm::vec4 color;   // rgb — tint, a — sun scatter
        glm::vec4 params;  // x — max opacity, y — 1: the sky is fogged, z — 1: depth is -1..1, w — 1: rows go bottom-up
    };

} // namespace

FogRenderer::~FogRenderer() {
    if (m_program.isValid()) { Miren::deleteProgram(m_program); }
}

Miren::TextureHandle FogRenderer::apply(
    Miren::TextureHandle sceneColor, Miren::TextureHandle depth, uint32_t width, uint32_t height, const FogFrame &frame,
    FullscreenQuad &quad
) {
    const FogSettings &settings = frame.settings;
    if (!settings.enabled || settings.density <= 0.f || settings.maxOpacity <= 0.f || frame.scene == nullptr ||
        !sceneColor.isValid() || !depth.isValid() || width == 0 || height == 0) {
        return sceneColor;
    }
    PANDA_PROFILE_SCOPE_N("Fog::apply");
    if (!m_program.isValid()) {
        m_program = Miren::createProgram(
            AssetImporterBase::loadShaderProgram("default-shaders/postprocess.vert", "default-shaders/fog.frag")
        );
    }
    PassUbo block{};
    block.inverseViewProj = glm::inverse(frame.viewProj);
    block.density = glm::vec4(
        settings.density, settings.heightFalloff > 0.f ? 0.69314718f / settings.heightFalloff : 0.f, settings.baseHeight,
        std::max(settings.startDistance, 0.f)
    );
    block.color = glm::vec4(settings.tint.r, settings.tint.g, settings.tint.b, std::max(settings.sunScatter, 0.f));
    block.params = glm::vec4(
        std::clamp(settings.maxOpacity, 0.f, 1.f), settings.affectSky ? 1.f : 0.f,
        Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth ? 1.f : 0.f,
        Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft ? 1.f : 0.f
    );

    const Miren::ViewId view = m_target.addView(width, height);
    frame.scene->bind(view); // the camera, the sun and the environment light of the frame
    Miren::setState(0);
    Miren::addInputUniformBuffer("FOG_PASS", &block, sizeof(block));
    Miren::addInputTexture("inputTexture", sceneColor);
    Miren::addInputTexture("depthTexture", depth);
    quad.submit(view, m_program);
    return m_target.color();
}

} // namespace Panda
