#include "PandaUI/Elements/Color/ColorSwatch.hpp"

#include "PandaUI/Core/Theme.hpp"

namespace PandaUI {

namespace {
    constexpr float DEFAULT_SIZE = 28.f;
}

ColorSwatch::ColorSwatch()
    : ColorSwatch(Color(0xFFFFFFFF)) {}

ColorSwatch::ColorSwatch(Color color)
    : m_color(color) {
    layout().setSize({DEFAULT_SIZE, DEFAULT_SIZE});
    surface().setCornerRadius(6.f);
    surface().setBorderWidth(1.f);
    apply();
}

void ColorSwatch::setColor(Color color) {
    m_color = color;
    apply();
}

void ColorSwatch::themeChanged() {
    apply();
}

void ColorSwatch::apply() {
    setBackgroundColor(m_color);
    surface().setBorderColor(theme().border);
}

} // namespace PandaUI
