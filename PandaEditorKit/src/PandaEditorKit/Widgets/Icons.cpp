#include "PandaEditorKit/Widgets/Icons.hpp"

#include <utility>

namespace Editor {

using namespace PandaUI;

void IconLibrary::set(const std::string &name, TextureHandle texture) {
    m_textures[name] = texture;
}

void IconLibrary::merge(const IconLibrary &other) {
    for (const auto &[name, texture] : other.m_textures) { m_textures[name] = texture; }
}

TextureHandle IconLibrary::get(const std::string &name) const {
    auto it = m_textures.find(name);
    return it == m_textures.end() ? TextureHandle{} : it->second;
}

namespace {
    IconLibrary s_icons; // the active registry (set by the application through setIcons)
}

void setIcons(IconLibrary library) {
    s_icons = std::move(library);
}

void addIcons(const IconLibrary &library) {
    s_icons.merge(library);
}

const IconLibrary &icons() {
    return s_icons;
}

std::shared_ptr<ImageView> makeIcon(const std::string &name, float size, ThemeColor color) {
    auto view = std::make_shared<ImageView>(icons().get(name));
    view->setTintColor(color);
    view->setContentMode(ImageContentMode::Fit);
    view->layout().setSize({size, size});
    view->layout().setFlexShrink(0.f);
    view->setUserInteractionEnabled(false); // the icon is decorative (clicks are caught by the container control)
    return view;
}

std::shared_ptr<ImageView> makeIcon(const std::string &name, float size) {
    return makeIcon(name, size, theme().textMuted);
}

IconButton::IconButton(const std::string &iconName, float iconSize, float buttonSize)
    : m_restColor(ThemeColor::textMuted) {
    setCursor(Cursor::PointingHand);
    setFocusable(false);
    layout().setSize({buttonSize, buttonSize});
    layout().setFlexShrink(0.f);
    layout().setJustifyContent(Justify::Center);
    layout().setAlignItems(Align::Center);
    surface().setCornerRadius(6.f);
    m_icon = makeIcon(iconName, iconSize, m_restColor);
    addSubview(m_icon);
    applyStyle();
}

void IconButton::setOnClick(ClickCallback callback) {
    m_onClick = std::move(callback);
}

void IconButton::setOnContextMenu(std::function<void()> callback) {
    m_onContextMenu = std::move(callback);
}

bool IconButton::pointerDown(PointerEvent &event) {
    if (event.button == PointerButton::Right && m_onContextMenu) {
        m_onContextMenu();
        return true; // consume the RMB, the primary action is not triggered
    }
    return Control::pointerDown(event);
}

void IconButton::setButtonSize(float width, float height) {
    layout().setSize({width, height});
}

void IconButton::setIconColor(ThemeColor rest) {
    m_restColor = rest;
    applyStyle();
}

void IconButton::setIconName(const std::string &name) {
    if (m_icon) { m_icon->setTexture(icons().get(name)); } // the tint is preserved (set separately by applyStyle)
}

void IconButton::setFixedColor(ThemeColor color) {
    m_hasFixedColor = true;
    m_fixedColor = color;
    applyStyle();
}

void IconButton::setActive(bool active) {
    if (m_active == active) { return; }
    m_active = active;
    applyStyle();
}

void IconButton::setCornerRadius(float radius) {
    surface().setCornerRadius(radius);
}

void IconButton::controlStateChanged(ControlState, ControlState) {
    applyStyle();
}

void IconButton::performPrimaryAction() {
    if (m_onClick) { m_onClick(); }
}

void IconButton::themeChanged() {
    applyStyle(); // the colors are tokens; the hover/active state picks between them
}

void IconButton::applyStyle() {
    if (hasControlState(ControlState::Disabled)) {
        setBackgroundColor(Color(0x00000000));
        if (m_icon) { m_icon->setTintColor(ThemeColor::textDisabled); }
        return;
    }
    if (m_active) { // selected tool/tab: accent backing + accent icon
        setBackgroundColor(ThemeColor::accentSoft);
        if (m_icon) { m_icon->setTintColor(ThemeColor::accentText); }
        return;
    }
    const bool hovered = hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted);
    setBackgroundColor(hovered ? ThemeColor::control : ThemeColor(Color(0x00000000)));
    const ThemeColor iconColor = m_hasFixedColor ? m_fixedColor : (hovered ? ThemeColor::textPrimary : m_restColor);
    if (m_icon) { m_icon->setTintColor(iconColor); }
}

} // namespace Editor
