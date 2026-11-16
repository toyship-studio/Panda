#pragma once

#include "AssetHandlerEditor.hpp"
#include "Panda/ScriptEngine/ScriptEngine.hpp"
#include "Project/Documents/FileDocuments.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/Settings/ProjectSettings.hpp"
#include "Project/System/ProjectCreator.hpp"

#include <Rain/Coders/JsonEncoder.hpp>

#include <string>
#include <unordered_map>
#include <Rain/Coders/JsonDecoder.hpp>

namespace Panda {

class ProjectLoaderOutput {
public:
    virtual ~ProjectLoaderOutput() = default;
    virtual void loaderDidLoadProject(const std::string &name, const path_t &path) = 0;
    // The open project is about to go — Close Project, another project in this window, the same
    // one read again. Its worlds and its assets are still there: whatever of the editor names
    // them (the selection, the tabs of entities and assets, the previews, the Settings window)
    // lets go now.
    virtual void loaderWillCloseProject() = 0;
    // No project is open any more (Close Project): the start screen.
    virtual void loaderDidLoadCloseProject() = 0;
    // The script build was loaded, or loading it failed: the classes and their fields are the
    // new ones — whoever shows them reads them again.
    virtual void loaderDidReloadScripts() = 0;
};

// The life of a project in the editor (context/architecture.md, "Project model"): opening,
// creating and closing it, its modules, its script build, its shader features, and the two
// sessions — `session.json` of the editor in the user data directory (the recent projects, the
// window, the dock layout) and `.Panda/session.json` of the project (the open worlds). The files
// the editor holds in memory — the settings of the editor and of the project, the worlds — are
// documents of FileDocuments: the loader opens and closes them with the project.
class ProjectLoader final {
public:
    ProjectLoader(FileDocuments &documents, ProjectLoaderOutput *output);
    ~ProjectLoader();
    // settings.json and the editor session, then the project of this instance: `project` when
    // the command line named one, else — openLastProject — the project the last session ended
    // with (a second window starts on the start screen instead).
    void loadInitialData(bool openLastProject, const path_t &project);
    // The exit, while the editor still answers (EditorLayer::onDetach): the files nobody asked to
    // write — the sessions, and the settings unless they were changed on disk.
    void saveOnExit();
    // The editor session (session.json): written at project open/close and at exit.
    void saveSession();
    void saveWindowState(LastOpenedProjectWindowState state);
    void openProject(const path_t &path);
    void reloadScriptsDll();
    // The script build on disk is newer than the loaded one (a step of Reload; the build is not
    // a document — nothing edits it in memory).
    bool isScriptsChangedOnDisk();
    // The source file a script class is registered in (REGISTER_SCRIPT(Name)), found in the
    // Scripts folder of the project; empty — no file registers it. The sources are read once and
    // again after the script build is loaded: the files may have moved by then.
    path_t scriptSource(const std::string &className);
    void closeProject();
    void createProject(const std::string &name, const path_t &path);
    const std::vector<RecentProject> &getRecentProjectsList();
    bool hasOpenedProject();
    const path_t &getOpenedProjectPath();
    void removeRecentProject(int index);
    // project.json was written or reread: the engine follows its shader features (a changed set
    // recompiles the scene shaders and the loaded project shaders).
    void applyProjectSettings();
    LastOpenedProjectWindowState getLastWindowState();
    void saveDockLayout(DockLayoutState state); // the docking layout is part of the editor session
    const DockLayoutState &getDockLayout();

private:
    const path_t &projectPath() const {
        return m_documents.projectPath();
    }
    ScriptEngineConfig makeScriptsConfig();
    // The files of the open project nobody asked to write (a switch, closing, the exit):
    // project.json, unless it was changed on disk, and the session of the project.
    void saveProject();
    // The open project goes: it is saved, the editor is told (loaderWillCloseProject), then its
    // worlds are released, then the registry of its assets.
    void releaseOpenedProject();
    void loadRecentProject();
    void appendRecentProject();
    void finalizeOpenedProject();
    // The shader features of the "rendering" section → the engine (ShaderFeatures). true — the set
    // changed: the scene shaders of the engine and the loaded project shaders must recompile.
    bool applyShaderFeatures();

    FileDocuments &m_documents;
    ProjectLoaderOutput *m_output;
    Rain::JsonEncoder m_jsonEncoder;
    Rain::JsonDecoder m_jsonDecoder;
    std::filesystem::file_time_type m_scriptsDllTime{};
    // Script class → the source file that registers it (scriptSource); read on the first question.
    std::unordered_map<std::string, path_t> m_scriptSources;
    bool m_scriptSourcesRead = false;
    EditorSession m_session;
    // What the user took out of the recent projects in this instance: the session file other
    // instances share still names them until this one writes it, and they are not taken back.
    std::vector<RecentProject> m_forgottenProjects;
    ProjectSession m_projectSession;
    ProjectCreator m_projectCreator;
    ScriptEngine m_scriptEngine;
    AssetHandlerEditor *m_assetHandler;
};

} // namespace Panda
