#pragma once

#include "Project/Export/ExportProjectTarget.hpp"

#include <Panda/Assets/Base/Path.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Panda {

enum class BuildAction { Configure, Build };
enum class ScriptsBuildConfig { Debug, Release };
enum class BuildTargetKind { Scripts, Export }; // CMake panel tabs: the scripts project, the Export project

// Asynchronous cmake pipelines of the open project: the scripts project (Scripts/<name>) and the
// Export project (Export/). Both are built PURELY from conventions and explicit arguments, WITHOUT
// CMake*Presets.json (presets carried machine-specific paths and broke the project on another
// computer). One run at a time; cmake runs on a PandaAsync worker, its output is accumulated line
// by line in a queue and drained by the main thread once per frame. See
// context/architecture.md.
class ProjectBuilder final {
public:
    struct Finished {
        BuildTargetKind kind;
        BuildAction action;
        bool success = false;
        bool canceled = false;
    };

    ~ProjectBuilder();

    // false — a build is already running (a new one does not start). Environment errors (no
    // cmake/SDK/project) do not fail the call: they are reported as output lines, and the task
    // completes unsuccessfully.
    bool startScripts(BuildAction action, ScriptsBuildConfig config, const path_t &scriptsDir);
    bool startExport(BuildAction action, ExportProjectTarget target, const path_t &exportDir);

    // The composition of the export command is shared by the panel (async) and the "Open Export
    // Project" menu (sync): the generator and the target's platform flags are set here, the SDK —
    // by an explicit -DPandaSDK_DIR.
    static const char *exportBuildDirName(ExportProjectTarget target); // Export/build/<name>
    static std::vector<std::string>
    exportConfigureArgs(ExportProjectTarget target, const path_t &exportDir, const path_t &sdkConfigDir);
    void cancel(); // cooperative: stops at the nearest cmake output line
    bool isRunning() const;
    BuildTargetKind activeKind() const; // target of the current (or last) run — output routing by tabs

    std::vector<std::string> drainLines();  // main thread, once per frame
    std::optional<Finished> takeFinished(); // main thread; returns the result once

private:
    struct CommandStep {
        path_t executable;
        std::vector<std::string> args;
        path_t workingDirectory;
    };

    // All the shared state is in a shared_ptr: the job on the worker holds a copy of it, so
    // destroying ProjectBuilder itself (editor closing) while the process is alive is safe.
    struct State {
        std::atomic<bool> running{false};
        std::atomic<bool> cancelRequested{false};
        std::mutex linesMutex;
        std::vector<std::string> lines;
        std::optional<Finished> finished; // written in complete (main thread)
        BuildTargetKind kind = BuildTargetKind::Scripts;

        void pushLine(std::string line);
    };

    bool tryBegin(BuildTargetKind kind);
    bool failStart(BuildTargetKind kind, BuildAction action, std::string message);
    void submit(BuildTargetKind kind, BuildAction action, std::vector<CommandStep> steps);

    std::shared_ptr<State> m_state = std::make_shared<State>();
};

} // namespace Panda
