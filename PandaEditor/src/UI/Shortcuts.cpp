#include "UI/Shortcuts.hpp"

#include <Foundation/PlatformDetection.hpp>

#include <utility>

namespace Editor {

KeyChord KeyChord::primary(PandaUI::Key key, bool shift, bool alt) {
    KeyChord chord;
    chord.key = key;
    chord.shift = shift;
    chord.alt = alt;
#ifdef PLATFORM_MACOS
    chord.super = true;
#else
    chord.ctrl = true;
#endif
    return chord;
}

std::string KeyChord::text() const {
    if (!isSet()) { return {}; }
    using PandaUI::Key;
    std::string result;
#ifdef PLATFORM_MACOS
    if (ctrl) { result += "⌃"; }
    if (alt) { result += "⌥"; }
    if (shift) { result += "⇧"; }
    if (super) { result += "⌘"; }
#else
    if (ctrl) { result += "Ctrl+"; }
    if (alt) { result += "Alt+"; }
    if (shift) { result += "Shift+"; }
    if (super) { result += "Win+"; }
#endif
    const int code = static_cast<int>(key);
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
        result += static_cast<char>(code);
        return result;
    }
    if (code >= static_cast<int>(Key::F1) && code <= static_cast<int>(Key::F25)) {
        result += "F" + std::to_string(code - static_cast<int>(Key::F1) + 1);
        return result;
    }
    switch (key) {
        case Key::Space: result += "Space"; break;
        case Key::Enter: result += "Enter"; break;
        case Key::Escape: result += "Esc"; break;
        case Key::Tab: result += "Tab"; break;
        case Key::Backspace: result += "⌫"; break;
        case Key::Delete: result += "Del"; break;
        case Key::Up: result += "↑"; break;
        case Key::Down: result += "↓"; break;
        case Key::Left: result += "←"; break;
        case Key::Right: result += "→"; break;
        case Key::Comma: result += ","; break;
        case Key::Period: result += "."; break;
        case Key::Slash: result += "/"; break;
        case Key::Minus: result += "-"; break;
        case Key::Equal: result += "="; break;
        default: result += "?"; break;
    }
    return result;
}

void ShortcutManager::registerCommand(
    std::string id, std::string group, std::string label, KeyChord defaultChord, Handler handler, KeyChord alternate
) {
    if (Command *existing = find(id)) { // re-registration updates (the user's binding is left untouched)
        existing->group = std::move(group);
        existing->label = std::move(label);
        existing->defaultChord = defaultChord;
        existing->alternate = alternate;
        existing->handler = std::move(handler);
        return;
    }
    m_commands.push_back(
        Command{std::move(id), std::move(group), std::move(label), defaultChord, defaultChord, alternate, std::move(handler)}
    );
}

void ShortcutManager::setBinding(const std::string &id, KeyChord chord) {
    if (Command *command = find(id)) { command->chord = chord; }
}

bool ShortcutManager::handleKey(const PandaUI::KeyEvent &event) {
    for (size_t i = 0; i < m_commands.size(); ++i) {
        const Command &command = m_commands[i];
        if (!command.handler || (!command.chord.matches(event) && !command.alternate.matches(event))) { continue; }
        // A copy: a handler may register commands (a project that opens) while it runs.
        const Handler handler = command.handler;
        // A command that does not apply now leaves the key to the next one bound to it.
        if (handler()) { return true; }
    }
    return false;
}

bool ShortcutManager::run(const std::string &id) {
    Command *command = find(id);
    if (command == nullptr || !command->handler) { return false; }
    const Handler handler = command->handler;
    return handler();
}

ShortcutManager::Command *ShortcutManager::find(const std::string &id) {
    for (Command &command : m_commands) {
        if (command.id == id) { return &command; }
    }
    return nullptr;
}

} // namespace Editor
