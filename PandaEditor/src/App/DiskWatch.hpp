#pragma once

#include <Fern/Files/FileWatcher.hpp>
#include <Panda/Assets/Base/Path.hpp>

#include <memory>

namespace Panda {

class EditorLayer;

// The files the editor works on, followed (context/architecture.md, "The document model"): what
// changes on disk while the editor runs — an edit in another program, an agent, git — the editor
// takes by itself, without a question (DocumentActions::takeDiskChanges). There is no command
// for it: the user never asks the editor to read the disk.
//
// It takes while it is the application in front: at once then, and when it comes back otherwise.
// A change waits while the game plays, while a button of the mouse is held and while a dialog
// is open: nothing is pulled from under a hand. A document with unsaved edits is left alone —
// Save asks about it.
//
// What is watched: of the editor — its settings file and its keymap; of the open project — the
// Assets folder (the assets, their sidecars, the worlds), project.json and the script library.
// The caches and the build folders of a project are not watched.
class DiskWatch final {
public:
    explicit DiskWatch(EditorLayer &app);
    ~DiskWatch();

    // What is followed from here on: the files of the editor, and those of the project that is
    // open now (an empty path — none is).
    void watch(const path_t &projectPath);
    // The editor is the application in front — one of its windows has the keyboard — or behind.
    void setInFront(bool inFront) {
        m_inFront = inFront;
    }
    bool isInFront() const {
        return m_inFront;
    }
    // A button of the mouse went down or up in a window of the editor.
    void pointerHeld(bool held) {
        m_pointerHeld = held;
    }
    // Something changed on disk and is not taken yet.
    bool isWaiting() const {
        return m_waiting;
    }
    // Every frame: what the watcher was told, and the taking when its time has come.
    void update(double deltaTime);

private:
    void note(const Fern::FileWatcher::Changes &changes);
    void reopenForModules(double deltaTime);

    EditorLayer &m_app;
    std::unique_ptr<Fern::FileWatcher> m_watcher;
    // As real paths: the watcher tells real paths.
    path_t m_assets;          // the Assets folder of the project
    path_t m_projectSettings; // its project.json
    path_t m_keymap;          // the keymap of the editor
    bool m_inFront = false;
    bool m_pointerHeld = false;
    bool m_waiting = false;       // something changed and is not taken yet
    bool m_newFiles = false;      // a file came or went among them: the asset registry reads the tree again
    bool m_keymapChanged = false; // the keymap is among them
    bool m_projectSettingsChanged = false; // and project.json: its modules are looked at after it is taken
    double m_sinceLast = 0.0;     // seconds since the last change was told
    double m_sinceFirst = 0.0;    // and since the first one that waits
    // project.json, taken, switches a module on: the project has to be opened again for it. That
    // is done when nothing is unsaved — at once, or when the user has saved.
    bool m_reopenWaits = false;
    bool m_reopenSaid = false;
    double m_sinceReopenLook = 0.0;
};

} // namespace Panda
