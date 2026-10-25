#include "PandaUI/Elements/Menu.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace PandaUI {

namespace {
    constexpr float MENU_WIDTH = 200.f;
    constexpr float ITEM_HEIGHT = 26.f;
    // Chevron of a submenu row (strokes as in the CollapsibleSection disclosure, rotated to point right).
    constexpr float CHEVRON_LONG = 3.6f;
    constexpr float CHEVRON_SHORT = 1.9f;
    constexpr float CHEVRON_THICKNESS = 1.6f;

    bool isViewInSubtree(View *target, View *root) {
        for (View *view = target; view; view = view->getSuperview()) {
            if (view == root) { return true; }
        }
        return false;
    }
} // namespace

// Base menu row: uniform style/highlight + activation. Not focusable — the keyboard is held by the Menu
// itself (it has the focus when shown in a Popup), and the keyboard highlight is set by applyHighlight.
class Menu::RowBase : public Control {
public:
    RowBase(Menu *owner, std::string title)
        : m_owner(owner) {
        setCursor(Cursor::PointingHand);
        setFocusable(false);
        layout().setHeight(Length::points(ITEM_HEIGHT));
        layout().setFlexDirection(FlexDirection::Row);
        layout().setAlignItems(Align::Center);
        layout().setPadding(Edge::Horizontal, Spacing::md);
        surface().setCornerRadius(Radius::sm);

        m_label = std::make_shared<Label>(std::move(title));
        m_label->setFont(Font(16.f));
        m_label->setUserInteractionEnabled(false);
        addSubview(m_label);
    }

    void setKeyboardHighlight(bool highlighted) {
        if (m_keyboardHighlight == highlighted) { return; }
        m_keyboardHighlight = highlighted;
        applyStyle();
    }
    virtual void activateRow() = 0;
    virtual bool isSubmenuRow() const {
        return false;
    }

protected:
    virtual bool isRowHighlighted() const {
        return m_keyboardHighlight || hasControlState(ControlState::Hovered) ||
               hasControlState(ControlState::Highlighted);
    }
    void applyStyle() {
        const Theme &t = theme();
        const bool highlighted = isRowHighlighted();
        // Item highlight = accent fill + white text (per the design reference for menu dropdowns).
        setBackgroundColor(highlighted ? t.accent : Color(0x00000000));
        m_label->setTextColor(highlighted ? Color(0xFFFFFFFF) : t.textPrimary);
        setNeedsDisplay();
    }
    void themeChanged() override {
        applyStyle();
    }

    Menu *m_owner;

private:
    std::shared_ptr<Label> m_label;
    bool m_keyboardHighlight = false;
};

// Regular item: click/Enter performs the action and reports the selection (the Popup closes).
class Menu::MenuItemRow final : public RowBase {
public:
    MenuItemRow(Menu *owner, std::string title, std::function<void()> action, std::function<void()> onSelected)
        : RowBase(owner, std::move(title))
        , m_action(std::move(action))
        , m_onSelected(std::move(onSelected)) {
        applyStyle();
    }

    void activateRow() override {
        if (m_action) { m_action(); }
        if (m_onSelected) { m_onSelected(); }
    }

private:
    void performPrimaryAction() override {
        activateRow();
    }
    void controlStateChanged(ControlState previousState, ControlState currentState) override {
        if (!contains(previousState, ControlState::Hovered) && contains(currentState, ControlState::Hovered)) {
            m_owner->rowHovered(this);
        }
        applyStyle();
    }

    std::function<void()> m_action;
    std::function<void()> m_onSelected;
};

// Parent row of a submenu: title + right chevron. Hovering opens the nested menu, click/→/Enter
// opens it and hands the keyboard over to the submenu; while the submenu is open, the row
// stays highlighted as a visual anchor.
class Menu::SubmenuRow final : public RowBase {
public:
    SubmenuRow(Menu *owner, std::string title)
        : RowBase(owner, std::move(title)) {
        applyStyle();
    }

