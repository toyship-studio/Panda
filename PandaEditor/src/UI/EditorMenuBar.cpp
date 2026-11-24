#include "EditorMenuBar.hpp"

#include "UI/Commands.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Fern/Events/Event.hpp>
#include <Fern/Events/MenuEvents.hpp>
#include <Fern/Window/Window.hpp>

namespace {

// The Fern ids of the items: the commands of the registry count from 1 in the order the menu
// names them, the lists of the project take the ranges above.
constexpr Fern::MenuCommandId OPEN_RECENT_PROJECT_BASE = 1000;
constexpr Fern::MenuCommandId NEW_ENTITY_BASE = 2000;
constexpr Fern::MenuCommandId NEW_ASSET_BASE = 3000;

Fern::MenuShortcut menuShortcut(Editor::KeyChord chord) {
    if (!chord.isSet()) { return {}; }
    Fern::MenuModifiers modifiers = Fern::MenuShortcut::None;
    if (chord.shift) { modifiers |= Fern::MenuShortcut::Shift; }
    if (chord.ctrl) { modifiers |= Fern::MenuShortcut::Control; }
    if (chord.alt) { modifiers |= Fern::MenuShortcut::Alt; }
    if (chord.super) { modifiers |= Fern::MenuShortcut::Super; }
    return {static_cast<Fern::Key>(static_cast<int>(chord.key)), modifiers};
}

} // namespace

namespace Panda {

EditorMenuBar::EditorMenuBar(Fern::Window *window, EditorMenuBarOutput *output, Editor::Commands &commands)
    : m_window(window)
    , m_output(output)
    , m_commands(commands) {}

void EditorMenuBar::rebuild() {
    const bool hasProject = m_output->menuBarHasOpenedProject();
    Fern::MenuBar menuBar;
    m_commandIds.clear();
    // An item of a command: the title is the menu's own (short, in the context of its menu), the
    // key is the one the keymap gives the command.
    auto item = [this](Fern::MenuItem &menu, const char *title, const char *command) -> Fern::MenuItem & {
        m_commandIds.emplace_back(command);
        return menu.addItem(title, static_cast<Fern::MenuCommandId>(m_commandIds.size()), menuShortcut(m_commands.chord(command)));
    };

    auto &fileMenu = menuBar.addMenu("File");
    // The first section is what begins something: New lists everything the project can create
    // (a world, the entities of the open world, the assets), New Window starts another instance
    // of the editor — one project per window.
    if (hasProject) {
        auto &newMenu = fileMenu.addSubmenu("New");
        item(newMenu, "World", "file.newWorld");
        const std::vector<std::string> entities = m_output->menuBarNewEntityTitles();
        if (!entities.empty()) { newMenu.addSeparator(); }
        for (size_t i = 0; i < entities.size(); ++i) {
            newMenu.addItem(entities[i], NEW_ENTITY_BASE + static_cast<Fern::MenuCommandId>(i));
        }
        const std::vector<std::string> assets = m_output->menuBarNewAssetTitles();
        if (!assets.empty()) { newMenu.addSeparator(); }
        for (size_t i = 0; i < assets.size(); ++i) {
            newMenu.addItem(assets[i], NEW_ASSET_BASE + static_cast<Fern::MenuCommandId>(i));
        }
    }
    item(fileMenu, "New Window", "file.newWindow");
    fileMenu.addSeparator();

    item(fileMenu, "Open Project", "file.openProject");
    const std::vector<RecentProject> &recentProjects = m_output->menuBarGetRecentProjectsList();
    bool hasRecentProjects = false;
    for (const RecentProject &project : recentProjects) {
        if (!project.path.empty()) {
            hasRecentProjects = true;
            break;
        }
    }

    if (hasRecentProjects) {
        auto &recentProjectsMenu = fileMenu.addSubmenu("Recent Projects");
        const path_t &openedProjectPath = m_output->menuBarGetOpenedProjectPath();
        for (size_t i = 0; i < recentProjects.size(); ++i) {
            const path_t projectPath = recentProjects[i].path;
            if (projectPath.empty()) { continue; }
            recentProjectsMenu.addItem(recentProjects[i].name, OPEN_RECENT_PROJECT_BASE + static_cast<Fern::MenuCommandId>(i))
                .setChecked(projectPath == openedProjectPath);
        }
    }

    if (hasProject) {
        fileMenu.addSeparator();
        // The document model: Save writes the file of the tab with the focus, Save All every
        // unsaved file.
        item(fileMenu, "Save", "file.save");
        item(fileMenu, "Save All", "file.saveAll");
        fileMenu.addSeparator();
        // Into the folder the Content Browser shows; dropping files onto the window does the same.
        item(fileMenu, "Import Files", "file.import");
        item(fileMenu, "Show C++ Project", "file.showCppProject");
        item(fileMenu, "Close Tab", "tab.close");
        item(fileMenu, "Close Project", "file.closeProject");
        fileMenu.addSeparator();

        item(fileMenu, "Export Game", "export.open"); // a window of its own
    }

    // Settings, the Zed way: the editor settings first, then the project's, then the keymap;
    // "Open" is the settings document, "Open … File" reveals the JSON in the file manager. The
    // documents need the editor chrome, so without a project they are disabled; the files of the
    // editor exist regardless of a project.
    fileMenu.addSeparator();
    auto &settingsMenu = fileMenu.addSubmenu("Settings");
    item(settingsMenu, "Open Settings", "settings.open").setEnabled(hasProject);
    item(settingsMenu, "Open Settings File", "settings.openFile");
    settingsMenu.addSeparator();
    item(settingsMenu, "Open Project Settings", "settings.openProject").setEnabled(hasProject);
    item(settingsMenu, "Open Project Settings File", "settings.openProjectFile").setEnabled(hasProject);
    settingsMenu.addSeparator();
    item(settingsMenu, "Open Keymap", "keymap.open").setEnabled(hasProject);
    item(settingsMenu, "Open Keymap File", "keymap.openFile");

    fileMenu.addSeparator();
    item(fileMenu, "Exit", "app.exit");

    if (hasProject) {
        auto &editMenu = menuBar.addMenu("Edit");
        item(editMenu, "Undo", "edit.undo");
        item(editMenu, "Redo", "edit.redo");

        auto &windowMenu = menuBar.addMenu("Window");
        // The parts of the window: the files on the left, the tree and the inspector of the world
        // tab, the output under the center.
        item(windowMenu, "Files", "window.toggleFiles");
        item(windowMenu, "Hierarchy", "window.toggleHierarchy");
        item(windowMenu, "Inspector", "window.toggleInspector");
        item(windowMenu, "Bottom Panel", "window.toggleBottomPanel");
        windowMenu.addSeparator();
        // The middle of the world tab: the world as the editor shows it, or the frame of the game.
        item(windowMenu, "World", "window.world");
        item(windowMenu, "Game", "window.game");
        windowMenu.addSeparator();
        item(windowMenu, "Plugins", "window.plugins"); // the Plugins tab (themes are applied there)
    }

    // Help, the Zed way: the update check, the restart into a downloaded update while one waits,
    // the release notes of the channel, About.
    auto &helpMenu = menuBar.addMenu("Help");
    item(helpMenu, "Check for Updates", "help.checkForUpdates");
    if (m_output->menuBarUpdateReady()) { item(helpMenu, "Restart to Update", "help.restartToUpdate"); }
    item(helpMenu, "Release Notes", "help.releaseNotes");
    if (hasProject) { // a tab of the editor window: every PandaUI element under the theme in use
        helpMenu.addSeparator();
        item(helpMenu, "UI Gallery", "help.uiGallery");
    }
    helpMenu.addSeparator();
    item(helpMenu, "About", "help.about");

    m_window->setMenuBar(menuBar);
    m_menuBar = std::move(menuBar);
}

namespace {

