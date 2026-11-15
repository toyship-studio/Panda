#include "EditorMenuBar.hpp"

#include "UI/Shortcuts.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Fern/Events/Event.hpp>
#include <Fern/Events/MenuEvents.hpp>
#include <Fern/Window/Window.hpp>

namespace {

namespace EditorMenuCommand {
    constexpr Fern::MenuCommandId NewWorld = 1;
    constexpr Fern::MenuCommandId Save = 2;
    constexpr Fern::MenuCommandId OpenProject = 3;
    constexpr Fern::MenuCommandId ShowCppProject = 4;
    constexpr Fern::MenuCommandId CloseProject = 5;
    constexpr Fern::MenuCommandId ExportGame = 6;
    constexpr Fern::MenuCommandId OpenExportXcodeMacOS = 7;
    constexpr Fern::MenuCommandId OpenExportXcodeIOSSimulator = 8;
    constexpr Fern::MenuCommandId OpenExportXcodeIOSDevice = 9;
    constexpr Fern::MenuCommandId OpenExportVisualStudioWindows = 10;
    constexpr Fern::MenuCommandId Exit = 11;
    constexpr Fern::MenuCommandId Undo = 12;
    constexpr Fern::MenuCommandId Redo = 13;
    constexpr Fern::MenuCommandId WorldSettings = 14;
    constexpr Fern::MenuCommandId About = 15;
    constexpr Fern::MenuCommandId Reload = 16;
    constexpr Fern::MenuCommandId ProjectSettings = 17;
    constexpr Fern::MenuCommandId EditorSettings = 18;
    constexpr Fern::MenuCommandId ShowEditorSettingsFile = 19;
    constexpr Fern::MenuCommandId ShowProjectSettingsFile = 20;
    constexpr Fern::MenuCommandId CheckForUpdates = 21;
    constexpr Fern::MenuCommandId RestartToUpdate = 22;
    constexpr Fern::MenuCommandId ReleaseNotes = 23;
    constexpr Fern::MenuCommandId Plugins = 24;
    constexpr Fern::MenuCommandId UiGallery = 25;
    constexpr Fern::MenuCommandId GameView = 26;
    constexpr Fern::MenuCommandId SaveAll = 27;
    constexpr Fern::MenuCommandId NewWindow = 28;
    constexpr Fern::MenuCommandId OpenRecentProjectBase = 1000;
    constexpr Fern::MenuCommandId NewEntityBase = 2000;
    constexpr Fern::MenuCommandId NewAssetBase = 3000;
} // namespace EditorMenuCommand

Fern::MenuShortcut primaryShortcut(Fern::Key key) {
    return {key, Fern::MenuShortcut::Primary};
}

Fern::MenuShortcut primaryAltShortcut(Fern::Key key) {
    return {key, static_cast<Fern::MenuModifiers>(Fern::MenuShortcut::Primary | Fern::MenuShortcut::Alt)};
}

Fern::MenuShortcut primaryShiftShortcut(Fern::Key key) {
    return {key, static_cast<Fern::MenuModifiers>(Fern::MenuShortcut::Primary | Fern::MenuShortcut::Shift)};
}

} // namespace