    void setSubmenuOpen(bool open) {
        if (m_submenuOpen == open) { return; }
        m_submenuOpen = open;
        applyStyle();
    }
    void activateRow() override {
        m_owner->openSubmenuFor(this, true);
    }
    bool isSubmenuRow() const override {
        return true;
    }

private:
    void performPrimaryAction() override {
        m_owner->openSubmenuFor(this, true);
    }
    void controlStateChanged(ControlState previousState, ControlState currentState) override {
        if (!contains(previousState, ControlState::Hovered) && contains(currentState, ControlState::Hovered)) {
            m_owner->openSubmenuFor(this, false); // hover opens it but does not steal the keyboard
        }
        applyStyle();
    }
    bool isRowHighlighted() const override {
        return m_submenuOpen || RowBase::isRowHighlighted();
    }
    void draw(float offsetX, float offsetY) override {
        Context *context = getContext();
        if (!context) { return; }
        UIRenderer &renderer = context->getRenderer();

        const Rect frame = getFrame();
        const float cx = offsetX + frame.origin.x + frame.size.width - 12.f - CHEVRON_SHORT;
        const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
        const Color color = isRowHighlighted() ? Color(0xFFFFFFFF) : theme().textDim;
        renderer.drawLine(
            Point(cx - CHEVRON_SHORT, cy - CHEVRON_LONG), Point(cx + CHEVRON_SHORT, cy), CHEVRON_THICKNESS, color
        );
        renderer.drawLine(
            Point(cx + CHEVRON_SHORT, cy), Point(cx - CHEVRON_SHORT, cy + CHEVRON_LONG), CHEVRON_THICKNESS, color
        );
    }

    bool m_submenuOpen = false;
};

Menu::Menu()
    : m_onItemSelected(nullptr) {
    setFocusable(true); // when shown in a Popup the menu takes the keyboard (wantsFocusWhenPresented)
    layout().setFlexDirection(FlexDirection::Column);
    layout().setWidth(Length::points(MENU_WIDTH));
    layout().setGap(1.f);
    layout().setPadding(Spacing::sm);
    applyElevation(*this, Elevation::Popover);
    applyStyle();
}

void Menu::addItem(std::string title, ItemAction action) {
    auto row = std::make_shared<MenuItemRow>(this, std::move(title), std::move(action), [this] {
        if (m_onItemSelected) { m_onItemSelected(); }
    });
    m_navRows.push_back(row.get());
    addSubview(row);
}

void Menu::addSubmenu(std::string title, std::shared_ptr<Menu> submenu) {
    if (!submenu) { return; }
    auto row = std::make_shared<SubmenuRow>(this, std::move(title));
    submenu->m_parentMenu = this;
    // A selection in a submenu closes the whole chain: it is forwarded up through the parent. The root's
    // onItemSelected is read at click time — the owner is free to set it after populating the menu.
    submenu->setOnItemSelected([this] {
        if (m_onItemSelected) { m_onItemSelected(); }
    });
    m_navRows.push_back(row.get());
    m_submenus.push_back({row.get(), std::move(submenu)});
    addSubview(row);
}

void Menu::addSeparator() {
    auto separator = std::make_shared<Panel>();
    separator->setUserInteractionEnabled(false);
    separator->layout().setHeight(Length::points(1.f));
    separator->setBackgroundColor(theme().borderMed);
    addSubview(separator);
}

void Menu::setOnItemSelected(std::function<void()> callback) {
    m_onItemSelected = std::move(callback);
}

bool Menu::keyDown(KeyEvent &event) {
    switch (event.key) {
        case Key::Down: moveHighlight(1); return true;
        case Key::Up: moveHighlight(-1); return true;
        case Key::Enter:
        case Key::KeypadEnter:
        case Key::Space: activateHighlighted(); return true;
        case Key::Right:
            // On a submenu row → open the submenu and hand the keyboard over to it.
            if (m_highlightedIndex >= 0 && m_highlightedIndex < static_cast<int>(m_navRows.size()) &&
                m_navRows[m_highlightedIndex]->isSubmenuRow()) {
                m_navRows[m_highlightedIndex]->activateRow();
                return true;
            }
            return false;
        case Key::Left:
            // Submenu: close itself and return the keyboard to the parent. Escape is not handled —
            // it bubbles up to the Popup and closes the whole popup (as in native menus).
            if (m_parentMenu) {
                m_parentMenu->closeOpenSubmenu();
                return true;
            }
            return false;
        default: return false;
    }
}

