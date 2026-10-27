#include "PandaUI/Rendering/Surface.hpp"

#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Core/View.hpp"

#include <algorithm>

namespace PandaUI {

namespace {

    Color withOpacity(Color color, float opacity) {
        color.a *= opacity;
        return color;
    }

} // namespace

Surface::Surface(View *owner)
    : m_owner(owner)
    // Transparent default: a regular View/Panel is a layout container, not a "plate".
    // An opaque white forced every wrapper to reset the background manually.
    , m_backgroundColor(Color(0.f, 0.f, 0.f, 0.f))
    , m_backgroundGradient()
    , m_backgroundImage()
    , m_opacity(1.f)
    , m_clipsToBounds(false)
    , m_cornerRadii()
    , m_borderWidth(0.f)
    , m_borderColor(Color(0.f, 0.f, 0.f, 0.f))
    , m_shadow() {}

void Surface::setOwner(View *owner) {
    m_owner = owner;
}

void Surface::setBackgroundColor(ThemeColor color) {
    m_backgroundColor = color;
    invalidate();
}

void Surface::setBackgroundGradient(SurfaceGradient gradient) {
    m_backgroundGradient = gradient;
    invalidate();
}

void Surface::clearBackgroundGradient() {
    if (!m_backgroundGradient) { return; }
    m_backgroundGradient.reset();
    invalidate();
}

void Surface::setBackgroundImage(SurfaceImage image) {
    m_backgroundImage = image;
    invalidate();
}

void Surface::setBackgroundImage(TextureHandle texture, EdgeInsets capInsets, ThemeColor tint) {
    setBackgroundImage(SurfaceImage(texture, capInsets, tint));
}

void Surface::clearBackgroundImage() {
    if (!m_backgroundImage) { return; }
    m_backgroundImage = SurfaceImage();
    invalidate();
}

void Surface::setOpacity(float opacity) {
    const float nextOpacity = std::clamp(opacity, 0.f, 1.f);
    if (m_opacity == nextOpacity) { return; }
    m_opacity = nextOpacity;
    invalidate();
}

void Surface::setClipsToBounds(bool clipsToBounds) {
    if (m_clipsToBounds == clipsToBounds) { return; }
    m_clipsToBounds = clipsToBounds;
    invalidate();
}

void Surface::setCornerRadius(float cornerRadius) {
    setCornerRadii(CornerRadii(std::max(0.f, cornerRadius)));
}

void Surface::setCornerRadii(CornerRadii radii) {
    radii.topLeft = std::max(0.f, radii.topLeft);
    radii.topRight = std::max(0.f, radii.topRight);
    radii.bottomRight = std::max(0.f, radii.bottomRight);
    radii.bottomLeft = std::max(0.f, radii.bottomLeft);
    if (m_cornerRadii == radii) { return; }
    m_cornerRadii = radii;
    invalidate();
}

void Surface::setBorderWidth(float borderWidth) {
    const float nextBorderWidth = std::max(0.f, borderWidth);
    if (m_borderWidth == nextBorderWidth) { return; }
    m_borderWidth = nextBorderWidth;
    invalidate();
}

void Surface::setBorderColor(ThemeColor color) {
    m_borderColor = color;
    invalidate();
}

void Surface::setShadow(const ShadowStyle &shadow) {
    m_shadow.color = shadow.color;
    m_shadow.offset = shadow.offset;
    m_shadow.radius = shadow.radius;
    m_shadow.spread = shadow.spread;
    invalidate();
}

void Surface::setShadowColor(Color color) {
    m_shadow.color = color;
    invalidate();
}

void Surface::setShadowOffset(Point offset) {
    if (m_shadow.offset.x == offset.x && m_shadow.offset.y == offset.y) { return; }
    m_shadow.offset = offset;
    invalidate();
}

void Surface::setShadowRadius(float radius) {
    const float nextRadius = std::max(0.f, radius);
    if (m_shadow.radius == nextRadius) { return; }
    m_shadow.radius = nextRadius;
    invalidate();
}

void Surface::setShadowSpread(float spread) {
    const float nextSpread = std::max(0.f, spread);
    if (m_shadow.spread == nextSpread) { return; }
    m_shadow.spread = nextSpread;
    invalidate();
}

void Surface::draw(UIRenderer &renderer, Rect frame, const glm::mat4 &transform) const {
    UIRenderer::RectData rect;
    rect.center = glm::vec3(frame.origin.x + frame.size.width * 0.5f, frame.origin.y + frame.size.height * 0.5f, 0.f);
    rect.size = frame.size;
    rect.transform = transform;

    if (m_shadow.isVisible()) {
        UIRenderer::ShadowData shadow;
        shadow.color = withOpacity(m_shadow.color, m_opacity);
        shadow.offset = m_shadow.offset;
        shadow.radius = m_shadow.radius;
        shadow.spread = m_shadow.spread;
        renderer.drawShadow(rect, m_cornerRadii, shadow);
    }

    // The fill: a gradient while one is set, the color otherwise (the theme of the moment).
    if (m_backgroundGradient) {
        const Color start = withOpacity(m_backgroundGradient->start.resolve(), m_opacity);
        const Color end = withOpacity(m_backgroundGradient->end.resolve(), m_opacity);
        if (start.a > 0.f || end.a > 0.f) {
            const auto mix = [](Color a, Color b) {
                return Color((a.r + b.r) * 0.5f, (a.g + b.g) * 0.5f, (a.b + b.b) * 0.5f, (a.a + b.a) * 0.5f);
            };
            UIRenderer::CornerColors colors;
            switch (m_backgroundGradient->direction) {
                case GradientDirection::TopToBottom:
                    colors = {start, start, end, end};
                    break;
                case GradientDirection::LeftToRight:
                    colors = {start, end, end, start};
                    break;
                case GradientDirection::TopLeftToBottomRight:
                    colors = {start, mix(start, end), end, mix(start, end)};
                    break;
                case GradientDirection::TopRightToBottomLeft:
                    colors = {mix(start, end), start, mix(start, end), end};
                    break;
            }
            renderer.drawRoundedRect(rect, m_cornerRadii, colors);
        }
    } else {
        const Color background = m_backgroundColor.resolve();
        if (background.a > 0.f) {
            rect.color = withOpacity(background, m_opacity);
            renderer.drawRoundedRect(rect, m_cornerRadii);
        }
    }

    // The image on top of the fill: a frame, a texture, a skin. Its own shape, not the corner radius.
    if (m_backgroundImage) {
        const Color tint = withOpacity(m_backgroundImage.tint.resolve(), m_opacity);
        if (tint.a > 0.f) {
            UIRenderer::RectData image = rect;
            image.color = tint;
            image.texture = m_backgroundImage.texture;
            image.textureCoords = m_backgroundImage.textureCoords;
            renderer.drawNineSlice(image, m_backgroundImage.capInsets, m_backgroundImage.scale);
        }
    }

    const Color border = m_borderColor.resolve();
    if (m_borderWidth > 0.f && border.a > 0.f) {
        rect.color = withOpacity(border, m_opacity);
        rect.texture = TextureHandle();
        rect.textureCoords = Rect(0.f, 0.f, 1.f, 1.f);
        renderer.drawRoundedRectBorder(rect, m_cornerRadii, m_borderWidth);
    }
}

void Surface::invalidate() {
    if (m_owner) { m_owner->setNeedsDisplay(); }
}

} // namespace PandaUI
