#include "PandaUI/Elements/Combobox.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Elements/ListView.hpp"
#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Elements/TextField.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace PandaUI {

namespace {

    char folded(char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    bool startsWithIgnoringCase(const std::string &text, const std::string &query) {
        return query.size() <= text.size() &&
               std::equal(query.begin(), query.end(), text.begin(), [](char a, char b) { return folded(a) == folded(b); });
    }

    bool containsIgnoringCase(const std::string &text, const std::string &query) {
        return std::search(text.begin(), text.end(), query.begin(), query.end(), [](char a, char b) {
                   return folded(a) == folded(b);
               }) != text.end();
    }

} // namespace

// The card of the dropdown. The search field of a long list has the keyboard: the arrows it does
// not use come up here and walk the rows, and an Escape it lets through (its text is empty) closes.
class Combobox::DropdownCard final : public Panel {
public:
    explicit DropdownCard(Combobox *owner)
        : m_owner(owner)
        , m_alive(owner->weak_from_this()) {}

    bool keyDown(KeyEvent &event) override {
        if (m_alive.expired()) { return Panel::keyDown(event); }
        switch (event.key) {
            case Key::Up: m_owner->moveDropdownHighlight(-1); return true;
            case Key::Down: m_owner->moveDropdownHighlight(1); return true;
            case Key::Escape: m_owner->closeDropdown(); return true;
            default: return Panel::keyDown(event);
        }
    }

private:
    Combobox *m_owner;
    std::weak_ptr<View> m_alive; // the combobox may go first (a form rebuilt under the open dropdown)
};

ComboboxAppearance &Combobox::defaultAppearance() {
    static ComboboxAppearance appearance;
    return appearance;
}

Combobox::Combobox()
    : m_appearance(defaultAppearance())
    , m_label(std::make_shared<Label>("")) {
    setCursor(Cursor::PointingHand);
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);

    m_label->setClipsToBounds(true); // long text does not run under the chevron
    m_label->layout().setFlexGrow(1.f);
    addSubview(m_label);

    updateLabel();
    applyAppearance();
}

Combobox::~Combobox() {
    // A form rebuilt under an open dropdown (an undo) takes the combobox away: the dropdown goes
    // too, and its callbacks, which hold the combobox weakly, call nothing.
    if (m_dropdown) { m_dropdown->dismiss(); }
}

void Combobox::setAppearance(const ComboboxAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

void Combobox::applyAppearance() {
    const ComboboxAppearance &a = m_appearance;
    layout().setHeight(Length::points(a.height));
    layout().setPadding(Edge::Left, a.paddingLeft);
    layout().setPadding(Edge::Right, a.paddingRight);
    surface().setCornerRadii(a.cornerRadius);
    surface().setBorderWidth(a.borderWidth);
    m_label->setFont(a.font);
    applyStyle();
    setNeedsDisplay();
}

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

    const ComboboxAppearance &a = m_appearance;
    const Rect frame = getFrame();
    const float cx = offsetX + frame.origin.x + frame.size.width - a.paddingRight * 0.5f;
    const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
    const Color color = (hasControlState(ControlState::Disabled) ? a.chevronDisabled : a.chevron).resolve();

    // Chevron down ⌄
    renderer.drawLine(Point(cx - a.chevronHalf, cy - a.chevronDrop), Point(cx, cy + a.chevronDrop), a.chevronThickness, color);
    renderer.drawLine(Point(cx, cy + a.chevronDrop), Point(cx + a.chevronHalf, cy - a.chevronDrop), a.chevronThickness, color);
}

void Combobox::updateLabel() {
    m_label->setText(selectedText());
    m_label->setTextRole(m_selectedIndex < 0 ? TextRole::Muted : TextRole::Primary);
}

void Combobox::applyStyle() {
    // The recessed field plate: the border reacts, the focus border wins over the hover.
    const bool hot = hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted);
    const SurfaceStyle &style = hasControlState(ControlState::Disabled) ? m_appearance.disabled
                                : hasControlState(ControlState::Focused)  ? m_appearance.focused
                                : hot                                     ? m_appearance.hovered
                                                                          : m_appearance.normal;
    style.applyTo(surface());
}

