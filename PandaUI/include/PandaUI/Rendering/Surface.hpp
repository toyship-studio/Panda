#pragma once

#include "PandaUI/Core/Base.hpp"

#include <glm/glm.hpp>

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

class Surface final {
public:
    explicit Surface(View *owner = nullptr);

    void setBackgroundColor(Color color);
    const Color &backgroundColor() const {
        return m_backgroundColor;
    }

    void setOpacity(float opacity);
    float opacity() const {
        return m_opacity;
    }

    void setClipsToBounds(bool clipsToBounds);
    bool clipsToBounds() const {
        return m_clipsToBounds;
    }

    void setCornerRadius(float cornerRadius);
    float cornerRadius() const {
        return m_cornerRadius;
    }

    void setBorderWidth(float borderWidth);
    float borderWidth() const {
        return m_borderWidth;
    }

    void setBorderColor(Color color);
    const Color &borderColor() const {
        return m_borderColor;
    }

    void setShadowColor(Color color);
    void setShadowOffset(Point offset);
    void setShadowRadius(float radius);
    void setShadowSpread(float spread);
    const SurfaceShadow &shadow() const {
        return m_shadow;
    }

    void draw(UIRenderer &renderer, Rect frame, const glm::mat4 &transform) const;

private:
    void setOwner(View *owner);
    void invalidate();

    View *m_owner;
    Color m_backgroundColor;
    float m_opacity;
    bool m_clipsToBounds;
    float m_cornerRadius;
    float m_borderWidth;
    Color m_borderColor;
    SurfaceShadow m_shadow;

    friend class View;
};

} // namespace PandaUI
