#include "UI/Commands.hpp"

#include <Fern/Files/AtomicWrite.hpp>
#include <Foundation/Assert.hpp>
#include <Foundation/Logger.hpp>
#include <Foundation/PlatformDetection.hpp>
#include <Rain/Value.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <utility>

namespace Editor {

namespace {

    using PandaUI::Key;

    // The keys a keymap can name besides letters, digits and F1…F25: the name of the file and
    // the text of a menu.
    struct NamedKey {
        Key key;
        const char *name;
        const char *text;
    };
    const NamedKey NAMED_KEYS[] = {
        {Key::Space, "space", "Space"},
        {Key::Enter, "enter", "Enter"},
        {Key::Escape, "escape", "Esc"},
        {Key::Tab, "tab", "Tab"},
        {Key::Backspace, "backspace", "⌫"},
        {Key::Delete, "delete", "Del"},
        {Key::Insert, "insert", "Ins"},
        {Key::Up, "up", "↑"},
        {Key::Down, "down", "↓"},
        {Key::Left, "left", "←"},
        {Key::Right, "right", "→"},
        {Key::PageUp, "pageup", "PgUp"},
        {Key::PageDown, "pagedown", "PgDn"},
        {Key::Home, "home", "Home"},
        {Key::End, "end", "End"},
        {Key::Comma, ",", ","},
        {Key::Period, ".", "."},
        {Key::Slash, "/", "/"},
        {Key::Minus, "-", "-"},
        {Key::Equal, "=", "="},
        {Key::Semicolon, ";", ";"},
        {Key::Apostrophe, "'", "'"},
        {Key::LeftBracket, "[", "["},
        {Key::RightBracket, "]", "]"},
        {Key::Backslash, "\\", "\\"},
        {Key::GraveAccent, "`", "`"},
    };

    bool isModifier(Key key) {
        const int code = static_cast<int>(key);
        return code >= static_cast<int>(Key::LeftShift) && code <= static_cast<int>(Key::RightSuper);
    }

    // The key alone, lower case for the file ("p", "f5", "backspace") or as shown ("P", "F5", "⌫").
    std::string keyName(Key key, bool forFile) {
        const int code = static_cast<int>(key);
        if (code >= 'A' && code <= 'Z') { return std::string(1, static_cast<char>(forFile ? code - 'A' + 'a' : code)); }
        if (code >= '0' && code <= '9') { return std::string(1, static_cast<char>(code)); }
        if (code >= static_cast<int>(Key::F1) && code <= static_cast<int>(Key::F25)) {
            return (forFile ? "f" : "F") + std::to_string(code - static_cast<int>(Key::F1) + 1);
        }
        for (const NamedKey &named : NAMED_KEYS) {
            if (named.key == key) { return forFile ? named.name : named.text; }
        }
        return {};
    }

    Key keyOfName(const std::string &name) {
        if (name.size() == 1) {
            const char c = name[0];
            if (c >= 'a' && c <= 'z') { return static_cast<Key>(c - 'a' + 'A'); }
            if (c >= '0' && c <= '9') { return static_cast<Key>(c); }
        }
        if (name.size() >= 2 && name[0] == 'f') { // f1 … f25; "f99999999999" of a hand-written keymap is no key
            const char *end = name.data() + name.size();
            int number = 0;
            const auto [stop, failure] = std::from_chars(name.data() + 1, end, number);
            if (failure == std::errc() && stop == end && number >= 1 && number <= 25) {
                return static_cast<Key>(static_cast<int>(Key::F1) + number - 1);
            }
        }
        for (const NamedKey &named : NAMED_KEYS) {
            if (name == named.name) { return named.key; }
        }
        return Key::Unknown;
    }