void Combobox::openDropdown() {
    Context *context = getContext();
    if (!context || m_items.empty() || m_dropdown) { return; } // already open — do not spawn another

    // Popover card with a shadow, with a ListView inside (single selection, the current item is
    // highlighted) and, over a long list, the search field.
    const float cardPadding = m_appearance.cardPadding;
    const float width = getFrame().size.width - cardPadding * 2.f;
    auto card = std::make_shared<DropdownCard>(this);
    applyElevation(*card, Elevation::Popover);
    card->layout().setFlexDirection(FlexDirection::Column);
    card->layout().setPadding(cardPadding);

    std::shared_ptr<TextField> search;
    if (itemCount() > m_appearance.maxVisibleRows) {
        search = std::make_shared<TextField>();
        search->setPlaceholder("Search…");
        search->setClearsOnEscape(true); // the first Escape takes the text back, the next closes
        search->layout().setWidth(Length::points(width));
        search->layout().setMargin(Edge::Bottom, cardPadding);
        search->setOnTextChanged([this, alive = weak_from_this()](TextField &field) {
            if (!alive.expired()) { filterDropdown(field.getText()); }
        });
        search->setOnSubmit([this, alive = weak_from_this()](TextField &) {
            if (!alive.expired()) { commitDropdown(); }
        });
        card->addSubview(search);
    }

    auto list = std::make_shared<ListView>();
    list->setSelectionMode(ListSelectionMode::Single);
    list->setBackgroundColor(Color(0x00000000));
    list->setFocusable(false); // the focus stays on the combobox (or its search) — it drives the arrow keys
    // A click on a row commits on PRESS: the combobox blur (the focus went to null on a click on a
    // non-focusable row) tears down the popover through focusChanged→dismiss, and the row does not live
    // to see the release.
    list->setCommitsSelectionOnPointerDown(true);
    // The card keeps the size of the whole list while a search filters it: the field does not move
    // under the typing, and a card that opened above the combobox does not jump below it.
    const int visible = std::min(itemCount(), m_appearance.maxVisibleRows);
    const float listHeight = static_cast<float>(visible) * list->appearance().rowHeight;
    list->layout().setWidth(Length::points(width));
    list->layout().setHeight(Length::points(listHeight));
    auto rows = std::make_shared<Panel>();
    rows->setBackgroundColor(Color(0x00000000));
    rows->layout().setWidth(Length::points(width));
    rows->layout().setHeight(Length::points(listHeight));
    rows->addSubview(list);

    auto empty = std::make_shared<Label>("Nothing found");
    empty->setTextRole(TextRole::Dim);
    empty->setTextAlignment(TextAlignment::Center);
    empty->setUserInteractionEnabled(false);
    empty->layout().setPositionType(PositionType::Absolute); // over the empty rows, not under them
    empty->layout().setPosition(Edge::Left, Length::points(0.f));
    empty->layout().setPosition(Edge::Right, Length::points(0.f));
    empty->layout().setPosition(Edge::Top, Length::points(Spacing::md));
    rows->addSubview(empty);
    card->addSubview(rows);

    auto popup = std::make_shared<Popup>(card, getWorldFrame());
    m_dropdown = popup.get();
    m_dropdownList = list.get();
    m_dropdownSearch = search.get();
    m_dropdownEmpty = empty.get();
    // The rows stay mapped after the dismiss: a click on a row commits on press, after the blur of
    // the combobox has already closed the dropdown (focusChanged).
    popup->setOnDismiss([this, alive = weak_from_this()] {
        if (alive.expired()) { return; }
        m_dropdown = nullptr;
        m_dropdownList = nullptr;
        m_dropdownSearch = nullptr;
        m_dropdownEmpty = nullptr;
    });
    filterDropdown({});
    // A click on a row — select and close (the arrow keys go through setSelectedIndex, which does not
    // call this callback).
    list->setOnSelectionChanged([this, alive = weak_from_this()](ListView &sender) {
        if (alive.expired()) { return; }
        const int index = itemOfRow(sender.selectedIndex());
        if (index >= 0) {
            setSelectedIndex(index);
            if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
        }
        closeDropdown();
    });
    context->addOverlay(popup);
    if (search) { context->setFocusedView(search.get()); } // typing filters right away
}