namespace Panda {

EditorMenuBar::EditorMenuBar(Fern::Window *window, EditorMenuBarOutput *output)
    : m_window(window)
    , m_output(output) {}

void EditorMenuBar::rebuild() {
    const bool hasProject = m_output->menuBarHasOpenedProject();
    Fern::MenuBar menuBar;

    auto &fileMenu = menuBar.addMenu("File");
    // The first section is what begins something: New lists everything the project can create
    // (a world, the entities of the open world, the assets), New Window starts another instance
    // of the editor — one project per window.
    if (hasProject) {
        auto &newMenu = fileMenu.addSubmenu("New");
        newMenu.addItem("World", EditorMenuCommand::NewWorld, primaryShortcut(Fern::Key::KEY_N));
        const std::vector<std::string> entities = m_output->menuBarNewEntityTitles();
        if (!entities.empty()) { newMenu.addSeparator(); }
        for (size_t i = 0; i < entities.size(); ++i) {
            newMenu.addItem(entities[i], EditorMenuCommand::NewEntityBase + static_cast<Fern::MenuCommandId>(i));
        }
        const std::vector<std::string> assets = m_output->menuBarNewAssetTitles();
        if (!assets.empty()) { newMenu.addSeparator(); }
        for (size_t i = 0; i < assets.size(); ++i) {
            newMenu.addItem(assets[i], EditorMenuCommand::NewAssetBase + static_cast<Fern::MenuCommandId>(i));
        }
    }
    fileMenu.addItem("New Window", EditorMenuCommand::NewWindow, primaryShiftShortcut(Fern::Key::KEY_N));
    fileMenu.addSeparator();

    fileMenu.addItem("Open Project", EditorMenuCommand::OpenProject, primaryShortcut(Fern::Key::KEY_O));
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

            std::string projectName = projectPath.filename().string();
            if (projectName.empty()) { projectName = projectPath.string(); }
            recentProjectsMenu
                .addItem(projectName, EditorMenuCommand::OpenRecentProjectBase + static_cast<Fern::MenuCommandId>(i))
                .setChecked(projectPath == openedProjectPath);
        }
    }

    if (hasProject) {
        fileMenu.addSeparator();
        // The document model: Save writes the file of the tab with the focus, Save All every
        // unsaved file (the shortcuts of Zed).
        fileMenu.addItem("Save", EditorMenuCommand::Save, primaryShortcut(Fern::Key::KEY_S));
        fileMenu.addItem("Save All", EditorMenuCommand::SaveAll, primaryAltShortcut(Fern::Key::KEY_S));
        // Synchronization of the engine with the disk (changed assets/scripts/world) — closes
        // the loop of external edits (agent, code editor, git) without a restart.
        fileMenu.addItem("Reload", EditorMenuCommand::Reload, primaryShortcut(Fern::Key::KEY_R));
        fileMenu.addSeparator();
#ifdef PLATFORM_MACOS
        fileMenu.addItem("Show C++ Project", EditorMenuCommand::ShowCppProject);
#endif
        fileMenu.addItem("Close Project", EditorMenuCommand::CloseProject);
        fileMenu.addSeparator();

        auto &exportMenu = fileMenu.addSubmenu("Export Game");
        exportMenu.addItem("Cook Runtime Data", EditorMenuCommand::ExportGame);
#ifdef PLATFORM_MACOS
        auto &xcodeMenu = exportMenu.addSubmenu("Open Xcode");
        xcodeMenu.addItem("macOS", EditorMenuCommand::OpenExportXcodeMacOS);
        xcodeMenu.addItem("iOS Simulator", EditorMenuCommand::OpenExportXcodeIOSSimulator);
        xcodeMenu.addItem("iOS Device", EditorMenuCommand::OpenExportXcodeIOSDevice);
#endif
#ifdef PLATFORM_WINDOWS
        exportMenu.addItem("Open Visual Studio", EditorMenuCommand::OpenExportVisualStudioWindows);
#endif
    }

    // Settings, the Zed way: the editor settings first, then the project's; "Open" is the
    // settings document, "Show … File" reveals the JSON in the file manager. The documents need
    // the editor chrome, so without a project they are disabled; the editor settings file exists
    // regardless of a project.
    fileMenu.addSeparator();
    auto &settingsMenu = fileMenu.addSubmenu("Settings");
    settingsMenu.addItem("Open Settings", EditorMenuCommand::EditorSettings, primaryShortcut(Fern::Key::KEY_COMMA))
        .setEnabled(hasProject);
    settingsMenu.addItem("Open Settings File", EditorMenuCommand::ShowEditorSettingsFile);
    settingsMenu.addSeparator();
    settingsMenu.addItem("Open Project Settings", EditorMenuCommand::ProjectSettings).setEnabled(hasProject);
    settingsMenu.addItem("Open Project Settings File", EditorMenuCommand::ShowProjectSettingsFile).setEnabled(hasProject);

    fileMenu.addSeparator();
    fileMenu.addItem("Exit", EditorMenuCommand::Exit, primaryShortcut(Fern::Key::KEY_Q));

    if (hasProject) {
        auto &editMenu = menuBar.addMenu("Edit");
        editMenu.addItem("Undo", EditorMenuCommand::Undo, primaryShortcut(Fern::Key::KEY_Z));
        editMenu.addItem("Redo", EditorMenuCommand::Redo, primaryShiftShortcut(Fern::Key::KEY_Z));

        auto &windowMenu = menuBar.addMenu("Window");
        windowMenu.addItem("World Settings", EditorMenuCommand::WorldSettings); // the viewport sidebar
        windowMenu.addItem("Game", EditorMenuCommand::GameView); // the Game tab: the frame of the game
        windowMenu.addSeparator();
        windowMenu.addItem("Plugins", EditorMenuCommand::Plugins); // the Plugins tab (themes are applied there)
    }

    // Help, the Zed way: the update check, the restart into a downloaded update while one waits,
    // the release notes of the channel, About.
    auto &helpMenu = menuBar.addMenu("Help");
    helpMenu.addItem("Check for Updates", EditorMenuCommand::CheckForUpdates);
    if (m_output->menuBarUpdateReady()) { helpMenu.addItem("Restart to Update", EditorMenuCommand::RestartToUpdate); }
    helpMenu.addItem("Release Notes", EditorMenuCommand::ReleaseNotes);
    if (hasProject) { // a tab of the editor window: every PandaUI element under the theme in use
        helpMenu.addSeparator();
        helpMenu.addItem("UI Gallery", EditorMenuCommand::UiGallery);
    }
    helpMenu.addSeparator();
    helpMenu.addItem("About", EditorMenuCommand::About);

    m_window->setMenuBar(menuBar);
    m_menuBar = std::move(menuBar);
}

