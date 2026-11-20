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
    // with (a second window starts on the start screen instead). → why that folder was not
    // opened (it is not a project: openProject); empty — it was, or none was asked for.
    std::string loadInitialData(bool openLastProject, const path_t &project);
    // The exit, while the editor still answers (EditorLayer::onDetach): the files nobody asked to
    // write — the sessions, and the settings unless they were changed on disk.
    void saveOnExit();
    // The editor session (session.json): written at project open/close and at exit.
    void saveSession();
    void saveWindowState(LastOpenedProjectWindowState state);
    // The name the .Panda/project.json of a folder gives the project (ProjectSettings::name). Empty
    // — the folder is not a project: there is no such folder or file, the file is not JSON, or
    // it names no project; `problem` says which, in a sentence for the user.
    static std::string readName(const path_t &folder, std::string *problem = nullptr);
    // The project of the folder in place of the open one. false — the folder is not a project
    // (readName): nothing changes, the reason is in the log and in `problem`.
    bool openProject(const path_t &path, std::string *problem = nullptr);
    void reloadScriptsDll();
    // The script build on disk is another one than the loaded one (a step of Reload; the build
    // is not a document — nothing edits it in memory).
    bool isScriptsChangedOnDisk();
    // The file of the script library of the open project (it is there once the scripts were built).
    path_t scriptsLibraryPath();
    // The source file a script class is registered in (REGISTER_SCRIPT(Name)), found in the
    // Scripts folder of the project; empty — no file registers it. The sources are read once and
    // again after the script build is loaded: the files may have moved by then.
    path_t scriptSource(const std::string &className);
    void closeProject();
    // A new project of the template, named `name`, in `path`, opened (false — see openProject).
    bool createProject(const std::string &name, const path_t &path, std::string *problem = nullptr);
    // The session of the open project (.Panda/session.json): the worlds are the documents' to
    // say when it is written; what the Export window is set to is kept here as it is chosen.
    const ProjectSession &projectSession() const {
        return m_projectSession;
    }
    void setExportChoice(std::string target, std::string configuration) {
        m_projectSession.exportTarget = std::move(target);
        m_projectSession.exportConfiguration = std::move(configuration);
    }
    const std::vector<RecentProject> &getRecentProjectsList();
    bool hasOpenedProject();
    const path_t &getOpenedProjectPath();
    // The name of the open project: "name" of its project.json as the file says it now
    // (ProjectSettings::name — what follows from it). Not a folder: the folder of the project may be called
    // anything — a clone, a copy, a checkout of a branch.
    const std::string &projectName() const {
        return m_documents.projectSettings().name;
    }
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
