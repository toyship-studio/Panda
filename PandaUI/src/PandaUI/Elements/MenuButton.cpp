#include "PandaUI/Elements/MenuButton.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Elements/Menu.hpp"
#include "PandaUI/Elements/Popup.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <memory>
#include <utility>

namespace PandaUI {

MenuButton::MenuButton()
    : MenuButton("") {}

MenuButton::MenuButton(std::string title)
    : Button(std::move(title)) {}

void MenuButton::addItem(std::string title, ItemAction action) {
    m_items.push_back({std::move(title), std::move(action), false});
}

void MenuButton::addSeparator() {
    m_items.push_back({"", nullptr, true});
}

void MenuButton::clearItems() {
    m_items.clear();
}

void MenuButton::performPrimaryAction() {
    openMenu();
}

void MenuButton::openMenu() {
    Context *context = getContext();
    if (!context || m_popup) { return; } // already open — do not spawn another

    auto menu = std::make_shared<Menu>();
    for (const Entry &entry : m_items) {
        if (entry.separator) {
            menu->addSeparator();
        } else {
            menu->addItem(entry.title, entry.action);
        }
    }

    auto popup = std::make_shared<Popup>(menu, getWorldFrame());
    Popup *p = popup.get();
    m_popup = p;
    m_menu = menu.get();
    menu->setOnItemSelected([p] { p->dismiss(); }); // item selection (click/Enter) → close
    popup->setOnDismiss([this, context, p] {
        m_popup = nullptr;
        m_menu = nullptr;
        context->removeFocusScope(p);
    });
    context->addOverlay(popup);
    context->addFocusScope(p, this, [p] { p->dismiss(); }); // the focus leaves the button/menu → close
}

bool MenuButton::keyDown(KeyEvent &event) {
    if (m_popup && m_menu) { // the menu is open — the focus is on the button, which drives the navigation
        switch (event.key) {
            case Key::Down: m_menu->moveHighlight(1); return true;
            case Key::Up: m_menu->moveHighlight(-1); return true;
            case Key::Enter:
            case Key::KeypadEnter:
            case Key::Space: m_menu->activateHighlighted(); return true;
            case Key::Escape: m_popup->dismiss(); return true;
            default: return false;
        }
    }
    switch (event.key) { // the menu is closed
        case Key::Down:
        case Key::Enter:
        case Key::KeypadEnter:
        case Key::Space: openMenu(); return true;
        default: return false;
    }
}

} // namespace PandaUI