namespace {

    // The keys of a menu item the way the palette writes the keys of every command.
    std::string shortcutText(Fern::MenuShortcut shortcut) {
        if (!shortcut.isValid()) { return {}; }
        const bool primary = (shortcut.modifiers & Fern::MenuShortcut::Primary) != 0;
        Editor::KeyChord chord = primary ? Editor::KeyChord::primary(static_cast<PandaUI::Key>(static_cast<int>(shortcut.key)))
                                         : Editor::KeyChord::plain(static_cast<PandaUI::Key>(static_cast<int>(shortcut.key)));
        chord.shift = (shortcut.modifiers & Fern::MenuShortcut::Shift) != 0;
        chord.alt = (shortcut.modifiers & Fern::MenuShortcut::Alt) != 0;
        if ((shortcut.modifiers & Fern::MenuShortcut::Control) != 0) { chord.ctrl = true; }
        if ((shortcut.modifiers & Fern::MenuShortcut::Super) != 0) { chord.super = true; }
        return chord.text();
    }

    void collectCommands(
        const Fern::MenuItem &item, const std::string &menu, const std::string &prefix, std::vector<MenuCommandInfo> &out
    ) {
        for (const Fern::MenuItem &child : item.getChildren()) {
            if (!child.isEnabled()) { continue; }
            if (child.getType() == Fern::MenuItemType::Submenu) {
                collectCommands(child, menu, prefix + child.getTitle() + ": ", out);
            } else if (child.getType() == Fern::MenuItemType::Command) {
                out.push_back({menu, prefix + child.getTitle(), shortcutText(child.getShortcut()), child.getCommandId()});
            }
        }
    }

} // namespace

std::vector<MenuCommandInfo> EditorMenuBar::commands() const {
    std::vector<MenuCommandInfo> result;
    for (const Fern::MenuItem &menu : m_menuBar.getMenus()) { collectCommands(menu, menu.getTitle(), {}, result); }
    return result;
}

bool EditorMenuBar::handleEvent(Fern::Event *event, bool modalPresented) {
    if (event->type != Fern::EventType::MenuCommand) { return false; }

    const auto *menuEvent = static_cast<const Fern::MenuCommandEvent *>(event);
    if (event->getWindow() != nullptr && event->getWindow() != m_window) { return false; }

    event->isHandled = true;
    if (modalPresented && menuEvent->commandId != EditorMenuCommand::Exit) { return true; }
    processCommand(menuEvent->commandId);
    return true;
}

