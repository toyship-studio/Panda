#include "PandaUI/Elements/ImageView.hpp"

#include "PandaUI/Runtime/Context.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"

#include <algorithm>
#include <utility>

namespace PandaUI {

ImageView::ImageView()
    : m_texture()
    , m_tintColor(0xFFFFFFFF)
    , m_contentMode(ImageContentMode::Stretch) {
    setBackgroundColor(Color(0x00000000));
    setUsesIntrinsicContentSize(true);
}

ImageView::ImageView(TextureHandle texture)
    : ImageView() {
    setTexture(texture);
}

void ImageView::setTexture(TextureHandle texture) {
    // Idempotency: the setters are called every frame (the scene viewport texture) — the same texture
    // must not dirty the layout, otherwise there is a layout pass on every frame.
    if (m_texture.id == texture.id) { return; }
    m_texture = texture;
    invalidateIntrinsicContentSize();
}

void ImageView::setTintColor(Color color) {
    m_tintColor = color;
    setNeedsDisplay();
}

void ImageView::setContentMode(ImageContentMode contentMode) {
    m_contentMode = contentMode;
    setNeedsDisplay();
}

void ImageView::setFlipVertical(bool flip) {
    if (m_flipVertical == flip) { return; }
    m_flipVertical = flip;
    setNeedsDisplay();
}

Size ImageView::intrinsicContentSize(Size) const {
    return m_texture ? m_texture.size : Size();
}

void ImageView::draw(float offsetX, float offsetY) {
    Context *context = getContext();
    if (!m_texture || !context) { return; }

    const Rect frame = getFrame();
    const Size textureSize = m_texture.size;
    if (frame.size.width <= 0.f || frame.size.height <= 0.f || textureSize.width <= 0.f || textureSize.height <= 0.f) {
        return;
    }

    float drawWidth = frame.size.width;
    float drawHeight = frame.size.height;
    float u0 = 0.f;
    float u1 = 1.f;
    float v0 = 0.f;
    float v1 = 1.f;
    if (m_contentMode == ImageContentMode::Fit || m_contentMode == ImageContentMode::Fill) {
        const float scaleX = frame.size.width / static_cast<float>(textureSize.width);
        const float scaleY = frame.size.height / static_cast<float>(textureSize.height);
        const float scale =
            m_contentMode == ImageContentMode::Fit ? std::min(scaleX, scaleY) : std::max(scaleX, scaleY);
        drawWidth = static_cast<float>(textureSize.width) * scale;
        drawHeight = static_cast<float>(textureSize.height) * scale;
        if (m_contentMode == ImageContentMode::Fill) {
            const float textureRatio = static_cast<float>(textureSize.width) / static_cast<float>(textureSize.height);
            const float frameRatio = frame.size.width / frame.size.height;
            if (textureRatio > frameRatio) {
                const float visibleWidth = frameRatio / textureRatio;
                u0 = (1.f - visibleWidth) * 0.5f;
                u1 = 1.f - u0;
            } else {
                const float visibleHeight = textureRatio / frameRatio;
                v0 = (1.f - visibleHeight) * 0.5f;
                v1 = 1.f - v0;
            }
            drawWidth = frame.size.width;
            drawHeight = frame.size.height;
        }
    } else if (m_contentMode == ImageContentMode::Center) {
        drawWidth = std::min(static_cast<float>(textureSize.width), frame.size.width);
        drawHeight = std::min(static_cast<float>(textureSize.height), frame.size.height);
        if (drawWidth < static_cast<float>(textureSize.width)) {
            const float visibleWidth = drawWidth / static_cast<float>(textureSize.width);
            u0 = (1.f - visibleWidth) * 0.5f;
            u1 = 1.f - u0;
        }
        if (drawHeight < static_cast<float>(textureSize.height)) {
            const float visibleHeight = drawHeight / static_cast<float>(textureSize.height);
            v0 = (1.f - visibleHeight) * 0.5f;
            v1 = 1.f - v0;
        }
    }

    const float x = offsetX + frame.origin.x + (frame.size.width - drawWidth) * 0.5f;
    const float y = offsetY + frame.origin.y + (frame.size.height - drawHeight) * 0.5f;

    if (m_flipVertical) { std::swap(v0, v1); } // FBO render-to-texture arrives flipped in Y

    UIRenderer::RectData rect;
    rect.center = {x + drawWidth * 0.5f, y + drawHeight * 0.5f, 0.f};
    rect.size = Size(drawWidth, drawHeight);
    rect.color = m_tintColor;
    rect.texture = m_texture;
    rect.textureCoords.origin = Point(u0, v1);
    rect.textureCoords.size = Size(u1 - u0, v0 - v1);
    context->getRenderer().drawRect(rect);
}

} // namespace PandaUI