void Menu::rowHovered(Control *row) {
    if (m_openSubmenuRow && row != static_cast<Control *>(m_openSubmenuRow)) { closeOpenSubmenu(); }
}

void Menu::openSubmenuFor(SubmenuRow *row, bool focusSubmenu) {
    if (m_openSubmenuRow == row) {
        if (focusSubmenu) {
            for (SubmenuEntry &entry : m_submenus) {
                if (entry.row != row) { continue; }
                if (Context *context = getContext()) { context->setFocusedView(entry.menu.get()); }
                break;
            }
        }
        return;
    }
    closeOpenSubmenu();
    Popup *popup = findAncestorPopup();
    if (!popup) { return; }
    for (SubmenuEntry &entry : m_submenus) {
        if (entry.row != row) { continue; }
        entry.menu->resetHighlight();
        popup->presentSubmenu(entry.menu, row->getWorldFrame());
        m_openSubmenuRow = row;
        row->setSubmenuOpen(true);
        if (focusSubmenu) {
            if (Context *context = getContext()) { context->setFocusedView(entry.menu.get()); }
            entry.menu->moveHighlight(1); // highlight the first item
        }
        return;
    }
}

void Menu::closeOpenSubmenu() {
    if (!m_openSubmenuRow) { return; }
    for (SubmenuEntry &entry : m_submenus) {
        if (entry.row != m_openSubmenuRow) { continue; }
        entry.menu->closeOpenSubmenu(); // recursively hide the grandchild submenus as well
        Context *context = getContext();
        const bool focusWasInside = context && isViewInSubtree(context->getFocusedView(), entry.menu.get());
        if (Popup *popup = findAncestorPopup()) { popup->hideSubmenu(entry.menu.get()); }
        entry.menu->resetHighlight();
        // The keyboard was in the submenu being closed → return the focus to this menu.
        if (focusWasInside && context) { context->setFocusedView(this); }
        break;
    }
    m_openSubmenuRow->setSubmenuOpen(false);
    m_openSubmenuRow = nullptr;
}

Popup *Menu::findAncestorPopup() {
    for (View *view = getSuperview(); view; view = view->getSuperview()) {
        if (auto *popup = dynamic_cast<Popup *>(view)) { return popup; }
    }
    return nullptr;
}

void Menu::moveHighlight(int delta) {
    if (m_navRows.empty()) { return; }
    int current = m_highlightedIndex;
    if (current < 0) { current = delta > 0 ? -1 : static_cast<int>(m_navRows.size()); } // first step → 0 or the end
    m_highlightedIndex = std::clamp(current + delta, 0, static_cast<int>(m_navRows.size()) - 1);
    // The highlight has left the row of the open submenu → the submenu closes (as in native menus).
    if (m_openSubmenuRow && m_navRows[m_highlightedIndex] != m_openSubmenuRow) { closeOpenSubmenu(); }
    applyHighlight();
}

void Menu::activateHighlighted() {
    if (m_highlightedIndex >= 0 && m_highlightedIndex < static_cast<int>(m_navRows.size())) {
        m_navRows[m_highlightedIndex]->activateRow();
    }
}

void Menu::resetHighlight() {
    m_highlightedIndex = -1;
    applyHighlight();
}

void Menu::applyHighlight() {
    for (int i = 0; i < static_cast<int>(m_navRows.size()); ++i) {
        m_navRows[i]->setKeyboardHighlight(i == m_highlightedIndex);
    }
}

void Menu::themeChanged() {
    applyStyle();
}

void Menu::applyStyle() {
    const Theme &t = theme();
    setBackgroundColor(t.surface);
    surface().setBorderColor(t.border);
}

} // namespace PandaUI
