#pragma once

#include "Fern/Events/Key.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Fern {

using MenuCommandId = uint32_t;
using MenuModifiers = uint8_t;

struct MenuShortcut {
    static constexpr MenuModifiers None = 0;
    static constexpr MenuModifiers Shift = 1 << 0;
    static constexpr MenuModifiers Control = 1 << 1;
    static constexpr MenuModifiers Alt = 1 << 2;
    static constexpr MenuModifiers Super = 1 << 3;
    static constexpr MenuModifiers Primary = 1 << 4;

    Key key = Key::KEY_UNKNOWN;
    MenuModifiers modifiers = None;

    MenuShortcut() = default;

    MenuShortcut(Key key, MenuModifiers modifiers)
        : key(key)
        , modifiers(modifiers) {}

    bool isValid() const {
        return key != Key::KEY_UNKNOWN;
    }
};

enum class MenuItemType {
    Command,
    Separator,
    Submenu,
};

class MenuItem {
public:
    static MenuItem command(std::string title, MenuCommandId commandId, MenuShortcut shortcut = {}) {
        MenuItem item(MenuItemType::Command);
        item.m_title = std::move(title);
        item.m_commandId = commandId;
        item.m_shortcut = shortcut;
        return item;
    }

    static MenuItem separator() {
        return MenuItem(MenuItemType::Separator);
    }

    static MenuItem submenu(std::string title) {
        MenuItem item(MenuItemType::Submenu);
        item.m_title = std::move(title);
        return item;
    }

    MenuItem &addItem(std::string title, MenuCommandId commandId, MenuShortcut shortcut = {}) {
        m_children.push_back(MenuItem::command(std::move(title), commandId, shortcut));
        return m_children.back();
    }

    MenuItem &addSubmenu(std::string title) {
        m_children.push_back(MenuItem::submenu(std::move(title)));
        return m_children.back();
    }

    void addSeparator() {
        m_children.push_back(MenuItem::separator());
    }

    MenuItem &setEnabled(bool enabled) {
        m_enabled = enabled;
        return *this;
    }

    MenuItem &setChecked(bool checked) {
        m_checked = checked;
        return *this;
    }

    MenuItemType getType() const {
        return m_type;
    }

    const std::string &getTitle() const {
        return m_title;
    }

    MenuCommandId getCommandId() const {
        return m_commandId;
    }

    MenuShortcut getShortcut() const {
        return m_shortcut;
    }

    bool isEnabled() const {
        return m_enabled;
    }

    bool isChecked() const {
        return m_checked;
    }

    const std::vector<MenuItem> &getChildren() const {
        return m_children;
    }

private:
    explicit MenuItem(MenuItemType type)
        : m_type(type) {}

    MenuItemType m_type = MenuItemType::Command;
    std::string m_title;
    MenuCommandId m_commandId = 0;
    MenuShortcut m_shortcut;
    bool m_enabled = true;
    bool m_checked = false;
    std::vector<MenuItem> m_children;
};

class MenuBar {
public:
    MenuItem &addMenu(std::string title) {
        m_menus.push_back(MenuItem::submenu(std::move(title)));
        return m_menus.back();
    }

    void clear() {
        m_menus.clear();
    }

    bool isEmpty() const {
        return m_menus.empty();
    }

    const std::vector<MenuItem> &getMenus() const {
        return m_menus;
    }

private:
    std::vector<MenuItem> m_menus;
};

} // namespace Fern