void Combobox::filterDropdown(const std::string &query) {
    if (!m_dropdownList) { return; }
    // The best match first: a choice that starts with the text, then one that has it; within a rank
    // the order of the items stays.
    m_dropdownRows.clear();
    std::vector<int> containing;
    for (int index = 0; index < itemCount(); ++index) {
        const std::string &item = m_items[static_cast<size_t>(index)];
        if (query.empty() || startsWithIgnoringCase(item, query)) {
            m_dropdownRows.push_back(index);
        } else if (containsIgnoringCase(item, query)) {
            containing.push_back(index);
        }
    }
    m_dropdownRows.insert(m_dropdownRows.end(), containing.begin(), containing.end());
    m_dropdownList->clearItems();
    for (int index : m_dropdownRows) { m_dropdownList->addItem(m_items[static_cast<size_t>(index)]); }

    const int rows = static_cast<int>(m_dropdownRows.size());
    if (m_dropdownEmpty) { m_dropdownEmpty->setHidden(rows > 0); }
    // The highlight: the current choice in the whole list, the best match of a search (Enter takes it).
    int highlight = -1;
    if (query.empty()) {
        const auto it = std::find(m_dropdownRows.begin(), m_dropdownRows.end(), m_selectedIndex);
        if (it != m_dropdownRows.end()) { highlight = static_cast<int>(it - m_dropdownRows.begin()); }
    } else if (rows > 0) {
        highlight = 0;
    }
    m_dropdownList->setSelectedIndex(highlight);
    m_dropdownList->setContentOffset(Point(0.f, 0.f));
    if (highlight >= 0) { m_dropdownList->scrollIndexIntoView(highlight); }
}

int Combobox::itemOfRow(int row) const {
    return row >= 0 && row < static_cast<int>(m_dropdownRows.size()) ? m_dropdownRows[static_cast<size_t>(row)] : -1;
}

void Combobox::closeDropdown() {
    if (!m_dropdown) { return; }
    const bool searched = m_dropdownSearch != nullptr;
    m_dropdown->dismiss();
    // The keyboard comes back from the search to the combobox: the arrows go on changing the choice.
    if (searched) {
        if (Context *context = getContext()) { context->setFocusedView(this); }
    }
}

bool Combobox::keyDown(KeyEvent &event) {
    if (m_dropdown) { // the dropdown is open
        switch (event.key) {
            case Key::Up: moveDropdownHighlight(-1); return true;
            case Key::Down: moveDropdownHighlight(1); return true;
            case Key::Enter:
            case Key::KeypadEnter:
            case Key::Space: commitDropdown(); return true;
            case Key::Escape: closeDropdown(); return true;
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
    Control::focusChanged(focused); // control-state → applyStyle (focus border)
    // Focus left the combobox → close the dropdown. Not for a search: it takes the focus as the
    // dropdown opens, and a click outside closes the popover.
    if (!focused && m_dropdown && !m_dropdownSearch) { m_dropdown->dismiss(); }
}

void Combobox::moveDropdownHighlight(int delta) {
    const int rows = static_cast<int>(m_dropdownRows.size());
    if (!m_dropdownList || rows == 0) { return; }
    int current = m_dropdownList->selectedIndex();
    if (current < 0) { current = delta > 0 ? -1 : rows; }
    const int next = std::clamp(current + delta, 0, rows - 1);
    m_dropdownList->setSelectedIndex(next); // does not call onSelectionChanged → highlight without commit
    m_dropdownList->scrollIndexIntoView(next);
}

void Combobox::commitDropdown() {
    if (m_dropdownList) {
        const int index = itemOfRow(m_dropdownList->selectedIndex());
        if (index >= 0 && index != m_selectedIndex) {
            setSelectedIndex(index);
            if (m_onSelectionChanged) { m_onSelectionChanged(*this); }
        }
    }
    closeDropdown();
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
