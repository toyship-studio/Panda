#pragma once

#include "Project/Export/ExportProjectTarget.hpp"
#include "Project/Settings/RecentProject.hpp"

#include <Fern/Window/Menu.hpp>
#include <Panda/Assets/Base/Path.hpp>

#include <string>
#include <vector>

namespace Fern {
struct Event;
class Window;
} // namespace Fern

namespace Panda {

class EditorMenuBarOutput {
public:
    virtual ~EditorMenuBarOutput() = default;
    virtual bool menuBarHasOpenedProject() = 0;
    virtual void menuBarOpenProject() = 0;
    virtual void menuBarOpenProject(const RecentProject &project) = 0;
    virtual void menuBarNewWorld() = 0;
    virtual void menuBarUndo() = 0;
    virtual void menuBarRedo() = 0;
    // The settings (the document model): the world sidebar of the viewport (Window menu); the
    // editor and the project settings documents and their files (the Settings submenu of the
    // File menu, the Zed layout: Open … opens the document, Show … File reveals the JSON in the
    // file manager — the editor has no text editor of its own).
    virtual void menuBarOpenWorldSettings() = 0;
    virtual void menuBarOpenProjectSettings() = 0;
    virtual void menuBarOpenEditorSettings() = 0;
    virtual void menuBarShowProjectSettingsFile() = 0;
    virtual void menuBarShowEditorSettingsFile() = 0;
    virtual const std::vector<RecentProject> &menuBarGetRecentProjectsList() = 0;
    virtual const path_t &menuBarGetOpenedProjectPath() = 0;
    virtual void menuBarCloseApp() = 0;
    virtual void menuBarShowCppProject() = 0;
    virtual void menuBarSaveWorld() = 0;
    virtual void menuBarReload() = 0;
    virtual void menuBarCloseProject() = 0;
    virtual void menuBarAbout() = 0;
    // The editor updater (App/EditorUpdater): a check on demand, the restart into a ready update
    // (the item exists only while one is ready), the release notes of the channel in the browser.
    virtual void menuBarCheckForUpdates() = 0;
    virtual void menuBarRestartToUpdate() = 0;
    virtual void menuBarViewReleaseNotes() = 0;
    virtual bool menuBarUpdateReady() = 0;
    virtual void menuBarExportGame() = 0;
    virtual void menuBarOpenExportProject(ExportProjectTarget target) = 0;
    // The Plugins tab (Window → Plugins): installing, updating and removing plugins, applying themes.
    virtual void menuBarOpenPlugins() = 0;
};

class EditorMenuBar final {
public:
    EditorMenuBar(Fern::Window *window, EditorMenuBarOutput *output);

    void rebuild();
    // modalPresented: an editor modal is open (Context::modals()) — menu commands are muted,
    // like all input under it: the dialog form is built from the state at the moment of opening, Undo/Save
    // under it would desynchronize it. Quitting the application remains available.
    bool handleEvent(Fern::Event *event, bool modalPresented);

private:
    void processCommand(Fern::MenuCommandId commandId);

    Fern::Window *m_window;
    EditorMenuBarOutput *m_output;
};

} // namespace Panda