    std::vector<KeyChord> parseChords(const std::vector<std::string> &names, const std::string &id) {
        std::vector<KeyChord> chords;
        for (const std::string &name : names) {
            const KeyChord chord = KeyChord::parse(name);
            if (chord.isSet()) {
                chords.push_back(chord);
            } else {
                LOG_WARN_EDITOR("keymap.json: \"%s\" of %s is not a key chord", name.c_str(), id.c_str());
            }
        }
        return chords;
    }

} // namespace

KeyChord KeyChord::of(const PandaUI::KeyEvent &event) {
    KeyChord chord;
    if (isModifier(event.key) || keyName(event.key, true).empty()) { return chord; }
    chord.key = event.key;
    chord.shift = event.isShiftDown;
    chord.ctrl = event.isControlDown;
    chord.alt = event.isAltDown;
    chord.super = event.isSuperDown;
    return chord;
}

KeyChord KeyChord::parse(const std::string &name) {
    KeyChord chord;
    size_t start = 0;
    while (start < name.size()) {
        // The last part is the key, and the key may be "-" itself ("cmd--").
        size_t dash = name.find('-', start);
        if (dash == start) { dash = name.find('-', start + 1); }
        const std::string part = name.substr(start, dash == std::string::npos ? std::string::npos : dash - start);
        const bool last = dash == std::string::npos;
        if (!last) {
            if (part == "shift") {
                chord.shift = true;
            } else if (part == "ctrl") {
                chord.ctrl = true;
            } else if (part == "alt") {
                chord.alt = true;
            } else if (part == "cmd") {
                chord.super = true;
            } else if (part == "primary") {
#ifdef PLATFORM_MACOS
                chord.super = true;
#else
                chord.ctrl = true;
#endif
            } else {
                return {};
            }
            start = dash + 1;
            continue;
        }
        chord.key = keyOfName(part);
        break;
    }
    if (!chord.isSet()) { return {}; }
    return chord;
}

std::string KeyChord::name() const {
    if (!isSet()) { return {}; }
    std::string result;
    if (ctrl) { result += "ctrl-"; }
    if (alt) { result += "alt-"; }
    if (shift) { result += "shift-"; }
    if (super) { result += "cmd-"; }
    return result + keyName(key, true);
}

std::string KeyChord::text() const {
    if (!isSet()) { return {}; }
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
    return result + keyName(key, false);
}

void Commands::add(std::string id, std::string group, std::string label, Handler handler, CommandScope scope) {
    Command *command = findMutable(id);
    if (command == nullptr) {
        m_commands.emplace_back();
        command = &m_commands.back();
        command->id = std::move(id);
        for (const DefaultBinding &binding : defaultKeymap()) {
            if (command->id != binding.command) { continue; }
            const KeyChord chord = KeyChord::parse(binding.chord);
            PND_ASSERT_F(chord.isSet(), "The default keymap: \"%s\" is not a key chord", binding.chord);
            command->defaults.push_back(chord);
        }
    }
    command->group = std::move(group);
    command->label = std::move(label);
    command->scope = scope;
    command->handler = std::move(handler);
    resolve(*command);
}

const Commands::Command *Commands::find(const std::string &id) const {
    for (const Command &command : m_commands) {
        if (command.id == id) { return &command; }
    }
    return nullptr;
}

Commands::Command *Commands::findMutable(const std::string &id) {
    return const_cast<Command *>(std::as_const(*this).find(id));
}

void Commands::resolve(Command &command) {
    const auto user = m_user.find(command.id);
    command.customized = user != m_user.end();
    command.chords = command.customized ? parseChords(user->second, command.id) : command.defaults;
}

bool Commands::handleKey(const PandaUI::KeyEvent &event) {
    if (m_capture) {
        const KeyChord chord = KeyChord::of(event);
        if (chord.isSet()) { m_capture(chord); }
        return true;
    }
    const bool worldHasKeyboard = m_worldScopeTest && m_worldScopeTest();
    for (size_t i = 0; i < m_commands.size(); ++i) {
        const Command &command = m_commands[i];
        if (!command.handler || (command.scope == CommandScope::World && !worldHasKeyboard)) { continue; }
        const bool bound = std::any_of(command.chords.begin(), command.chords.end(), [&event](const KeyChord &chord) {
            return chord.matches(event);
        });
        if (!bound) { continue; }
        // A copy: a handler may register commands (a project that opens) while it runs.
        const Handler handler = command.handler;
        // A command that does not apply now leaves the key to the next one bound to it.
        if (handler()) { return true; }
    }
    return false;
}

bool Commands::run(const std::string &id) {
    const Command *command = find(id);
    if (command == nullptr || !command->handler) { return false; }
    const Handler handler = command->handler;
    return handler();
}

KeyChord Commands::chord(const std::string &id) const {
    const Command *command = find(id);
    return command != nullptr && !command->chords.empty() ? command->chords.front() : KeyChord();
}

std::string Commands::keys(const std::string &id) const {
    return chord(id).text();
}

std::string Commands::hint(const std::string &title, const std::string &id) const {
    const std::string text = keys(id);
    return text.empty() ? title : title + "  " + text;
}

void Commands::captureKeyOf(const std::string &id) {
    const KeyChord pressed = chord(id);
    if (m_capture && pressed.isSet()) { m_capture(pressed); }
}

std::vector<const Commands::Command *> Commands::boundTo(KeyChord chord, const std::string &exceptId) const {
    std::vector<const Command *> result;
    for (const Command &command : m_commands) {
        if (command.id == exceptId) { continue; }
        if (std::find(command.chords.begin(), command.chords.end(), chord) != command.chords.end()) { result.push_back(&command); }
    }
    return result;
}

namespace {
    std::filesystem::file_time_type timeOf(const std::filesystem::path &path) {
        std::error_code error;
        const auto time = std::filesystem::last_write_time(path, error);
        return error ? std::filesystem::file_time_type{} : time;
    }
} // namespace

bool Commands::reloadKeymap() {
    if (m_keymapPath.empty() || timeOf(m_keymapPath) == m_keymapTime) { return false; }
    loadKeymap(m_keymapPath);
    return true;
}

void Commands::loadKeymap(const std::filesystem::path &path) {
    m_keymapPath = path;
    m_keymapTime = timeOf(path);
    m_user.clear();
    std::ifstream file(path);
    if (file.is_open()) {
        // A file of a person: comments and a comma after the last item are taken.
        Rain::Value document;
        if (!Rain::parseJson(file, document, Rain::JsonSyntax::Relaxed) || !document.isObject()) {
            LOG_ERROR_EDITOR("%s is not a JSON object: the default keys are used.", path.string().c_str());
        } else {
            for (const Rain::Value::Member &member : document.members()) {
                std::vector<std::string> names;
                if (member.value.isString()) {
                    names.emplace_back(member.value.getString());
                } else if (member.value.isArray()) {
                    for (const Rain::Value &item : member.value.items()) {
                        if (item.isString()) { names.emplace_back(item.getString()); }
                    }
                }
                m_user[member.key] = std::move(names);
            }
        }
    }
    for (Command &command : m_commands) { resolve(command); }
    notify();
}

void Commands::bind(const std::string &id, std::vector<KeyChord> chords) {
    Command *command = findMutable(id);
    if (command == nullptr) { return; }
    if (chords == command->defaults) {
        m_user.erase(id);
    } else {
        std::vector<std::string> names;
        for (const KeyChord &chord : chords) { names.push_back(chord.name()); }
        m_user[id] = std::move(names);
    }
    resolve(*command);
    writeKeymap();
    notify();
}

void Commands::resetBinding(const std::string &id) {
    Command *command = findMutable(id);
    if (command == nullptr || m_user.erase(id) == 0) { return; }
    resolve(*command);
    writeKeymap();
    notify();
}

// The file holds what differs from the defaults and nothing else; the entries of commands this
// build does not know (a module that is off) are kept.
void Commands::writeKeymap() {
    if (m_keymapPath.empty()) { return; }
    Rain::Value document = Rain::Value::object();
    for (const auto &[id, names] : m_user) {
        Rain::Value &keys = document.add(id, Rain::Value::array());
        for (const std::string &name : names) { keys.push(name); }
    }
    std::string error;
    if (!Fern::writeFileAtomically(m_keymapPath, Rain::writeJson(document, Rain::JsonLayout::PrettyFlatArrays) + "\n", &error)) {
        LOG_ERROR_EDITOR("The keymap is not saved: %s.", error.c_str());
        return;
    }
    m_keymapTime = timeOf(m_keymapPath); // the editor's own write is not a change on disk
}

void Commands::notify() {
    // A copy: a listener may rebuild what registered it.
    const std::vector<std::function<void()>> listeners = m_listeners;
    for (const auto &listener : listeners) { listener(); }
}

} // namespace Editor
