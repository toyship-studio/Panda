#pragma once

#include "AssetHandlerEditor.hpp"
#include "Panda/Base/TypeContext.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/ScriptEngine/ScriptEngine.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/System/ProjectCreator.hpp"
#include "Project/Settings/ProjectSettings.hpp"

#include <Rain/Coders/JsonEncoder.hpp>
#include <Rain/Coders/JsonDecoder.hpp>

namespace Panda {

class ProjectLoaderOutput {
public:
    virtual ~ProjectLoaderOutput() = default;
    virtual void loaderDidLoadProject(const std::string &name, const path_t &path) = 0;
    virtual void loaderDidLoadCloseProject() = 0;
    virtual void loaderDidLoadWorld() = 0;
    virtual void loaderDidCreateNewWorld() = 0;
    virtual EditorCameraSettings getEditorCameraSettings() = 0;
    virtual void setEditorCameraSettings(EditorCameraSettings settings) = 0;
};

// The files of the editor and of the project (context/architecture.md, "Project model"):
// - the editor scope: `settings.json` in the user data directory — the sections of the settings
//   registry (editorSettings), applied at once and written on every change (saveEditorSettings);
// - the editor session: `session.json` next to it — recent projects, the window, the dock layout;
// - the project scope: `<project>/.Panda/project.json` — the sections of the registry
//   (projectSettings), in version control;
// - the project session: `<project>/.Panda/session.json` — the last world and the camera.
class ProjectLoader final {
public:
    ProjectLoader(World *world, ProjectLoaderOutput *output);
    ~ProjectLoader();
    void loadInitialData();
    // The editor session (session.json): written at project open/close and at exit.
    void saveSession();
    void saveWindowState(LastOpenedProjectWindowState state);
    // The sections of the project scope and the project session (project.json, session.json);
    // false — project.json could not be written.
    bool saveProjectSettings();
    // The project scope rereads project.json (the Don't Save of the project settings document).
    void reloadProjectSettings();
    void openProject(const path_t &path);
    void openCppProject();
    void showCppProject();
    void reloadScriptsDll();
    // The world/scripts have "overtaken" the engine on disk (mtime is newer than the moment of
    // load/save) — sources for the unified Reload synchronization (context/architecture.md).
    bool isWorldChangedOnDisk();
    bool isScriptsChangedOnDisk();
    void newWorld();
    bool openWorld(const path_t &path);
    bool saveWorld();
    void closeProject();
    void createProject(const std::string &name, const path_t &path);
    const std::vector<RecentProject> &getRecentProjectsList();
    bool hasOpenedProject();
    const path_t &getOpenedProjectPath();
    const path_t &getOpenedWorldPath();
    void removeRecentProject(int index);
    // The sections of the project scope (ProjectWorldSettings, ProjectModuleSettings, the
    // sections of the modules): read by type, written as a whole by saveProjectSettings.
    TypeContext &projectSettings() {
        return m_projectSettings;
    }
    // The sections of the editor scope (EditorViewportSettings, EditorAppearanceSettings, the
    // sections of the modules); a change is followed by saveEditorSettings.
    TypeContext &editorSettings() {
        return m_editorSettings;
    }
    void saveEditorSettings();
    // The files of the two scopes: settings.json of the user data directory (empty — no such
    // directory) and .Panda/project.json of the open project (empty — no project).
    path_t editorSettingsPath() const;
    path_t projectSettingsPath() const;
    LastOpenedProjectWindowState getLastWindowState();
    void saveDockLayout(DockLayoutState state); // the docking layout is part of the editor session
    const DockLayoutState &getDockLayout();

private:
    bool loadWorld(const path_t &path);
    ScriptEngineConfig makeScriptsConfig();
    bool saveWorldAs();
    bool writeWorld(const path_t &path);
    void registerCurrentWorldAsset();
    void createDefaultWorld();
    // The open world into the project files: the session's last world, and the start world of
    // the project when it has none yet.
    void syncWorldPaths();
    void loadRecentProject();
    void appendRecentProject();
    void finalizeOpenedProject();

    Rain::JsonEncoder m_jsonEncoder;
    Rain::JsonDecoder m_jsonDecoder;
    // mtime at the moment of the editor's last synchronization with the disk (the Reload contract).
    std::filesystem::file_time_type m_worldDiskTime{};
    std::filesystem::file_time_type m_scriptsDllTime{};
    TypeContext m_editorSettings;
    EditorSession m_session;
    TypeContext m_projectSettings;
    ProjectSession m_projectSession;
    ProjectCreator m_projectCreator;
    path_t m_projectPath;
    path_t m_worldPath;
    World *m_world;
    ProjectLoaderOutput *m_output;
    ScriptEngine m_scriptEngine;
    AssetHandlerEditor *m_assetHandler;
};

} // namespace Panda
