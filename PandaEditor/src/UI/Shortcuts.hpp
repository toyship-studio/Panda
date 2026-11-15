#pragma once

#include "PandaUI/Core/KeyboardEvent.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Editor {

// Key chord: a key + an exact set of modifiers. The comparison is strict (Shift+Space != Space) so
// that bindings do not overlap. A plain struct — easy to serialize (later — user bindings). The
// default chord has no key: it matches nothing.
struct KeyChord {
    PandaUI::Key key = PandaUI::Key::Unknown;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;

    bool isSet() const {
        return key != PandaUI::Key::Unknown;
    }
    bool matches(const PandaUI::KeyEvent &event) const {
        return isSet() && event.key == key && event.isShiftDown == shift && event.isControlDown == ctrl &&
               event.isAltDown == alt && event.isSuperDown == super;
    }
    // The chord with the "command" modifier of the platform: ⌘ on macOS, Ctrl elsewhere.
    static KeyChord primary(PandaUI::Key key, bool shift = false, bool alt = false);
    static KeyChord plain(PandaUI::Key key, bool shift = false) {
        KeyChord chord;
        chord.key = key;
        chord.shift = shift;
        return chord;
    }
    // How a menu or a palette writes it: "⌘D", "⇧Space", "F2"; empty for a chord that is not set.
    std::string text() const;
};

// The commands of the editor: what the user can ask of it by a key or by name. A command is a
// stable id ("entity.duplicate"), the group it is filed under and its name as the command palette
// shows them ("Entity", "Duplicate"), the chords that run it and a handler. The binding is
// separated from the handler and can be reassigned (setBinding) — the ground for user key
// settings. handleKey finds the command of a chord and runs it; run does the same by id (the
// palette). A handler returns whether it did something: false lets the key go on (a space in a
// text field) and tells the palette the command does not apply now.
class ShortcutManager {
public:
    using Handler = std::function<bool()>;

    struct Command {
        std::string id;
        std::string group;
        std::string label;
        KeyChord defaultChord;
        KeyChord chord;     // current (by default = defaultChord), reassignable
        KeyChord alternate; // a second chord for the same command (Delete next to Backspace)
        Handler handler;
    };

    void registerCommand(
        std::string id, std::string group, std::string label, KeyChord defaultChord, Handler handler,
        KeyChord alternate = {}
    );
    void setBinding(const std::string &id, KeyChord chord); // reassign (later — from the settings)
    bool handleKey(const PandaUI::KeyEvent &event);         // → true if a command was found and consumed the event
    bool run(const std::string &id);                        // → what the handler answered; false — no such command
    const std::vector<Command> &commands() const {
        return m_commands;
    }

private:
    Command *find(const std::string &id);

    std::vector<Command> m_commands;
};

} // namespace Editor
