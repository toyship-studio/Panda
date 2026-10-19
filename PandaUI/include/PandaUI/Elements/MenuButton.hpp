#pragma once

#include "PandaUI/Elements/Button.hpp"

#include <functional>
#include <string>
#include <vector>

namespace PandaUI {

class Menu;
class Popup;

// Dropdown button: on press, opens a menu below itself (as a Popup overlay). The items are
// configured in advance; the menu is built on every opening. Works with both mouse and keyboard:
// ↑/↓ walk through the items, Enter selects, Escape and loss of focus close it.
class MenuButton : public Button {
public:
    using ItemAction = std::function<void()>;

    MenuButton();
    explicit MenuButton(std::string title);

    void addItem(std::string title, ItemAction action);
    void addSeparator();
    void clearItems(); // reset the items (the menu is rebuilt on the next opening)

    bool keyDown(KeyEvent &event) override;

private:
    struct Entry {
        std::string title;
        ItemAction action;
        bool separator;
    };

    void performPrimaryAction() override;
    void openMenu();

    std::vector<Entry> m_items;
    Menu *m_menu = nullptr;     // the open menu (or null) — for arrow-key navigation
    Popup *m_popup = nullptr;   // the open popup (or null) — do not open it again
};

} // namespace PandaUI
