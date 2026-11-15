#include "Panda/Renderer/FogRenderer.hpp"

#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/Renderer/MirenViewDistribution.hpp"
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
    if (m_color.isValid()) {
        Miren::deleteFrameBuffer(m_frameBuffer);
        Miren::deleteTexture(m_color);
    }
    if (m_program.isValid()) { Miren::deleteProgram(m_program); }
}

void FogRenderer::ensureTarget(uint32_t width, uint32_t height) {
    if (m_color.isValid() && m_width == width && m_height == height) { return; }
    if (m_color.isValid()) {
        Miren::deleteFrameBuffer(m_frameBuffer);
        Miren::deleteTexture(m_color);
    }
    Miren::TextureCreate create;
    create.m_width = width;
    create.m_height = height;
    create.m_format = Miren::TextureFormat::RGBA16F;
    create.m_minFiltering = Miren::LINEAR;
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP;
    m_color = Miren::createTexture(create);
    Miren::FrameBufferAttachment attachments[] = {m_color};
    m_frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
    m_width = width;
    m_height = height;
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
    ensureTarget(width, height);

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

    Miren::setViewport(Views::FOG_VIEW, Miren::Rect(0, 0, width, height));
    Miren::setViewClear(Views::FOG_VIEW, 0x000000ff);
    Miren::setViewFrameBuffer(Views::FOG_VIEW, m_frameBuffer);
    frame.scene->bind(Views::FOG_VIEW); // the camera, the sun and the environment light of the frame
    Miren::setState(0);
    Miren::addInputUniformBuffer("FOG_PASS", &block, sizeof(block));
    Miren::addInputTexture("inputTexture", sceneColor);
    Miren::addInputTexture("depthTexture", depth);
    quad.submit(Views::FOG_VIEW, m_program);
    return m_color;
}

} // namespace Panda
