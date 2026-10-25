#include "PandaUI/Elements/Combobox.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Elements/ListView.hpp"
#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <utility>

namespace PandaUI {

namespace {
    constexpr float COMBO_HEIGHT = 28.f; // = height of text fields (TextField DEFAULT_HEIGHT)
    constexpr float PADDING_LEFT = Spacing::md;
    constexpr float PADDING_RIGHT = 24.f; // room for the chevron on the right
    constexpr float CHEVRON_HALF = 4.f;
    constexpr float CHEVRON_DROP = 2.2f;
    constexpr float CHEVRON_THICKNESS = 1.6f;
    constexpr int MAX_VISIBLE_ROWS = 8;
    constexpr float CARD_PADDING = 4.f;
}

Combobox::Combobox()
    : m_label(std::make_shared<Label>("")) {
    setCursor(Cursor::PointingHand);
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setHeight(Length::points(COMBO_HEIGHT));
    layout().setPadding(Edge::Left, PADDING_LEFT);
    layout().setPadding(Edge::Right, PADDING_RIGHT);
    surface().setCornerRadius(Radius::sm);
    surface().setBorderWidth(1.f);

    m_label->setFont(Font(16.f));
    m_label->setClipsToBounds(true); // long text does not run under the chevron
    m_label->layout().setFlexGrow(1.f);
    addSubview(m_label);

    updateLabel();
    applyStyle();
}

Combobox::~Combobox() = default;

void Combobox::setItems(std::vector<std::string> items) {
    m_items = std::move(items);
    if (m_selectedIndex >= static_cast<int>(m_items.size())) {
        m_selectedIndex = m_items.empty() ? -1 : static_cast<int>(m_items.size()) - 1;
    }
    updateLabel();
}

void Combobox::addItem(std::string item) {
    m_items.push_back(std::move(item));
    updateLabel();
}

void Combobox::clearItems() {
    m_items.clear();
    m_selectedIndex = -1;
    updateLabel();
}

void Combobox::setSelectedIndex(int index) {
    const int clamped = (index >= 0 && index < static_cast<int>(m_items.size())) ? index : -1;
    if (clamped == m_selectedIndex) { return; }
    m_selectedIndex = clamped;
    updateLabel();
}

std::string Combobox::selectedText() const {
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        return m_items[m_selectedIndex];
    }
    return m_placeholder;
}

void Combobox::setPlaceholder(std::string text) {
    m_placeholder = std::move(text);
    updateLabel();
}

void Combobox::setOnSelectionChanged(SelectionChangedCallback callback) {
    m_onSelectionChanged = std::move(callback);
}

void Combobox::performPrimaryAction() {
    openDropdown();
}

void Combobox::controlStateChanged(ControlState, ControlState) {
    applyStyle();
}

void Combobox::themeChanged() {
    applyStyle();
    updateLabel();
    setNeedsDisplay();
}

void Combobox::draw(float offsetX, float offsetY) {
    Context *context = getContext();
    if (!context) { return; }
    UIRenderer &renderer = context->getRenderer();

    const Rect frame = getFrame();
    const float cx = offsetX + frame.origin.x + frame.size.width - PADDING_RIGHT * 0.5f;
    const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
    const Color color = hasControlState(ControlState::Disabled) ? theme().textDisabled : theme().textDim;

    // Chevron down ⌄
    renderer.drawLine(
        Point(cx - CHEVRON_HALF, cy - CHEVRON_DROP), Point(cx, cy + CHEVRON_DROP), CHEVRON_THICKNESS, color
    );
    renderer.drawLine(
        Point(cx, cy + CHEVRON_DROP), Point(cx + CHEVRON_HALF, cy - CHEVRON_DROP), CHEVRON_THICKNESS, color
    );
}

void Combobox::updateLabel() {
    m_label->setText(selectedText());
    m_label->setTextRole(m_selectedIndex < 0 ? TextRole::Muted : TextRole::Primary);
}

void Combobox::applyStyle() {
    const Theme &t = theme();
    Color background = t.input;   // recessed field surface (#1B1B1B)
    Color border = t.borderMed;
    if (hasControlState(ControlState::Disabled)) {
        background = t.inputDeep;
    } else if (hasControlState(ControlState::Highlighted)) {
        border = t.borderStrong;
    } else if (hasControlState(ControlState::Hovered)) {
        border = t.borderStrong;
    }
    if (hasControlState(ControlState::Focused)) { border = t.focusRing; } // focus border on top
    setBackgroundColor(background);
    surface().setBorderColor(border);
}

