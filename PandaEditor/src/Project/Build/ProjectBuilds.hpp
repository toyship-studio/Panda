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

// Configure — cmake makes the build tree (again); Build — compiles, configuring a tree that is not
// configured yet; Clean — deletes the build folder.
enum class BuildAction { Configure, Build, Clean };
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
// carried the paths of one machine and broke the project on another). What it is run with — the
// cmake itself, the generator, the compilers — is the toolchain of the editor settings
// (EditorToolchainSettings); the Export project is an IDE project (Xcode, Visual Studio) and
// takes only the cmake from it. One build at a time: the steps run on a PandaAsync worker, their
// lines are collected and handed to the output once a frame (update). What is wrong before the
// first step — no cmake, no SDK, no project — is a line of the output and a failed build, the
// same way for every start. See context/architecture.md.
class ProjectBuilds final {
public:
    // scriptsBuilt — a Build of the scripts succeeded: the library on disk is a new one.
    ProjectBuilds(ProjectLoader &project, FileDocuments &documents, std::function<void()> scriptsBuilt);
    ~ProjectBuilds();

    void setOutput(ProjectBuildsOutput *output); // null — nobody shows the builds

    // What a build builds: the configuration of the scripts, the target of the Export project. The
    // CMake panel shows and changes them; a command (Build Scripts) builds what is chosen.
    ScriptsBuildConfig scriptsConfig() const {
        return m_scriptsConfig;
    }
    void setScriptsConfig(ScriptsBuildConfig config) {
        m_scriptsConfig = config;
    }
    ExportProjectTarget exportTarget() const {
        return m_exportTarget;
    }
    void setExportTarget(ExportProjectTarget target) {
        m_exportTarget = target;
    }

    // A start while a build runs does nothing. An Export build cooks the data of the game first.
    void start(BuildTargetKind kind, BuildAction action);
    // Cooks, configures the Export project for the target and opens what cmake generated in its
    // IDE (Xcode, Visual Studio).
    void openExportProject(ExportProjectTarget target);
    void cancel(); // cooperative: a step stops at its next line of output
    bool isRunning() const;
    // The run that was started last, for who did not watch it (a tool of the MCP server): how it
    // ended — Running until update has handed out its last line — and everything it said.
    enum class Result { None, Running, Succeeded, Failed, Canceled };
    Result lastResult() const {
        return m_lastResult;
    }
    const std::vector<std::string> &lastOutput() const {
        return m_lastOutput;
    }

    // The data of the game: the open world as it is edited, the assets, the list of modules →
    // .Panda/Cooked/Runtime. What it does and what is wrong goes to the log of the editor.
    // false — nothing usable was written.
    bool cook();
    void showFolder(BuildTargetKind kind); // the folder of the cmake project, in the file manager
    // Where the cmake project is built in its chosen configuration: Scripts/<name>/cmake-build-debug,
    // Export/build/macos-debug.
    path_t buildFolder(BuildTargetKind kind);

    void update(); // once a frame: the lines and the end of the build → the output

private:
    struct Finished {
        bool success = false;
        bool canceled = false;
        bool ran = false; // false — the build ended before its first step (endWithError)
    };

    // What the worker shares with the main thread. The job holds a copy of the pointer, so the
    // builds may go (the editor closes) while a step still runs.
    struct State {
        std::atomic<bool> running{false};
        std::atomic<bool> cancelRequested{false};
        std::mutex linesMutex;
        std::vector<std::string> lines;
        std::optional<Finished> finished; // main thread: set when the job completes, taken by update

        void pushLine(std::string line);
    };

    // A step of a build, run on the worker; false ends the build as failed.
    using Step = std::function<bool(State &)>;
    static Step commandStep(path_t executable, std::vector<std::string> args);
    static Step removeStep(path_t directory, std::string why);
    static Step stampStep(path_t buildDirectory, std::string signature);

    // What cmake is run with (EditorToolchainSettings), resolved to files.
    struct Toolchain {
        path_t cmake;
        path_t sdkConfigDirectory;
        std::vector<std::string> scriptsArguments; // -G …, the make program, the compilers
        // The generator and the compilers of the scripts as one line: a build tree keeps the one
        // it was made with, and a tree made with another is made again. Empty — the defaults.
        std::string scriptsSignature;
    };

    path_t folder(BuildTargetKind kind);
    bool begin(BuildTargetKind kind, BuildAction action); // false — no project, or a build runs
    // The build that began ends before its first step: the panel gets the reason as a line. fail
    // says it in the log of the editor too (the panel may be out of sight).
    void endWithError(const std::string &reason);
    void fail(const std::string &reason);
    std::optional<Toolchain> findToolchain(); // fails the build when a part is missing
    void start(BuildTargetKind kind, BuildAction action, bool openWhenConfigured);
    void run(std::vector<Step> steps);
    void openGeneratedProject(const path_t &buildDirectory, ExportProjectTarget target);

    ProjectLoader &m_project;
    FileDocuments &m_documents;
    std::function<void()> m_scriptsBuilt;
    ProjectBuildsOutput *m_output = nullptr;
    std::shared_ptr<State> m_state = std::make_shared<State>();
    ScriptsBuildConfig m_scriptsConfig = ScriptsBuildConfig::Debug;
    ExportProjectTarget m_exportTarget;
    // The build that runs, or the last one.
    BuildTargetKind m_kind = BuildTargetKind::Scripts;
    BuildAction m_action = BuildAction::Configure;
    // openExportProject: where cmake generates the project, and for which IDE.
    struct OpenRequest {
        path_t buildDirectory;
        ExportProjectTarget target;
    };
    std::optional<OpenRequest> m_openWhenConfigured;
    Result m_lastResult = Result::None;
    std::vector<std::string> m_lastOutput;
};

} // namespace Panda
