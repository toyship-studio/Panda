#include "Panda/Renderer/BloomRenderer.hpp"
#include "Panda/Renderer/MirenViewDistribution.hpp"
#include "Panda/Renderer/Std140Buffer.hpp"
#include "Panda/Assets/StaticResources.hpp"

#include <Foundation/Profile.hpp>

#include <Foundation/GraphicsAPI.hpp>

#include <algorithm>

namespace Panda {

BloomRenderer::BloomRenderer() {}

BloomRenderer::~BloomRenderer() {
    auto destroy = [](Target &target) {
        if (!target.created) { return; }
        Miren::deleteFrameBuffer(target.frameBuffer);
        Miren::deleteTexture(target.color);
    };
    for (Target &target : m_down) {
        destroy(target);
    }
    for (Target &target : m_up) {
        destroy(target);
    }
    destroy(m_output);
}


void BloomRenderer::ensureTarget(Target &target, uint32_t width, uint32_t height) {
    if (target.created && target.width == width && target.height == height) { return; }
    if (target.created) { // resize: like the viewport — resizeTexture + FBO recreation
        Miren::resizeTexture(target.color, width, height);
        Miren::deleteFrameBuffer(target.frameBuffer);
        target.frameBuffer = Miren::createFrameBuffer(target.specification);
    } else {
        Miren::TextureCreate create;
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        create.m_format = Miren::TextureFormat::RGBA16F;
        create.m_width = width;
        create.m_height = height;
        target.color = Miren::createTexture(create);
        Miren::FrameBufferAttachment attachments[] = {target.color};
        target.specification = Miren::FrameBufferSpecification(attachments, 1);
        target.frameBuffer = Miren::createFrameBuffer(target.specification);
        target.created = true;
    }
    target.width = width;
    target.height = height;
}


Miren::TextureHandle BloomRenderer::apply(
    Miren::TextureHandle sceneColor, Miren::TextureHandle sceneEmissive, uint32_t width, uint32_t height,
    const BloomSettings &settings, FullscreenQuad &quad
) {
    PANDA_PROFILE_SCOPE_N("Bloom::apply");
    Miren::TextureHandle emissive = sceneEmissive;
    if (!settings.enabled || width == 0 || height == 0 || !emissive.isValid()) { return sceneColor; }
    if (!StaticResources::bloomDownsampleShader || !StaticResources::bloomUpsampleAddShader ||
        !StaticResources::bloomApplyShader) {
        return sceneColor;
    }
    Miren::ProgramHandle downShader = StaticResources::bloomDownsampleShader->getMirenHandle();
    Miren::ProgramHandle upShader = StaticResources::bloomUpsampleAddShader->getMirenHandle();
    Miren::ProgramHandle applyShader = StaticResources::bloomApplyShader->getMirenHandle();
    if (!downShader.isValid() || !upShader.isValid() || !applyShader.isValid()) { return sceneColor; }

    // How many levels fit: each one is half the size, the bottom is no smaller than ~8px (at
    // least one).
    uint32_t levelWidth[MAX_LEVELS];
    uint32_t levelHeight[MAX_LEVELS];
    int levels = 0;
    uint32_t w = width;
    uint32_t h = height;
    for (int i = 0; i < MAX_LEVELS; ++i) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        if (levels > 0 && std::min(w, h) < 8) { break; }
        levelWidth[i] = w;
        levelHeight[i] = h;
        ++levels;
    }

    int step = 0;
    auto beginPass = [&](Target &target, uint32_t passWidth, uint32_t passHeight) {
        const Miren::ViewId viewId = Views::bloomView(step++);
        ensureTarget(target, passWidth, passHeight);
        Miren::setViewport(viewId, Miren::Rect(0, 0, passWidth, passHeight));
        Miren::setViewClear(viewId, 0x000000ff);
        Miren::setViewFrameBuffer(viewId, target.frameBuffer);
        Miren::setState(0);
        return viewId;
    };
    auto bloomData = [&](float sourceWidth, float sourceHeight) {
        Std140Buffer ubo;
        ubo.addVec4(sourceWidth, sourceHeight, 0.f, 0.f);
        ubo.addVec4(settings.radius, settings.intensity, 1.f / (float)levels, 0.f);
        return ubo;
    };

    // Downsampling: emissive → ½ → ¼ → ...
    for (int i = 0; i < levels; ++i) {
        const Miren::ViewId viewId = beginPass(m_down[i], levelWidth[i], levelHeight[i]);
        Miren::TextureHandle source = (i == 0) ? emissive : m_down[i - 1].color;
        const float sourceWidth = (i == 0) ? (float)width : (float)levelWidth[i - 1];
        const float sourceHeight = (i == 0) ? (float)height : (float)levelHeight[i - 1];
        Miren::addInputTexture("sourceTexture", source);
        Std140Buffer ubo = bloomData(sourceWidth, sourceHeight);
        Miren::addInputUniformBuffer("BLOOM_DATA", ubo.getData(), ubo.getSize());
        quad.submit(viewId, downShader);
    }

    // Upsampling with accumulation: up[i] = down[i] + tent(lower level).
    Miren::TextureHandle current = m_down[levels - 1].color;
    for (int i = levels - 2; i >= 0; --i) {
        const Miren::ViewId viewId = beginPass(m_up[i], levelWidth[i], levelHeight[i]);
        Miren::addInputTexture("sourceTexture", current);
        Miren::addInputTexture("levelTexture", m_down[i].color);
        Std140Buffer ubo = bloomData((float)levelWidth[i + 1], (float)levelHeight[i + 1]);
        Miren::addInputUniformBuffer("BLOOM_DATA", ubo.getData(), ubo.getSize());
        quad.submit(viewId, upShader);
        current = m_up[i].color;
    }

    // Apply: scene color + glow with intensity (the normalization by the number of octaves is in
    // params.z).
    const Miren::ViewId viewId = beginPass(m_output, width, height);
    Miren::addInputTexture("colorTexture", sceneColor);
    Miren::addInputTexture("bloomTexture", current);
    Std140Buffer ubo = bloomData((float)levelWidth[0], (float)levelHeight[0]);
    Miren::addInputUniformBuffer("BLOOM_DATA", ubo.getData(), ubo.getSize());
    quad.submit(viewId, applyShader);
    return m_output.color;
}

} // namespace Panda
