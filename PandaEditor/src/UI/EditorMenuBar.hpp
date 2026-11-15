#pragma once

#include "Project/Settings/RecentProject.hpp"

#include <Fern/Window/Menu.hpp>
#include <Panda/Assets/Base/Path.hpp>

#include <string>
#include <vector>

namespace Editor {
class Commands;
}

namespace Fern {
struct Event;
class Window;
} // namespace Fern

namespace Panda {

// What the menu bar asks besides commands: the lists that change with the project. Every other
// item is a command of the editor (UI/Commands.hpp) by its id — the menu names no key and no
// handler of its own.
class EditorMenuBarOutput {
public:
    virtual ~EditorMenuBarOutput() = default;
    virtual bool menuBarHasOpenedProject() = 0;
    virtual void menuBarOpenProject(const RecentProject &project) = 0;
    // File → New: what the open project can create besides a world — the entities of the open
    // world and the assets (the core's and the modules'). Titles for the menu, the index to create.
    virtual std::vector<std::string> menuBarNewEntityTitles() = 0;
    virtual std::vector<std::string> menuBarNewAssetTitles() = 0;
    virtual void menuBarNewEntity(size_t index) = 0;
    virtual void menuBarNewAsset(size_t index) = 0;
    virtual const std::vector<RecentProject> &menuBarGetRecentProjectsList() = 0;
    virtual const path_t &menuBarGetOpenedProjectPath() = 0;
    // "Restart to Update" exists only while a downloaded update waits.
    virtual bool menuBarUpdateReady() = 0;
};

// An item of the menu bar that is not a command of the registry (File → New → an entity, a
// recent project), as the command palette lists it.
struct MenuCommandInfo final {
    std::string menu;  // "File"
    std::string title; // "New: Camera"
    uint32_t id = 0;   // what EditorMenuBar::run takes
};

class EditorMenuBar final {
public:
    EditorMenuBar(Fern::Window *window, EditorMenuBarOutput *output, Editor::Commands &commands);

    void rebuild();
    // The enabled items that are not commands of the registry, flattened: the command palette
    // shows them by name. run does what choosing the item does.
    std::vector<MenuCommandInfo> dynamicItems() const;
    void run(uint32_t commandId) {
        processCommand(commandId);
    }
    // modalPresented: an editor modal is open (Context::modals()) — menu commands are muted,
    // like all input under it: the dialog form is built from the state at the moment of opening, Undo/Save
    // under it would desynchronize it. Quitting the application remains available.
    bool handleEvent(Fern::Event *event, bool modalPresented);

private:
    void processCommand(Fern::MenuCommandId commandId);

    Fern::Window *m_window;
    EditorMenuBarOutput *m_output;
    Editor::Commands &m_commands;
    Fern::MenuBar m_menuBar;              // as last built
    std::vector<std::string> m_commandIds; // the command of a menu item: Fern id - 1 → command id
};

} // namespace Panda