void Combobox::openDropdown() {
    Context *context = getContext();
    if (!context || m_items.empty() || m_dropdown) { return; } // already open — do not spawn another

    // Popover card with a shadow, with a ListView inside (single selection, the current item is highlighted).
    auto card = std::make_shared<Panel>();
    applyElevation(*card, Elevation::Popover);
    card->layout().setPadding(CARD_PADDING);

    auto list = std::make_shared<ListView>();
    list->setSelectionMode(ListSelectionMode::Single);
    list->setBackgroundColor(Color(0x00000000));
    list->setFocusable(false); // the focus stays on the combobox — it drives the arrow-key navigation
    // A click on a row commits on PRESS: the combobox blur (the focus went to null on a click on a
    // non-focusable row) tears down the popover through focusChanged→dismiss, and the row does not live
    // to see the release.
    list->setCommitsSelectionOnPointerDown(true);
    for (const auto &item : m_items) { list->addItem(item); }
    if (m_selectedIndex >= 0) { list->setSelectedIndex(m_selectedIndex); }

    const int visible = std::min(static_cast<int>(m_items.size()), MAX_VISIBLE_ROWS);
    list->layout().setWidth(Length::points(getFrame().size.width - CARD_PADDING * 2.f));
    list->layout().setHeight(Length::points(static_cast<float>(visible) * ListView::kRowHeight));
    card->addSubview(list);

    auto popup = std::make_shared<Popup>(card, getWorldFrame());
    m_dropdown = popup.get();
    m_dropdownList = list.get();
    popup->setOnDismiss([this] {
        m_dropdown = nullptr;
        m_dropdownList = nullptr;
    });
    // A click on a row — select and close (the arrow keys go through setSelectedIndex, which does not
    // call this callback).
    list->setOnSelectionChanged([this, popup = popup.get()](ListView &sender) {
        const int index = sender.selectedIndex();
        if (index >= 0) {
            setSelectedIndex(index);
            if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
        }
        popup->dismiss();
    });
    context->addOverlay(popup);
}

bool Combobox::keyDown(KeyEvent &event) {
    if (m_dropdown) { // the dropdown is open
        switch (event.key) {
            case Key::Up: moveDropdownHighlight(-1); return true;
            case Key::Down: moveDropdownHighlight(1); return true;
            case Key::Enter:
            case Key::KeypadEnter:
            case Key::Space: commitDropdown(); return true;
            case Key::Escape: m_dropdown->dismiss(); return true;
            default: return false;
        }
    }
    switch (event.key) { // the dropdown is closed
        case Key::Up: stepSelection(-1); return true;
        case Key::Down: stepSelection(1); return true;
        case Key::Enter:
        case Key::KeypadEnter:
        case Key::Space: openDropdown(); return true;
        default: return false;
    }
}

void Combobox::focusChanged(bool focused) {
    Control::focusChanged(focused);                        // control-state → applyStyle (focus border)
    if (!focused && m_dropdown) { m_dropdown->dismiss(); } // focus left the combobox → close the dropdown
}

void Combobox::moveDropdownHighlight(int delta) {
    if (!m_dropdownList || m_items.empty()) { return; }
    int current = m_dropdownList->selectedIndex();
    if (current < 0) { current = delta > 0 ? -1 : itemCount(); }
    const int next = std::clamp(current + delta, 0, itemCount() - 1);
    m_dropdownList->setSelectedIndex(next); // does not call onSelectionChanged → highlight without commit
    m_dropdownList->scrollIndexIntoView(next);
}

void Combobox::commitDropdown() {
    if (m_dropdownList) {
        const int index = m_dropdownList->selectedIndex();
        if (index >= 0 && index != m_selectedIndex) {
            setSelectedIndex(index);
            if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
        }
    }
    if (m_dropdown) { m_dropdown->dismiss(); }
}

void Combobox::stepSelection(int delta) {
    if (m_items.empty()) { return; }
    int current = m_selectedIndex;
    if (current < 0) { current = delta > 0 ? -1 : itemCount(); }
    const int next = std::clamp(current + delta, 0, itemCount() - 1);
    if (next != m_selectedIndex) {
        setSelectedIndex(next);
        if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
    }
}

} // namespace PandaUI
