#include "PandaUI/Elements/Button.hpp"

#include "PandaUI/Core/Theme.hpp"

#include <array>
#include <memory>
#include <utility>

namespace PandaUI {

namespace {

    // The presets of the editor (the design): the plate per state and the title color.
    ButtonAppearance makeDefaultAppearance(ButtonStyle style) {
        const Color transparent(0.f, 0.f, 0.f, 0.f);
        ButtonAppearance a;
        switch (style) {
            case ButtonStyle::Accent:
                a.normal = {SurfaceStyle(ThemeColor::accent), ThemeColor::onAccent};
                a.hovered = {SurfaceStyle(ThemeColor::accentHover), ThemeColor::onAccent};
                a.pressed = {SurfaceStyle(ThemeColor::accentPressed), ThemeColor::onAccent};
                a.focused = {SurfaceStyle(ThemeColor::accent), ThemeColor::onAccent};
                a.disabled = {SurfaceStyle(ThemeColor::controlPressed), ThemeColor::onAccent};
                break;
            case ButtonStyle::AccentOutline:
                // The backing is constant; the feedback goes through the border.
                a.normal = {SurfaceStyle(ThemeColor::accentMutedSurface, ThemeColor::accentMutedBorder), ThemeColor::accentText};
                a.hovered = {SurfaceStyle(ThemeColor::accentMutedSurface, ThemeColor::accent), ThemeColor::accentText};
                a.pressed = a.hovered;
                a.focused = a.hovered;
                a.disabled = {SurfaceStyle(ThemeColor::controlPressed, ThemeColor::border), ThemeColor::accentText};
                break;
            case ButtonStyle::Link:
                // Flat: no plate at all, the feedback is the text color.
                a.normal = {SurfaceStyle(transparent), ThemeColor::accent};
                a.hovered = {SurfaceStyle(transparent), ThemeColor::accentHover};
                a.pressed = a.hovered;
                a.focused = a.hovered;
                a.disabled = {SurfaceStyle(transparent), ThemeColor::textMuted};
                break;
            case ButtonStyle::Destructive:
                a.normal = {SurfaceStyle(ThemeColor::danger), ThemeColor::onAccent};
                a.hovered = {SurfaceStyle(ThemeColor::danger.withShade(0.18f)), ThemeColor::onAccent};
                a.pressed = {SurfaceStyle(ThemeColor::danger.withShade(-0.18f)), ThemeColor::onAccent};
                a.focused = {SurfaceStyle(ThemeColor::danger), ThemeColor::onAccent};
                a.disabled = {SurfaceStyle(ThemeColor::controlPressed), ThemeColor::onAccent};
                break;
            case ButtonStyle::Neutral:
            default:
                a.normal = {SurfaceStyle(ThemeColor::control, ThemeColor::border), ThemeColor::textPrimary};
                a.hovered = {SurfaceStyle(ThemeColor::controlHover, ThemeColor::borderStrong), ThemeColor::textPrimary};
                a.pressed = {SurfaceStyle(ThemeColor::controlPressed, ThemeColor::borderStrong), ThemeColor::textPrimary};
                a.focused = {SurfaceStyle(ThemeColor::control, ThemeColor::borderStrong), ThemeColor::textPrimary};
                a.disabled = {SurfaceStyle(ThemeColor::controlPressed, ThemeColor::border), ThemeColor::textPrimary};
                break;
        }
        return a;
    }

} // namespace

ButtonAppearance &Button::defaultAppearance(ButtonStyle style) {
    static std::array<ButtonAppearance, 5> appearances = {
        makeDefaultAppearance(ButtonStyle::Neutral),       makeDefaultAppearance(ButtonStyle::Accent),
        makeDefaultAppearance(ButtonStyle::AccentOutline), makeDefaultAppearance(ButtonStyle::Link),
        makeDefaultAppearance(ButtonStyle::Destructive),
    };
    const size_t index = static_cast<size_t>(style);
    return appearances[index < appearances.size() ? index : 0];
}

ButtonMetrics &Button::defaultMetrics(ButtonSize size) {
    // The heights of GPUI's buttons: 28 for the regular one (Medium), 22 for the small one (Default).
    static std::array<ButtonMetrics, 2> metrics = {
        ButtonMetrics{16.f, EdgeInsets(5.f, Spacing::lg, Spacing::lg, 5.f), 28.f},
        ButtonMetrics{14.f, EdgeInsets(Spacing::xs, Spacing::md, Spacing::md, Spacing::xs), 22.f},
    };
    return metrics[size == ButtonSize::Small ? 1 : 0];
}

Button::Button()
    : Button("") {}

Button::Button(std::string text)
    : m_titleLabel(std::make_shared<Label>(std::move(text)))
    , m_onClick(nullptr)
    , m_style(ButtonStyle::Neutral)
    , m_size(ButtonSize::Regular)
    , m_appearance(defaultAppearance(ButtonStyle::Neutral)) {
    setCursor(Cursor::PointingHand);
    // No shadow: by design only floating layers (popovers/dialogs) have shadows; buttons are flat.
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setJustifyContent(Justify::Center);

    m_titleLabel->setNumberOfLines(1);
    m_titleLabel->setUserInteractionEnabled(false);
    m_titleLabel->layout().setFlexShrink(1.f);
    addSubview(m_titleLabel);

    applyMetrics();
    applyAppearance();
}

void Button::setAppearance(const ButtonAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

void Button::setStyle(ButtonStyle style) {
    if (m_style == style) { return; }
    m_style = style;
    m_appearance = defaultAppearance(style);
    applyMetrics();
    applyAppearance();
}

void Button::setSize(ButtonSize size) {
    if (m_size == size) { return; }
    m_size = size;
    applyMetrics();
    applyAppearance();
}

void Button::applyMetrics() {
    const ButtonMetrics &metrics = defaultMetrics(m_size);
    m_appearance.font = m_appearance.font.withSize(metrics.fontSize);
    m_appearance.padding = metrics.padding;
    m_appearance.minHeight = metrics.minHeight;
}

void Button::applyAppearance() {
    m_titleLabel->setFont(m_appearance.font);
    m_titleLabel->setTextShadow(m_appearance.titleShadow);
    m_titleLabel->setTextOutline(m_appearance.titleOutline);
    layout().setPadding(Edge::Top, m_appearance.padding.top);
    layout().setPadding(Edge::Left, m_appearance.padding.left);
    layout().setPadding(Edge::Right, m_appearance.padding.right);
    layout().setPadding(Edge::Bottom, m_appearance.padding.bottom);
    layout().setMinHeight(Length::points(m_appearance.minHeight));
    surface().setCornerRadii(m_appearance.cornerRadius);
    surface().setBorderWidth(m_appearance.borderWidth);
    updateAppearance();
}

void Button::setText(std::string text) {
    m_titleLabel->setText(std::move(text));
}

void Button::setOnClick(ClickCallback callback) {
    m_onClick = std::move(callback);
}

void Button::setFont(Font font) {
    m_appearance.font = font;
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
    updateAppearance(); // the tokens resolve at draw; the plate of the state is set again anyway
}

const ButtonAppearance::State &Button::currentState() const {
    if (hasControlState(ControlState::Disabled)) { return m_appearance.disabled; }
    if (hasControlState(ControlState::Highlighted)) { return m_appearance.pressed; }
    if (hasControlState(ControlState::Hovered)) { return m_appearance.hovered; }
    if (hasControlState(ControlState::Focused)) { return m_appearance.focused; }
    return m_appearance.normal;
}

void Button::updateAppearance() {
    const ButtonAppearance::State &state = currentState();
    state.surface.applyTo(surface());
    m_titleLabel->setTextColor(state.text);
}

} // namespace PandaUI
