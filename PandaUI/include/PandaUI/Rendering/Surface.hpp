#pragma once

#include "PandaUI/Core/Base.hpp"
#include "PandaUI/Core/Style.hpp"
#include "PandaUI/Core/TextureHandle.hpp"
#include "PandaUI/Core/Theme.hpp"

#include <glm/glm.hpp>

#include <optional>

namespace PandaUI {

class UIRenderer;
class View;

struct SurfaceShadow {
    Color color = Color(0.f, 0.f, 0.f, 0.f);
    Point offset;
    float radius = 0.f;
    float spread = 0.f;

    bool isVisible() const {
        return color.a > 0.f && (radius > 0.f || spread > 0.f || offset.x != 0.f || offset.y != 0.f);
    }
};

// An image on a surface: a texture, or a part of it, drawn as one stretched quad or in nine slices —
// the corners keep their size, the edges stretch along one axis, the center along both (UIKit's
// resizableImage). The caps are in texture pixels; scale is texture pixels per point (2 for a @2x
// image). The tint multiplies the image: white leaves it as it is, a theme token follows the theme.
struct SurfaceImage {
    TextureHandle texture;
    Rect textureCoords = Rect(0.f, 0.f, 1.f, 1.f); // the part of the texture (an atlas), in UV
    EdgeInsets capInsets;
    float scale = 1.f;
    ThemeColor tint = Color(1.f, 1.f, 1.f, 1.f);

    SurfaceImage() = default;
    explicit SurfaceImage(TextureHandle texture, EdgeInsets capInsets = {}, ThemeColor tint = Color(1.f, 1.f, 1.f, 1.f))
        : texture(texture)
        , capInsets(capInsets)
        , tint(tint) {}

    bool isVisible() const {
        return texture.isValid();
    }
    explicit operator bool() const {
        return isVisible();
    }
};

enum class GradientDirection : uint8_t {
    TopToBottom,
    LeftToRight,
    TopLeftToBottomRight,
    TopRightToBottomLeft,
};

// A two-color linear gradient over the shape of the surface; drawn instead of the background color.
struct SurfaceGradient {
    ThemeColor start;
    ThemeColor end;
    GradientDirection direction = GradientDirection::TopToBottom;

    SurfaceGradient() = default;
    SurfaceGradient(ThemeColor start, ThemeColor end, GradientDirection direction = GradientDirection::TopToBottom)
        : start(start)
        , end(end)
        , direction(direction) {}
};

// The visual layer of a view — what CALayer is to a UIView: the background (a color, a gradient or
// an image), the corners, the border, the shadow, the opacity and the clip. Every view has one
// (View::surface()); the widgets set theirs from their appearance, a game styles any view through
// it. Colors are ThemeColor: a theme token is resolved every time the surface draws, a literal
// stays as given.
class Surface final {
public:
    explicit Surface(View *owner = nullptr);

    /* ------------------------------ Background ------------------------------ */

    void setBackgroundColor(ThemeColor color);
    Color backgroundColor() const { // the color of the moment
        return m_backgroundColor.resolve();
    }
    const ThemeColor &backgroundThemeColor() const {
        return m_backgroundColor;
    }

    // A gradient replaces the background color while it is set.
    void setBackgroundGradient(SurfaceGradient gradient);
    void clearBackgroundGradient();
    const std::optional<SurfaceGradient> &backgroundGradient() const {
        return m_backgroundGradient;
    }

    // An image over the background color (a color under a translucent image shows through); the
    // border is drawn on top. The corner radius does not cut the image — the image has its own shape;
    // clipsToBounds with a radius rounds it like everything inside the view.
    void setBackgroundImage(SurfaceImage image);
    void setBackgroundImage(TextureHandle texture, EdgeInsets capInsets = {}, ThemeColor tint = Color(1.f, 1.f, 1.f, 1.f));
    void clearBackgroundImage();
    const SurfaceImage &backgroundImage() const {
        return m_backgroundImage;
    }

    /* ------------------------------ Shape ------------------------------ */

    void setCornerRadius(float cornerRadius); // every corner
    void setCornerRadii(CornerRadii radii);   // a tab, a segment: round on some sides only
    float cornerRadius() const {              // the common radius; the largest one when they differ
        return m_cornerRadii.isUniform() ? m_cornerRadii.topLeft : m_cornerRadii.max();
    }
    CornerRadii cornerRadii() const {
        return m_cornerRadii;
    }

    void setBorderWidth(float borderWidth); // inside the bounds, like CALayer
    float borderWidth() const {
        return m_borderWidth;
    }
    void setBorderColor(ThemeColor color);
    Color borderColor() const {
        return m_borderColor.resolve();
    }
    const ThemeColor &borderThemeColor() const {
        return m_borderColor;
    }

    void setShadow(const ShadowStyle &shadow); // one of Shadow::* (Style.hpp), or any
    void setShadowColor(Color color);
    void setShadowOffset(Point offset);
    void setShadowRadius(float radius);
    void setShadowSpread(float spread);
    const SurfaceShadow &shadow() const {
        return m_shadow;
    }

    /* ------------------------------ Compositing ------------------------------ */

    void setOpacity(float opacity);
    float opacity() const {
        return m_opacity;
    }

    // The subviews and the drawing of the view stay inside its bounds; the corner radii round the
    // clip too (masksToBounds), each corner its own.
    void setClipsToBounds(bool clipsToBounds);
    bool clipsToBounds() const {
        return m_clipsToBounds;
    }

    void draw(UIRenderer &renderer, Rect frame, const glm::mat4 &transform) const;

private:
    void setOwner(View *owner);
    void invalidate();

    View *m_owner;
    ThemeColor m_backgroundColor;
    std::optional<SurfaceGradient> m_backgroundGradient;
    SurfaceImage m_backgroundImage;
    float m_opacity;
    bool m_clipsToBounds;
    CornerRadii m_cornerRadii;
    float m_borderWidth;
    ThemeColor m_borderColor;
    SurfaceShadow m_shadow;

    friend class View;
};

} // namespace PandaUI
