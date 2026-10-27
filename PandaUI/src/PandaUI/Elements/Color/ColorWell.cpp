#include "PandaUI/Elements/Color/ColorWell.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Color/ColorPicker.hpp"
#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <memory>
#include <utility>

namespace PandaUI {

namespace {
    constexpr float DEFAULT_WIDTH = 40.f;
    constexpr float DEFAULT_HEIGHT = 26.f;
}

ColorWell::ColorWell()
    : ColorWell(Color(0x2E7DFFFF)) {}

ColorWell::ColorWell(Color color)
    : m_color(color)
    , m_onColorChanged(nullptr) {
    setCursor(Cursor::PointingHand);
    layout().setSize({DEFAULT_WIDTH, DEFAULT_HEIGHT});
    surface().setCornerRadius(Radius::sm);
    surface().setBorderWidth(1.f);
    apply();
}

void ColorWell::setColor(Color color) {
    m_color = color;
    apply();
}

void ColorWell::setOnColorChanged(ColorChangedCallback callback) {
    m_onColorChanged = std::move(callback);
}

void ColorWell::performPrimaryAction() {
    openPicker();
}

void ColorWell::controlStateChanged(ControlState, ControlState) {
    apply();
}

void ColorWell::themeChanged() {
    apply();
}

void ColorWell::apply() {
    const Theme &t = Theme::current();
    setBackgroundColor(m_color);
    if (hasControlState(ControlState::Focused)) {
        surface().setBorderColor(t.focusRing);
    } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted)) {
        surface().setBorderColor(t.borderStrong);
    } else {
        surface().setBorderColor(t.border);
    }
}

void ColorWell::openPicker() {
    Context *context = getContext();
    if (!context || m_picker) { return; } // already open — do not pile up pickers

    // Popover card with a shadow, with a full-fledged ColorPicker inside.
    auto card = std::make_shared<Panel>();
    applyElevation(*card, Elevation::Popover);
    card->layout().setPadding(Spacing::lg);

    auto picker = std::make_shared<ColorPicker>();
    picker->setColor(m_color);
    picker->setOnColorChanged([this](ColorPicker &sender) {
        m_color = sender.getColor();
        apply();
        if (m_onColorChanged) { m_onColorChanged(*this); }
    });
    card->addSubview(picker);

    auto popup = std::make_shared<Popup>(card, getWorldFrame());
    Popup *p = popup.get();
    m_picker = p;
    popup->setOnDismiss([this, context, p] {
        m_picker = nullptr;
        context->removeFocusScope(p);
    });
    context->addOverlay(popup);
    // Close the picker when the focus has left both the picker and the well itself (Tab, etc.).
    context->addFocusScope(p, this, [p] { p->dismiss(); });
}

bool ColorWell::keyDown(KeyEvent &event) {
    if (event.key == Key::Escape && m_picker) {
        m_picker->dismiss();
        return true;
    }
    return Control::keyDown(event); // Enter/Space → performPrimaryAction → openPicker (guarded against duplicates)
}

} // namespace PandaUI
