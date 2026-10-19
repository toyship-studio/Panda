#include "UI/Shortcuts.hpp"

#include <utility>

namespace Editor {

void ShortcutManager::registerCommand(std::string id, std::string label, KeyChord defaultChord, Handler handler) {
    if (Command *existing = find(id)) { // re-registration updates (the user's binding is left untouched)
        existing->label = std::move(label);
        existing->defaultChord = defaultChord;
        existing->handler = std::move(handler);
        return;
    }
    m_commands.push_back(Command{std::move(id), std::move(label), defaultChord, defaultChord, std::move(handler)});
}

void ShortcutManager::setBinding(const std::string &id, KeyChord chord) {
    if (Command *command = find(id)) { command->chord = chord; }
}

bool ShortcutManager::handleKey(const PandaUI::KeyEvent &event) {
    for (Command &command : m_commands) {
        if (command.handler && command.chord.matches(event)) { return command.handler(); }
    }
    return false;
}

ShortcutManager::Command *ShortcutManager::find(const std::string &id) {
    for (Command &command : m_commands) {
        if (command.id == id) { return &command; }
    }
    return nullptr;
}

} // namespace Editor