    void collectDynamicItems(
        const Fern::MenuItem &item, const std::string &menu, const std::string &prefix, std::vector<MenuCommandInfo> &out
    ) {
        for (const Fern::MenuItem &child : item.getChildren()) {
            if (!child.isEnabled()) { continue; }
            if (child.getType() == Fern::MenuItemType::Submenu) {
                collectDynamicItems(child, menu, prefix + child.getTitle() + ": ", out);
            } else if (child.getType() == Fern::MenuItemType::Command && child.getCommandId() >= OPEN_RECENT_PROJECT_BASE) {
                out.push_back({menu, prefix + child.getTitle(), child.getCommandId()});
            }
        }
    }

} // namespace

std::vector<MenuCommandInfo> EditorMenuBar::dynamicItems() const {
    std::vector<MenuCommandInfo> result;
    for (const Fern::MenuItem &menu : m_menuBar.getMenus()) { collectDynamicItems(menu, menu.getTitle(), {}, result); }
    return result;
}

bool EditorMenuBar::handleEvent(Fern::Event *event, bool modalPresented) {
    if (event->type != Fern::EventType::MenuCommand) { return false; }

    const auto *menuEvent = static_cast<const Fern::MenuCommandEvent *>(event);
    // The menu bar is the application's: its commands are the same whichever window of the editor
    // has the keyboard (the Settings window: Save writes the document of its page).
    event->isHandled = true;
    const Fern::MenuCommandId id = menuEvent->commandId;
    const std::string command = id >= 1 && id <= m_commandIds.size() ? m_commandIds[id - 1] : std::string();
    // The keymap editor records a chord: the menu took the key before the UI saw it, so the
    // chord of the item is what was pressed.
    if (m_commands.isCapturing()) {
        if (!command.empty()) { m_commands.captureKeyOf(command); }
        return true;
    }
    if (modalPresented && command != "app.exit") { return true; }
    processCommand(id);
    return true;
}

void EditorMenuBar::processCommand(Fern::MenuCommandId commandId) {
    if (commandId >= 1 && commandId <= m_commandIds.size()) {
        const std::string command = m_commandIds[commandId - 1]; // a copy: the command may rebuild the menu
        m_commands.run(command);
        return;
    }
    const std::vector<RecentProject> &recentProjects = m_output->menuBarGetRecentProjectsList();
    if (commandId >= OPEN_RECENT_PROJECT_BASE && commandId < OPEN_RECENT_PROJECT_BASE + recentProjects.size()) {
        const size_t index = static_cast<size_t>(commandId - OPEN_RECENT_PROJECT_BASE);
        if (!recentProjects[index].path.empty()) { m_output->menuBarOpenProject(recentProjects[index]); }
        return;
    }
    if (commandId >= NEW_ASSET_BASE) {
        m_output->menuBarNewAsset(static_cast<size_t>(commandId - NEW_ASSET_BASE));
        return;
    }
    if (commandId >= NEW_ENTITY_BASE) { m_output->menuBarNewEntity(static_cast<size_t>(commandId - NEW_ENTITY_BASE)); }
}

} // namespace Panda
