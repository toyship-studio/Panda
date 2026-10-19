#pragma once

#include "PandaUI/Core/View.hpp"

namespace PandaUI {

// Simple color swatch: shows the given color (background + themed border, rounded corners).
// Not interactive — the "display" variation. The caller sets the size via layout.
class ColorSwatch : public View {
public:
    ColorSwatch();
    explicit ColorSwatch(Color color);

    void setColor(Color color);
    Color getColor() const {
        return m_color;
    }

private:
    void themeChanged() override;
    void apply();

    Color m_color;
};

} // namespace PandaUI
