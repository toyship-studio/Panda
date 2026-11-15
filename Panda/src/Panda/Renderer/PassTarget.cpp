#include "Panda/Renderer/PassTarget.hpp"

namespace Panda {

PassTarget::~PassTarget() {
    if (!m_color.isValid()) { return; }
    Miren::deleteFrameBuffer(m_frameBuffer);
    Miren::deleteTexture(m_color);
}

Miren::ViewId PassTarget::addView(uint32_t width, uint32_t height, uint32_t clearColor) {
    if (!m_color.isValid()) {
        Miren::TextureCreate create;
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        create.m_format = Miren::TextureFormat::RGBA16F;
        create.m_width = static_cast<uint16_t>(width);
        create.m_height = static_cast<uint16_t>(height);
        m_color = Miren::createTexture(create);
    } else if (m_width != width || m_height != height) {
        // A framebuffer holds the size of its attachments: it is made again over the resized texture.
        Miren::resizeTexture(m_color, width, height);
        Miren::deleteFrameBuffer(m_frameBuffer);
        m_frameBuffer = Miren::FrameBufferHandle();
    }
    if (!m_frameBuffer.isValid()) {
        Miren::FrameBufferAttachment attachments[] = {m_color};
        m_frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
    }
    m_width = width;
    m_height = height;
    return Miren::addView({
        .frameBuffer = m_frameBuffer,
        .viewport = Miren::Rect(0, 0, static_cast<int>(width), static_cast<int>(height)),
        .clearColor = clearColor,
    });
}

} // namespace Panda
