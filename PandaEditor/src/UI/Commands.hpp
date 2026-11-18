#pragma once

#include "PandaUI/Core/KeyboardEvent.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Editor {

// Key chord: a key + an exact set of modifiers. The comparison is strict (Shift+Space != Space) so
// that bindings do not overlap. The default chord has no key: it matches nothing.
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
    bool operator==(const KeyChord &other) const {
        return key == other.key && shift == other.shift && ctrl == other.ctrl && alt == other.alt && super == other.super;
    }
    // The chord of a key event; unset for a modifier key alone.
    static KeyChord of(const PandaUI::KeyEvent &event);
    // The chord as a keymap writes it: "cmd-shift-p", "f5", "backspace". Modifiers: cmd, ctrl,
    // alt, shift, and primary — cmd on macOS, ctrl elsewhere (what the default keymap uses).
    // parse answers an unset chord for a text that names no key.
    static KeyChord parse(const std::string &name);
    std::string name() const;
    // How a menu, a tooltip or the palette shows it: "⇧⌘P", "F5"; empty for a chord that is not set.
    std::string text() const;
};

// Where the key of a command works. The palette, a menu and the MCP server run a command by its
// id regardless of the scope: the scope is about a key press only.
enum class CommandScope {
    Editor, // anywhere in the editor window
    World,  // only while the Scene surface or the Hierarchy has the keyboard (Backspace deletes an
            // entity there and nowhere else)
};

// The commands of the editor: everything the user can ask of it by a key, a menu item or by name
// (the command palette, the MCP server). A command is a stable id ("entity.duplicate"), the group
// and the name the palette shows ("Entity", "Duplicate"), a scope and a handler; a handler
// returns whether it did something — false lets the key go on to the next command bound to it
// and tells the palette the command does not apply now.
//
// THE KEYS LIVE IN ONE PLACE. No command, menu or view names a key: the default chords of every
// command are the table of UI/DefaultKeymap.cpp, the user's changes are `keymap.json` next to the
// editor's settings.json (an object: command id → array of chords, only what differs from the
// defaults; an empty array unbinds). Menus and tooltips ask keys(id). What stays in a view is
// what is not a command: text editing, navigation inside a list, the held flight keys of the
// viewport camera.
class Commands {
public:
    using Handler = std::function<bool()>;

    struct Command {
        std::string id;
        std::string group;
        std::string label;
        CommandScope scope = CommandScope::Editor;
        std::vector<KeyChord> defaults;
        std::vector<KeyChord> chords; // in effect: the user's, or the defaults
        bool customized = false;      // keymap.json names the command
        Handler handler;
    };

    // Registering again replaces the command (a controller that is rebuilt).
    void add(std::string id, std::string group, std::string label, Handler handler, CommandScope scope = CommandScope::Editor);
    const std::vector<Command> &all() const {
        return m_commands;
    }
    const Command *find(const std::string &id) const;

    bool handleKey(const PandaUI::KeyEvent &event); // → true: a command of the chord ran and took the key
    bool run(const std::string &id);                // → what the handler answered; false — no such command
    // The first chord of the command as text ("⌘S"), empty when it has none; the chord itself.
    std::string keys(const std::string &id) const;
    KeyChord chord(const std::string &id) const;
    // "Save  ⌘S" — a tooltip with the key of the command when it has one.
    std::string hint(const std::string &title, const std::string &id) const;

    // The user's keymap. load reads the file (a missing one — only the defaults); bind and reset
    // change one command and write the file. reloadKeymap reads the file again when it is not
    // the one the editor last read or wrote (an edit in another program): true — it was.
    void loadKeymap(const std::filesystem::path &path);
    bool reloadKeymap();
    void bind(const std::string &id, std::vector<KeyChord> chords);
    void resetBinding(const std::string &id);
    const std::filesystem::path &keymapPath() const {
        return m_keymapPath;
    }
    // The other commands a chord already runs in a scope that overlaps.
    std::vector<const Command *> boundTo(KeyChord chord, const std::string &exceptId) const;

    // Whether the world has the keyboard (CommandScope::World); what follows a keymap change
    // (the menu bar and the tooltips show keys).
    void setWorldScopeTest(std::function<bool()> test) {
        m_worldScopeTest = std::move(test);
    }
    void addKeymapListener(std::function<void()> listener) {
        m_listeners.push_back(std::move(listener));
    }
    // Recording a chord (the keymap editor): while a capture is set, a chord runs nothing and
    // goes to it instead. A menu that took the key before the UI saw it reports the command.
    void setCapture(std::function<void(KeyChord)> capture) {
        m_capture = std::move(capture);
    }
    bool isCapturing() const {
        return static_cast<bool>(m_capture);
    }
    void captureKeyOf(const std::string &id);

private:
    Command *findMutable(const std::string &id);
    void resolve(Command &command);
    void writeKeymap();
    void notify();

    std::vector<Command> m_commands;
    std::map<std::string, std::vector<std::string>> m_user; // keymap.json as read: id → chord names
    std::filesystem::path m_keymapPath;
    std::filesystem::file_time_type m_keymapTime{}; // of the file as the editor last read or wrote it
    std::function<bool()> m_worldScopeTest;
    std::function<void(KeyChord)> m_capture;
    std::vector<std::function<void()>> m_listeners;
};

// The default keymap: UI/DefaultKeymap.cpp — the only place of the editor that names keys.
struct DefaultBinding {
    const char *command;
    const char *chord;
};
const std::vector<DefaultBinding> &defaultKeymap();

} // namespace Editor
