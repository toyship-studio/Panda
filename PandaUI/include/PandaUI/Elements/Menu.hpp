#pragma once

#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PandaUI {

class Control;
class Popup;

// Menu content: a vertical list of clickable items and separators on a styled surface.
// Usually shown inside a Popup. After an item is chosen, onItemSelected is called (the Popup
// closes itself with it). Focusable: when shown inside a Popup it takes the keyboard
// (arrows/Enter/Space — navigation, →/← — enter/leave a submenu, Escape bubbles up to the Popup
// and closes the whole popup).
class Menu : public Panel {
public:
    using ItemAction = std::function<void()>;

    Menu();

    void addItem(std::string title, ItemAction action);
    void addSeparator();
    // Item with an arrow: hover/click/→ opens a submenu next to it (works only inside a Popup).
    // Choosing a submenu item closes the whole chain via onItemSelected of this menu.
    void addSubmenu(std::string title, std::shared_ptr<Menu> submenu);
    void setOnItemSelected(std::function<void()> callback);

    // Keyboard navigation. Normally the menu handles the keys itself (it is focused when shown in
    // a Popup), but the owner button (MenuButton) can also drive navigation while it has the focus.
    bool empty() const {
        return m_navRows.empty();
    }
    void moveHighlight(int delta); // ↑/↓ over the items (clamped)
    void activateHighlighted();    // Enter — run the highlighted item / open the submenu

private:
    class RowBase;
    class MenuItemRow;
    class SubmenuRow;

    struct SubmenuEntry {
        SubmenuRow *row;
        std::shared_ptr<Menu> menu;
    };

    bool keyDown(KeyEvent &event) override;
    bool wantsFocusWhenPresented() const override {
        return true;
    }
    void themeChanged() override;
    void applyStyle();
    void applyHighlight();
    void resetHighlight();

    // Submenu hover logic: hovering a regular item closes the open submenu, hovering a submenu
    // row opens its submenu (closing the other one). Opening via the keyboard (→/Enter)
    // additionally moves the focus into the submenu.
    void rowHovered(Control *row);
    void openSubmenuFor(SubmenuRow *row, bool focusSubmenu);
    void closeOpenSubmenu();
    Popup *findAncestorPopup();

    std::function<void()> m_onItemSelected;
    std::vector<RowBase *> m_navRows; // clickable items and submenu rows (without separators)
    std::vector<SubmenuEntry> m_submenus;
    Menu *m_parentMenu = nullptr;
    SubmenuRow *m_openSubmenuRow = nullptr;
    int m_highlightedIndex = -1;
};

} // namespace PandaUI
