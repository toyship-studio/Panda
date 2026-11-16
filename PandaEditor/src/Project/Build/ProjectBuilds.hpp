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

struct CookReport;

class FileDocuments;
class ProjectLoader;

// Configure — cmake makes the build tree (again); Build — compiles, configuring a tree that is not
// configured yet; Clean — deletes the build folder; Cook — the data of the game alone, without
// cmake (the Export project only).
enum class BuildAction { Configure, Build, Clean, Cook };
enum class BuildConfig { Debug, Release };
enum class BuildTargetKind { Scripts, Export }; // the two cmake projects of a project
// The steps of an export, in their order.
enum class ExportStep { None, Cook, Configure, Build };

// Who shows the build of the scripts (the CMake panel). Called on the main thread.
class ScriptsBuildOutput {
public:
    virtual ~ScriptsBuildOutput() = default;
    virtual void buildStarted() = 0;
    virtual void buildOutput(const std::vector<std::string> &lines) = 0;
    virtual void buildFinished(bool success, bool canceled) = 0;
};

// The builds of the open project: its two cmake projects — the scripts (Scripts/<name>) and the
// game (Export/) — and the data of the game the second one is built from.
//
// cmake is run from conventions and explicit arguments alone, without CMake*Presets.json (presets
// carried the paths of one machine and broke the project on another). What it is run with — the
// cmake itself, the generator, the compilers — is the toolchain of the editor settings
// (EditorToolchainSettings); the Export project is an IDE project (Xcode, Visual Studio) and
// takes only the cmake from it. One build at a time: the steps run on a PandaAsync worker, their
// lines are collected once a frame (update). What is wrong before the first step — no cmake, no
// SDK, no project — is a line of the output and a failed build, the same way for every start.
//
// The two are shown in two places. The scripts are the inner loop: the CMake panel is given
// their output as it comes (ScriptsBuildOutput). The export is a task of its own with a window
// of its own, which asks what it shows when it draws: the run (kind, action, lastResult,
// lastOutput), its step, the game it made, the report of its cook. See context/architecture.md.
class ProjectBuilds final {
public:
    // scriptsBuilt — a Build of the scripts succeeded: the library on disk is a new one.
    ProjectBuilds(ProjectLoader &project, FileDocuments &documents, std::function<void()> scriptsBuilt);
    ~ProjectBuilds();

    void setOutput(ScriptsBuildOutput *output); // null — nobody shows the build of the scripts

    // What a build builds: the configuration of the scripts; the target and the configuration
    // of the export. The CMake panel and the Export window show and change them; a command
    // (Build Scripts) builds what is chosen.
    BuildConfig scriptsConfig() const {
        return m_scriptsConfig;
    }
    void setScriptsConfig(BuildConfig config) {
        m_scriptsConfig = config;
    }
    // The export targets of this machine: an Xcode project is made on a Mac, a Visual Studio
    // one on Windows. Empty — the editor exports nothing on this platform.
    static std::vector<ExportProjectTarget> exportTargets();
    static const char *exportTargetName(ExportProjectTarget target); // "macOS", "iOS Simulator", …
    static const char *exportIdeName(ExportProjectTarget target);    // "Xcode", "Visual Studio"
    ExportProjectTarget exportTarget() const {
        return m_exportTarget;
    }
    void setExportTarget(ExportProjectTarget target) {
        m_exportTarget = target;
    }
    BuildConfig exportConfig() const {
        return m_exportConfig;
    }
    void setExportConfig(BuildConfig config) {
        m_exportConfig = config;
    }

