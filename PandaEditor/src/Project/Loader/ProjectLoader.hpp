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

class ProjectLoader;

// A world the project has open: the world and the `.world` file it is the document of (the
// document model, context/architecture.md). The editor keeps any number of them open, a tab
// each; one is active (ProjectLoader::activeWorld) — shown, edited and played.
class WorldFile final : public FileDocument {
public:
    WorldFile(ProjectLoader &loader, uint32_t id);

    // Stable while the world is open: the key of its tab.
    uint32_t id() const {
        return m_id;
    }
    World &world() {
        return m_world;
    }
    // The file as a tab shows it; "Untitled World" for a world without one.
    std::string name() const;

    std::string title() const override;
    path_t path() const override {
        return m_path;
    }
    bool isDirty() const override {
        return m_world.isChanged();
    }
    bool isChangedOnDisk() const override {
        return m_stamp.isStale(m_path);
    }
    bool save() override;   // Save, or Save As for a world without a file
    bool reload() override; // the same file again

private:
    friend class ProjectLoader;

    ProjectLoader &m_loader;
    uint32_t m_id;
    World m_world;
    path_t m_path;
    DiskStamp m_stamp;             // the file as the editor last read or wrote it
    EditorCameraSettings m_camera; // where the editor camera stood in this world
    bool m_hasCamera = false;      // false — the world was not shown yet: the camera stays where it is
};

class ProjectLoaderOutput {
public:
    virtual ~ProjectLoaderOutput() = default;
    virtual void loaderDidLoadProject(const std::string &name, const path_t &path) = 0;
    virtual void loaderDidLoadCloseProject() = 0;
    // Another world became the active one: the editor shows it from now on.
    virtual void loaderDidActivateWorld() = 0;
    // The active world was read from a file (opened into an untouched new world, reloaded), got
    // its starter content, or its file changed (Save As).
    virtual void loaderDidLoadWorld() = 0;
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
// - the project session: `<project>/.Panda/session.json` — the open worlds and the camera in each.
class ProjectLoader final {
public:
    explicit ProjectLoader(ProjectLoaderOutput *output);
    ~ProjectLoader();
    // The editor settings and the session; openLastProject — the project the last session ended
    // with is opened again (a second window starts on the start screen instead).
    void loadInitialData(bool openLastProject = true);
    // The editor session (session.json): written at project open/close and at exit.
    void saveSession();
    void saveWindowState(LastOpenedProjectWindowState state);
    // The files the loader holds in memory, as file documents (FileDocument.hpp): the open
    // worlds, project.json and settings.json. Save, Reload and the unsaved-changes questions go
    // through them (EditorLayer).
    //
    // The worlds the project has open, in the order of their tabs. One is active: the editor
    // shows and edits it, Play plays its copy. There is always one — without a project, an
    // empty world.
    const std::vector<std::unique_ptr<WorldFile>> &worlds() const {
        return m_worlds;
    }
    WorldFile &activeWorld() {
        return *m_activeWorld;
    }
    WorldFile *findWorld(uint32_t id);
    WorldFile *findWorld(const path_t &path); // the open world of this file
    bool isWorldFile(const FileDocument *file) const;
    // The editor camera stays with the world it was looking at; the new active world brings its own.
    void activateWorld(WorldFile &world);
    // A world file in a tab of its own: an open one is activated, another one is read into a new
    // document. An untouched new world gives its place to the file. nullptr — the file could not
    // be read; nothing changes then.
    WorldFile *openWorld(const path_t &path);
    // A new world with the starter content, active.
    WorldFile &newWorld();
    // Drops the world from memory (the unsaved-changes question is the caller's). The last one
    // leaves a new world behind: the editor always has a world to show.
    void closeWorld(WorldFile &world);
    bool saveWorld(WorldFile &world);   // false — nothing was written (an error, a cancelled Save As)
    bool reloadWorld(WorldFile &world); // file → memory; false — the file is broken or gone
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
    void closeProject();
    void createProject(const std::string &name, const path_t &path);
    const std::vector<RecentProject> &getRecentProjectsList();
    bool hasOpenedProject();
    const path_t &getOpenedProjectPath();
    const path_t &getOpenedWorldPath(); // the file of the active world; empty — it has none yet
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
    class ProjectSettingsFile;
    class EditorSettingsFile;
    // The file → memory side of the two settings documents (FileDocument::reload).
    bool reloadProjectSettings();
    bool reloadEditorSettings();
    ScriptEngineConfig makeScriptsConfig();
    // A new world document at the end of the list (not active): empty, or with the starter content.
    WorldFile &addWorld(bool starterContent);
    // The document of the file: the open one, the untouched new active world, or a new document.
    // Nothing is activated. nullptr — the file could not be read.
    WorldFile *loadWorld(const path_t &path);
    bool readWorld(WorldFile &world, const path_t &path); // file → the document
    bool saveWorldAs(WorldFile &world);
    bool writeWorld(WorldFile &world, const path_t &path);
    void registerWorldAsset(const path_t &path);
    // The world becomes the active one and brings its camera (the previous one is not asked).
    void showWorld(WorldFile &world);
    // Every open world goes; one empty world stays, active.
    void resetWorlds();
    // The worlds of the last session, else the start world of the project, else a new world.
    void openSessionWorlds();
    // The start world of the project, when it has none yet: the active world, once it has a file.
    void syncWorldPaths();
    // The open worlds and their cameras into the project session.
    void collectSession();
    std::string projectRelative(const path_t &path) const; // forward slashes; empty — outside the project
    void loadRecentProject();
    void appendRecentProject();
    void finalizeOpenedProject();
    // The shader features of the "rendering" section → the engine (ShaderFeatures). true — the set
    // changed: the scene shaders of the engine and the loaded project shaders must recompile.
    bool applyShaderFeatures();

    Rain::JsonEncoder m_jsonEncoder;
    Rain::JsonDecoder m_jsonDecoder;
    // The files as the editor last read or wrote them (the Reload contract).
    DiskStamp m_projectSettingsStamp;
    DiskStamp m_editorSettingsStamp;
    std::vector<std::unique_ptr<WorldFile>> m_worlds; // the open worlds, in the order of their tabs
    WorldFile *m_activeWorld = nullptr;               // one of m_worlds, always
    uint32_t m_nextWorldId = 1;
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
    ProjectLoaderOutput *m_output;
    ScriptEngine m_scriptEngine;
    AssetHandlerEditor *m_assetHandler;
};

} // namespace Panda