void EditorMenuBar::processCommand(Fern::MenuCommandId commandId) {
    const std::vector<RecentProject> &recentProjects = m_output->menuBarGetRecentProjectsList();
    if (commandId >= EditorMenuCommand::OpenRecentProjectBase &&
        commandId < EditorMenuCommand::OpenRecentProjectBase + recentProjects.size()) {
        const size_t index = static_cast<size_t>(commandId - EditorMenuCommand::OpenRecentProjectBase);
        if (!recentProjects[index].path.empty()) { m_output->menuBarOpenProject(recentProjects[index]); }
        return;
    }
    if (commandId >= EditorMenuCommand::NewAssetBase) {
        m_output->menuBarNewAsset(static_cast<size_t>(commandId - EditorMenuCommand::NewAssetBase));
        return;
    }
    if (commandId >= EditorMenuCommand::NewEntityBase) {
        m_output->menuBarNewEntity(static_cast<size_t>(commandId - EditorMenuCommand::NewEntityBase));
        return;
    }

    switch (commandId) {
        case EditorMenuCommand::NewWorld:
            m_output->menuBarNewWorld();
            break;
        case EditorMenuCommand::Save:
            m_output->menuBarSave();
            break;
        case EditorMenuCommand::SaveAll:
            m_output->menuBarSaveAll();
            break;
        case EditorMenuCommand::NewWindow:
            m_output->menuBarNewWindow();
            break;
        case EditorMenuCommand::Reload:
            m_output->menuBarReload();
            break;
        case EditorMenuCommand::OpenProject:
            m_output->menuBarOpenProject();
            break;
        case EditorMenuCommand::ShowCppProject:
            m_output->menuBarShowCppProject();
            break;
        case EditorMenuCommand::CloseProject:
            m_output->menuBarCloseProject();
            break;
        case EditorMenuCommand::ExportGame:
            m_output->menuBarExportGame();
            break;
        case EditorMenuCommand::OpenExportXcodeMacOS:
            m_output->menuBarOpenExportProject(ExportProjectTarget::XcodeMacOS);
            break;
        case EditorMenuCommand::OpenExportXcodeIOSSimulator:
            m_output->menuBarOpenExportProject(ExportProjectTarget::XcodeIOSSimulator);
            break;
        case EditorMenuCommand::OpenExportXcodeIOSDevice:
            m_output->menuBarOpenExportProject(ExportProjectTarget::XcodeIOSDevice);
            break;
        case EditorMenuCommand::OpenExportVisualStudioWindows:
            m_output->menuBarOpenExportProject(ExportProjectTarget::VisualStudioWindows);
            break;
        case EditorMenuCommand::Exit:
            m_output->menuBarCloseApp();
            break;
        case EditorMenuCommand::Undo:
            m_output->menuBarUndo();
            break;
        case EditorMenuCommand::Redo:
            m_output->menuBarRedo();
            break;
        case EditorMenuCommand::WorldSettings:
            m_output->menuBarOpenWorldSettings();
            break;
        case EditorMenuCommand::ProjectSettings:
            m_output->menuBarOpenProjectSettings();
            break;
        case EditorMenuCommand::EditorSettings:
            m_output->menuBarOpenEditorSettings();
            break;
        case EditorMenuCommand::ShowEditorSettingsFile:
            m_output->menuBarShowEditorSettingsFile();
            break;
        case EditorMenuCommand::ShowProjectSettingsFile:
            m_output->menuBarShowProjectSettingsFile();
            break;
        case EditorMenuCommand::About:
            m_output->menuBarAbout();
            break;
        case EditorMenuCommand::CheckForUpdates:
            m_output->menuBarCheckForUpdates();
            break;
        case EditorMenuCommand::RestartToUpdate:
            m_output->menuBarRestartToUpdate();
            break;
        case EditorMenuCommand::ReleaseNotes:
            m_output->menuBarViewReleaseNotes();
            break;
        case EditorMenuCommand::Plugins:
            m_output->menuBarOpenPlugins();
            break;
        case EditorMenuCommand::UiGallery:
            m_output->menuBarOpenUiGallery();
            break;
        case EditorMenuCommand::GameView:
            m_output->menuBarOpenGameView();
            break;
        default:
            break;
    }
}

} // namespace Panda
