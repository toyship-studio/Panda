#pragma once

#include "Project/Export/ExportProjectTarget.hpp"

#include <Panda/Assets/Base/Path.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Panda {

class FileDocuments;
class ProjectLoader;

enum class BuildAction { Configure, Build };
enum class ScriptsBuildConfig { Debug, Release };
enum class BuildTargetKind { Scripts, Export }; // the two cmake projects of a project (the tabs of the CMake panel)

// Who shows the builds (the CMake panel). Called on the main thread.
class ProjectBuildsOutput {
public:
    virtual ~ProjectBuildsOutput() = default;
    virtual void buildStarted(BuildTargetKind kind) = 0;
    virtual void buildOutput(BuildTargetKind kind, const std::vector<std::string> &lines) = 0;
    virtual void buildFinished(BuildTargetKind kind, bool success, bool canceled) = 0;
};

// The builds of the open project: its two cmake projects — the scripts (Scripts/<name>) and the
// game (Export/) — and the data of the game the second one is built from.
//
// cmake is run from conventions and explicit arguments alone, without CMake*Presets.json (presets
// carried the paths of one machine and broke the project on another). One build at a time: cmake
// runs on a PandaAsync worker, its lines are collected and handed to the output once a frame
// (update). What is wrong with the environment — no cmake, no SDK, no project — is a line of the
// output and a failed build, the same way for every start. See context/architecture.md.
class ProjectBuilds final {
public:
    // scriptsBuilt — a Build of the scripts succeeded: the library on disk is a new one.
    ProjectBuilds(ProjectLoader &project, FileDocuments &documents, std::function<void()> scriptsBuilt);
    ~ProjectBuilds();

    void setOutput(ProjectBuildsOutput *output); // null — nobody shows the builds

    // A start while a build runs does nothing.
    void startScripts(BuildAction action, ScriptsBuildConfig config);
    void startExport(BuildAction action, ExportProjectTarget target); // cooks the data of the game first
    // Cooks, configures the Export project and opens what cmake generated in its IDE (Xcode,
    // Visual Studio).
    void openExportProject(ExportProjectTarget target);
    void cancel(); // cooperative: cmake stops at its next line of output
    bool isRunning() const;

    // The data of the game: the open world as it is edited, the assets, the list of modules →
    // .Panda/Cooked/Runtime. What it does and what is wrong goes to the log of the editor.
    // false — nothing usable was written.
    bool cook();
    void showFolder(BuildTargetKind kind); // the folder of the cmake project, in the file manager

    void update(); // once a frame: the lines and the end of the build → the output

private:
    struct Finished {
        bool success = false;
        bool canceled = false;
        bool cmakeRan = false; // false — the build ended before cmake started (endWithError)
    };

    struct CommandStep {
        path_t executable;
        std::vector<std::string> args;
    };

    // What the worker shares with the main thread. The job holds a copy of the pointer, so the
    // builds may go (the editor closes) while cmake still runs.
    struct State {
        std::atomic<bool> running{false};
        std::atomic<bool> cancelRequested{false};
        std::mutex linesMutex;
        std::vector<std::string> lines;
        std::optional<Finished> finished; // main thread: set when the job completes, taken by update

        void pushLine(std::string line);
    };

    // cmake and the SDK the projects are configured against.
    struct Toolchain {
        path_t cmake;
        path_t sdkConfigDirectory;
    };

    path_t folder(BuildTargetKind kind);
    bool begin(BuildTargetKind kind, BuildAction action); // false — no project, or a build runs
    // The build that began ends before cmake starts: the panel gets the reason as a line. fail
    // says it in the log of the editor too (the panel may be out of sight).
    void endWithError(const std::string &reason);
    void fail(const std::string &reason);
    std::optional<Toolchain> findToolchain(); // fails the build when a part is missing
    void run(std::vector<CommandStep> steps);
    void startExport(BuildAction action, ExportProjectTarget target, bool openWhenConfigured);
    void openGeneratedProject(const path_t &buildDirectory, ExportProjectTarget target);

    ProjectLoader &m_project;
    FileDocuments &m_documents;
    std::function<void()> m_scriptsBuilt;
    ProjectBuildsOutput *m_output = nullptr;
    std::shared_ptr<State> m_state = std::make_shared<State>();
    // The build that runs, or the last one.
    BuildTargetKind m_kind = BuildTargetKind::Scripts;
    BuildAction m_action = BuildAction::Configure;
    // openExportProject: where cmake generates the project, and for which IDE.
    struct OpenRequest {
        path_t buildDirectory;
        ExportProjectTarget target;
    };
    std::optional<OpenRequest> m_openWhenConfigured;
};

} // namespace Panda
