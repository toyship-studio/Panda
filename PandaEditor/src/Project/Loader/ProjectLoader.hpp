#pragma once

#include "AssetHandlerEditor.hpp"
#include "Panda/Base/TypeContext.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/ScriptEngine/ScriptEngine.hpp"
#include "Project/Documents/FileDocument.hpp"
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
    // settings.json was reread: the editor applies the scope (it is not saved back).
    virtual void loaderDidReloadEditorSettings() = 0;
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
    // The editor settings and the session; openLastProject — the project the last session ended
    // with is opened again (a second window starts on the start screen instead).
    void loadInitialData(bool openLastProject = true);
    // The editor session (session.json): written at project open/close and at exit.
    void saveSession();
    void saveWindowState(LastOpenedProjectWindowState state);
    // The files the loader holds in memory, as file documents (FileDocument.hpp): the open world,
    // project.json and settings.json. Save, Reload and the unsaved-changes questions go through
    // them (EditorLayer).
    FileDocument &worldFile() {
        return *m_worldFile;
    }
    FileDocument &projectSettingsFile() {
        return *m_projectSettingsFile;
    }
    FileDocument &editorSettingsFile() {
        return *m_editorSettingsFile;
    }
    // An edit went into the project scope (the Project Settings tab): project.json is behind.
    void markProjectSettingsEdited() {
        m_projectSettingsDirty = true;
    }
    // The sections of the project scope and the project session (project.json, session.json);
    // false — project.json could not be written.
    // overwriteDiskEdits = false — a save nobody asked for (closing or switching the project):
    // a project.json changed on disk since the editor read it is left alone.
    bool saveProjectSettings(bool overwriteDiskEdits = true);
    void openProject(const path_t &path);
    void openCppProject();
    void showCppProject();
    void reloadScriptsDll();
    // The script build on disk is newer than the loaded one (a step of Reload; the build is not
    // a document — nothing edits it in memory).
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
    // overwriteDiskEdits = false — a save nobody asked for (exit): a settings.json changed on
    // disk since the editor read it is left alone.
    void saveEditorSettings(bool overwriteDiskEdits = true);
    // The files of the two scopes: settings.json of the user data directory (empty — no such
    // directory) and .Panda/project.json of the open project (empty — no project).
    path_t editorSettingsPath() const;
    path_t projectSettingsPath() const;
    LastOpenedProjectWindowState getLastWindowState();
    void saveDockLayout(DockLayoutState state); // the docking layout is part of the editor session
    const DockLayoutState &getDockLayout();

private:
    class WorldFile;
    class ProjectSettingsFile;
    class EditorSettingsFile;
    // The file → memory side of the two settings documents (FileDocument::reload).
    bool reloadProjectSettings();
    bool reloadEditorSettings();
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
    // The shader features of the "rendering" section → the engine (ShaderFeatures). true — the set
    // changed: the scene shaders of the engine and the loaded project shaders must recompile.
    bool applyShaderFeatures();

    Rain::JsonEncoder m_jsonEncoder;
    Rain::JsonDecoder m_jsonDecoder;
    // The files as the editor last read or wrote them (the Reload contract).
    DiskStamp m_worldStamp;
    DiskStamp m_projectSettingsStamp;
    DiskStamp m_editorSettingsStamp;
    std::unique_ptr<FileDocument> m_worldFile;
    std::unique_ptr<FileDocument> m_projectSettingsFile;
    std::unique_ptr<FileDocument> m_editorSettingsFile;
    bool m_projectSettingsDirty = false; // the project scope has edits project.json does not
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
