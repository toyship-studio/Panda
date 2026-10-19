#include "PandaUI/Elements/Button.hpp"

#include "PandaUI/Core/Theme.hpp"

#include <memory>
#include <utility>

namespace PandaUI {

Button::Button()
    : Button("") {}

Button::Button(std::string text)
    : m_titleLabel(std::make_shared<Label>(std::move(text)))
    , m_onClick(nullptr)
    , m_style(ButtonStyle::Neutral)
    , m_size(ButtonSize::Regular)
    , m_normalColor(theme().control)
    , m_hoveredColor(theme().controlHover)
    , m_pressedColor(theme().controlPressed)
    , m_disabledColor(theme().controlPressed) {
    setCursor(Cursor::PointingHand);
    surface().setCornerRadius(7.f);
    surface().setBorderWidth(1.f);
    // No shadow: by design only floating layers (popovers/dialogs) have shadows; buttons are flat.
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setJustifyContent(Justify::Center);

    m_titleLabel->setNumberOfLines(1);
    m_titleLabel->setUserInteractionEnabled(false);
    m_titleLabel->layout().setFlexShrink(1.f);
    addSubview(m_titleLabel);
    applySize();

    applyThemeColors();
    updateAppearance();
}

void Button::setStyle(ButtonStyle style) {
    if (m_style == style) { return; }
    m_style = style;
    applyThemeColors();
    updateAppearance();
}

void Button::setSize(ButtonSize size) {
    if (m_size == size) { return; }
    m_size = size;
    applySize();
}

void Button::applySize() {
    const bool small = m_size == ButtonSize::Small;
    m_titleLabel->setFont(m_titleLabel->getFont().withSize(small ? 14.f : 16.f));
    layout().setPadding(Edge::Horizontal, small ? 12.f : 16.f);
    layout().setPadding(Edge::Vertical, small ? 3.f : 9.f);
    layout().setMinHeight(Length::points(small ? 24.f : 0.f));
    surface().setCornerRadius(small ? 6.f : 7.f);
}

void Button::setText(std::string text) {
    m_titleLabel->setText(std::move(text));
}

void Button::setOnClick(ClickCallback callback) {
    m_onClick = std::move(callback);
}

void Button::setFont(Font font) {
    m_titleLabel->setFont(font);
}

void Button::setNumberOfLines(int numberOfLines) {
    m_titleLabel->setNumberOfLines(numberOfLines);
}

void Button::performPrimaryAction() {
    Control::performPrimaryAction();
    if (m_onClick) { m_onClick(*this); }
}

void Button::controlStateChanged(ControlState, ControlState) {
    updateAppearance();
}

void Button::themeChanged() {
    applyThemeColors();
    updateAppearance();
}

void Button::applyThemeColors() {
    const Theme &t = theme();
    if (m_style == ButtonStyle::Accent) {
        m_normalColor = t.accent;
        m_hoveredColor = t.accentHover;
        m_pressedColor = t.accentPressed;
        m_titleLabel->setTextColor(t.onAccent);
    } else if (m_style == ButtonStyle::AccentOutline) {
        m_normalColor = t.accentMutedSurface; // the backing is constant; feedback goes through the border
        m_hoveredColor = t.accentMutedSurface;
        m_pressedColor = t.accentMutedSurface;
        m_titleLabel->setTextColor(t.accentText);
    } else if (m_style == ButtonStyle::Destructive) {
        const auto shade = [](Color color, float target, float amount) {
            return Color(
                color.r + (target - color.r) * amount, color.g + (target - color.g) * amount,
                color.b + (target - color.b) * amount, color.a
            );
        };
        m_normalColor = t.danger;
        m_hoveredColor = shade(t.danger, 1.f, 0.18f);
        m_pressedColor = shade(t.danger, 0.f, 0.18f);
        m_titleLabel->setTextColor(t.onAccent);
    } else if (m_style == ButtonStyle::Link) {
        // Flat link: the background is always transparent, feedback is given by the text color
        // (updateAppearance by state).
        const Color transparent = Color(0x00000000);
        m_normalColor = transparent;
        m_hoveredColor = transparent;
        m_pressedColor = transparent;
        m_titleLabel->setTextColor(t.accent);
    } else {
        m_normalColor = t.control;
        m_hoveredColor = t.controlHover;
        m_pressedColor = t.controlPressed;
        m_titleLabel->setTextColor(t.textPrimary);
    }
    m_disabledColor = m_style == ButtonStyle::Link ? Color(0x00000000) : t.controlPressed;
}

void Button::updateAppearance() {
    const Theme &t = theme();
    // No shadow (flat, see ctor). The background depends on the state; the border depends on the style.
    const Color transparent = Color(0x00000000);
    if (hasControlState(ControlState::Disabled)) {
        setBackgroundColor(m_disabledColor);
    } else if (hasControlState(ControlState::Highlighted)) {
        setBackgroundColor(m_pressedColor);
    } else if (hasControlState(ControlState::Hovered)) {
        setBackgroundColor(m_hoveredColor);
    } else {
        setBackgroundColor(m_normalColor);
    }

    const bool hot = hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted) ||
                     hasControlState(ControlState::Focused);
    Color border = transparent;
    switch (m_style) {
        case ButtonStyle::Accent:
        case ButtonStyle::Destructive:
            border = transparent; // solid fill — no border
            break;
        case ButtonStyle::AccentOutline:
            border = hasControlState(ControlState::Disabled) ? t.border : (hot ? t.accent : t.accentMutedBorder);
            break;
        case ButtonStyle::Link:
            border = transparent; // the link is flat; feedback is the text color
            m_titleLabel->setTextColor(
                hasControlState(ControlState::Disabled) ? t.textMuted
                                                        : (hot ? t.accentHover : t.accent)
            );
            break;
        case ButtonStyle::Neutral:
        default:
            border = hot ? t.borderStrong : t.border;
            break;
    }
    surface().setBorderColor(border);
}

} // namespace PandaUI