    // A start while a build runs does nothing. An export cooks the data of the game first — on
    // the main thread, which stops the editor for its time, so it waits for the frame that shows
    // it has begun (update) — then cmake configures the Export project, then it is built.
    void start(BuildTargetKind kind, BuildAction action);
    // Cooks, configures the Export project for the chosen target and opens what cmake generated
    // in its IDE (Xcode, Visual Studio).
    void openExportProject();
    void cancel(); // cooperative: a step stops at its next line of output
    bool isRunning() const;
    // The run that was started last, for who did not watch it (the Export window, a tool of the
    // MCP server): what it was, how it ended — Running until update has taken its last line —
    // and everything it said.
    enum class Result { None, Running, Succeeded, Failed, Canceled };
    BuildTargetKind kind() const {
        return m_kind;
    }
    BuildAction action() const {
        return m_action;
    }
    Result lastResult() const {
        return m_lastResult;
    }
    const std::vector<std::string> &lastOutput() const {
        return m_lastOutput;
    }
    // The step an export is at, or the one its last run ended in (a failed run: the step that
    // failed). None — the last run was not an export.
    ExportStep exportStep() const;

    // The data of the game (GameExporter), from the files of the project: the start world of the
    // project settings and everything it reaches, in one pack; the manifest and the list of
    // modules → .Panda/Cooked/Runtime. What is wrong, what is not saved and what the project has
    // that the game does not reach goes to the log of the editor and into cookMessages(); the
    // whole account — cookReportFile(). false — nothing usable was written.
    bool cook();
    // What the last cook said — its errors, its warnings, the summary of the pack — as lines of
    // the output: an export shows them before what cmake says.
    const std::vector<std::string> &cookMessages() const {
        return m_cookMessages;
    }
    // Grows with every cook: who shows the report of the last one reads it again.
    uint32_t cookCount() const {
        return m_cookCount;
    }
    // The game the Export project built for the chosen target and configuration.
    struct BuiltGame {
        path_t product;    // what the build made: the .app bundle, the .exe; empty — it is not built
        path_t executable; // what runs here: empty for a target of another platform (an iOS build)
    };
    BuiltGame builtGame();
    bool runBuiltGame(); // starts it as the system starts an application; false — nothing to run here
    // .Panda/Cooked/report.json: what the last cook made (CookReport).
    path_t cookReportFile() const;
    // The report of the last cook of the project, read from that file; false — no cook was made.
    bool readCookReport(CookReport &report) const;
    void showFolder(BuildTargetKind kind); // the folder of the cmake project, in the file manager
    // Where the cmake project is built in its chosen configuration: Scripts/<name>/cmake-build-debug,
    // Export/build/macos-release.
    path_t buildFolder(BuildTargetKind kind);

    void update(); // once a frame: the lines and the end of the build; an export that waits begins

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
        std::atomic<ExportStep> exportStep{ExportStep::None};
        std::mutex linesMutex;
        std::vector<std::string> lines;
        std::optional<Finished> finished; // main thread: set when the job completes, taken by update

        void pushLine(std::string line);
    };

    // A step of a build, run on the worker; false ends the build as failed.
    using Step = std::function<bool(State &)>;
    static Step commandStep(path_t executable, std::vector<std::string> args);
    static Step exportStepMark(ExportStep step); // the export enters a step
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
    void launch(bool openWhenConfigured); // the build that began: its steps are made and started
    void run(std::vector<Step> steps);
    void openGeneratedProject(const path_t &buildDirectory, ExportProjectTarget target);

    ProjectLoader &m_project;
    FileDocuments &m_documents;
    std::vector<std::string> m_cookMessages;
    std::function<void()> m_scriptsBuilt;
    ScriptsBuildOutput *m_output = nullptr;
    std::shared_ptr<State> m_state = std::make_shared<State>();
    BuildConfig m_scriptsConfig = BuildConfig::Debug;
    ExportProjectTarget m_exportTarget;
    BuildConfig m_exportConfig = BuildConfig::Debug;
    uint32_t m_cookCount = 0;
    // An export that began and waits for its cook: the frames left before it (start).
    struct PendingExport {
        bool openWhenConfigured = false;
        int frames = 0;
    };
    std::optional<PendingExport> m_pendingExport;
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
