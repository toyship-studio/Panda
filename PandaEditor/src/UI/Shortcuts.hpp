#pragma once

#include "PandaUI/Core/KeyboardEvent.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Editor {

// Key chord: a key + an exact set of modifiers. The comparison is strict (Shift+Space != Space) so
// that bindings do not overlap. A plain struct — easy to serialize (later — user bindings).
struct KeyChord {
    PandaUI::Key key = PandaUI::Key::Unknown;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;

    bool matches(const PandaUI::KeyEvent &event) const {
        return event.key == key && event.isShiftDown == shift && event.isControlDown == ctrl &&
               event.isAltDown == alt && event.isSuperDown == super;
    }
};

// Editor hotkey manager (a separate one, as agreed — NOT hardcoded in handlers). A command =
// a stable id + a human-readable name + a binding (chord) + a handler. The binding is separated from
// the handler and can be reassigned (setBinding) — groundwork for future user hotkey settings
// (id -> serializable binding, the commands() list for the UI). handleKey looks up the command by the
// current chord and executes it; the handler returns whether the event was consumed (false → the key
// goes further, e.g. a space in an input field).
class ShortcutManager {
public:
    using Handler = std::function<bool()>;

    struct Command {
        std::string id;
        std::string label;
        KeyChord defaultChord;
        KeyChord chord; // current (by default = defaultChord), reassignable
        Handler handler;
    };

    void registerCommand(std::string id, std::string label, KeyChord defaultChord, Handler handler);
    void setBinding(const std::string &id, KeyChord chord); // reassign (later — from the settings)
    bool handleKey(const PandaUI::KeyEvent &event);         // → true if a command was found and consumed the event

    const std::vector<Command> &commands() const { // for the future settings UI / serialization
        return m_commands;
    }

private:
    Command *find(const std::string &id);
    std::vector<Command> m_commands;
};

} // namespace Editor
