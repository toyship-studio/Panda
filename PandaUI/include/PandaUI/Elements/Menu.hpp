#pragma once

#include "PandaUI/Core/Appearance.hpp"

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
// The look of a menu: the popover card, the rows and their highlight, the separators.
struct MenuAppearance {
    SurfaceStyle card{ThemeColor::surface, ThemeColor::border}; // the radius and the shadow are the popover elevation
    float width = 200.f;
    float padding = Spacing::sm;
    float itemGap = 1.f;
    float itemHeight = 26.f;
    float itemPaddingX = Spacing::md;
    float itemRadius = Radius::sm;
    Font itemFont = Font(16.f);
    ThemeColor itemText = ThemeColor::textPrimary;
    ThemeColor itemHighlight = ThemeColor::accent; // the row under the cursor or the keyboard
    ThemeColor itemHighlightedText = Color(0xFFFFFFFF);
    SurfaceImage itemHighlightImage;
    ThemeColor chevron = ThemeColor::textDim; // of a submenu row; the highlighted text color when highlighted
    ThemeColor separator = ThemeColor::borderMed;
    float chevronLong = 3.6f;
    float chevronShort = 1.9f;
    float chevronThickness = 1.6f;
};

class Menu : public Panel {
public:
    using ItemAction = std::function<void()>;

    Menu();

    // The look of the menus created from now on (Appearance.hpp).
    static MenuAppearance &defaultAppearance();
    void setAppearance(const MenuAppearance &appearance);
    const MenuAppearance &appearance() const {
        return m_appearance;
    }

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
    void applyAppearance();
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

    MenuAppearance m_appearance;
    std::function<void()> m_onItemSelected;
    std::vector<RowBase *> m_navRows; // clickable items and submenu rows (without separators)
    std::vector<Panel *> m_separators; // ownership is in m_subviews
    std::vector<SubmenuEntry> m_submenus;
    Menu *m_parentMenu = nullptr;
    SubmenuRow *m_openSubmenuRow = nullptr;
    int m_highlightedIndex = -1;
};

} // namespace PandaUI
