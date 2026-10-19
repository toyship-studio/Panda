#include "EditorMenuBar.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Fern/Events/Event.hpp>
#include <Fern/Events/MenuEvents.hpp>
#include <Fern/Window/Window.hpp>

namespace {

namespace EditorMenuCommand {
    constexpr Fern::MenuCommandId NewWorld = 1;
    constexpr Fern::MenuCommandId SaveWorld = 2;
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
    constexpr Fern::MenuCommandId OpenRecentProjectBase = 1000;
    constexpr Fern::MenuCommandId SelectThemeBase = 2000;
} // namespace EditorMenuCommand

Fern::MenuShortcut primaryShortcut(Fern::Key key) {
    return {key, Fern::MenuShortcut::Primary};
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
    if (hasProject) {
        fileMenu.addItem("New World", EditorMenuCommand::NewWorld, primaryShortcut(Fern::Key::KEY_N));
        fileMenu.addItem("Save World", EditorMenuCommand::SaveWorld, primaryShortcut(Fern::Key::KEY_S));
        // Synchronization of the engine with the disk (changed assets/scripts/world) — closes
        // the loop of external edits (agent, code editor, git) without a restart.
        fileMenu.addItem("Reload", EditorMenuCommand::Reload, primaryShortcut(Fern::Key::KEY_R));
    }
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
    settingsMenu.addItem("Show Settings File", EditorMenuCommand::ShowEditorSettingsFile);
    settingsMenu.addSeparator();
    settingsMenu.addItem("Open Project Settings", EditorMenuCommand::ProjectSettings).setEnabled(hasProject);
    settingsMenu.addItem("Show Project Settings File", EditorMenuCommand::ShowProjectSettingsFile).setEnabled(hasProject);

    fileMenu.addSeparator();
    fileMenu.addItem("Exit", EditorMenuCommand::Exit, primaryShortcut(Fern::Key::KEY_Q));

    if (hasProject) {
        auto &editMenu = menuBar.addMenu("Edit");
        editMenu.addItem("Undo", EditorMenuCommand::Undo, primaryShortcut(Fern::Key::KEY_Z));
        editMenu.addItem("Redo", EditorMenuCommand::Redo, primaryShiftShortcut(Fern::Key::KEY_Z));

        auto &windowMenu = menuBar.addMenu("Window");
        windowMenu.addItem("World Settings", EditorMenuCommand::WorldSettings); // the viewport sidebar
    }

    // Themes are data plugins (Project/Plugins/EditorPlugins): the host holds the list and the current one.
    const std::vector<std::string> themeNames = m_output->menuBarThemeNames();
    if (!themeNames.empty()) {
        auto &viewMenu = menuBar.addMenu("View");
        auto &themeMenu = viewMenu.addSubmenu("Theme");
        const int current = m_output->menuBarCurrentTheme();
        for (size_t i = 0; i < themeNames.size(); ++i) {
            themeMenu.addItem(themeNames[i], EditorMenuCommand::SelectThemeBase + static_cast<Fern::MenuCommandId>(i))
                .setChecked(static_cast<int>(i) == current);
        }
    }

    auto &helpMenu = menuBar.addMenu("Help");
    helpMenu.addItem("About", EditorMenuCommand::About);

    m_window->setMenuBar(menuBar);
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
    if (commandId >= EditorMenuCommand::SelectThemeBase) {
        m_output->menuBarSelectTheme(static_cast<int>(commandId - EditorMenuCommand::SelectThemeBase));
        return;
    }
    if (commandId >= EditorMenuCommand::OpenRecentProjectBase &&
        commandId < EditorMenuCommand::OpenRecentProjectBase + recentProjects.size()) {
        const size_t index = static_cast<size_t>(commandId - EditorMenuCommand::OpenRecentProjectBase);
        if (!recentProjects[index].path.empty()) { m_output->menuBarOpenProject(recentProjects[index]); }
        return;
    }

    switch (commandId) {
        case EditorMenuCommand::NewWorld:
            m_output->menuBarNewWorld();
            break;
        case EditorMenuCommand::SaveWorld:
            m_output->menuBarSaveWorld();
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
        default:
            break;
    }
}

} // namespace Panda
