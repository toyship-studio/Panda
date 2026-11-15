#include "UI/Commands.hpp"

namespace Editor {

// THE DEFAULT KEYMAP OF THE EDITOR — the only place that names keys. A row is a command id and a
// chord in the text of keymap.json (KeyChord::parse); several rows of one command are its
// alternative chords, the first one is what menus and tooltips show. `primary` is ⌘ on macOS and
// Ctrl elsewhere. A user's keymap.json replaces the chords of the commands it names.
const std::vector<DefaultBinding> &defaultKeymap() {
    static const std::vector<DefaultBinding> KEYMAP = {
        // File
        {"file.newWorld", "primary-n"},
        {"file.newWindow", "primary-shift-n"},
        {"file.openProject", "primary-o"},
        {"file.save", "primary-s"},
        {"file.saveAll", "primary-alt-s"},
        {"file.reload", "primary-r"},
        {"settings.open", "primary-,"},
        {"keymap.open", "primary-k"},
        {"app.exit", "primary-q"},
        // Edit
        {"edit.undo", "primary-z"},
        {"edit.redo", "primary-shift-z"},
        // Editor
        {"palette.show", "primary-shift-p"},
        {"goto.show", "primary-p"},
        {"hierarchy.search", "primary-f"},
        {"dock.toggleFullscreen", "shift-space"},
        // View
        {"camera.focusSelection", "f"},
        // Entity (the world has the keyboard)
        {"entity.delete", "backspace"},
        {"entity.delete", "delete"},
        {"entity.duplicate", "primary-d"},
        {"entity.copy", "primary-c"},
        {"entity.cut", "primary-x"},
        {"entity.paste", "primary-v"},
        {"entity.rename", "f2"},
        {"entity.selectAll", "primary-a"},
        {"entity.deselect", "escape"},
        // Tools of the gizmo
        {"gizmo.move", "1"},
        {"gizmo.rotate", "2"},
        {"gizmo.scale", "3"},
        // World
        {"world.play", "f5"},
        {"world.stop", "shift-f5"},
    };
    return KEYMAP;
}

} // namespace Editor
