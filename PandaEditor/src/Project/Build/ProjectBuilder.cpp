#include "Project/Build/ProjectBuilder.hpp"

#include "Project/System/PandaSdkLocator.hpp"
#include "Project/System/SystemTools.hpp"

#include <Panda/Application/Application.hpp>

#include <algorithm>
#include <filesystem>
#include <thread>
#include <utility>

namespace Panda {

namespace {

    const char *buildTypeName(ScriptsBuildConfig config) {
        return config == ScriptsBuildConfig::Debug ? "Debug" : "RelWithDebInfo";
    }

    // The same directories as in the external scripts/Zed tasks — a shared build tree.
    const char *buildDirName(ScriptsBuildConfig config) {
        return config == ScriptsBuildConfig::Debug ? "cmake-build-debug" : "cmake-build-release";
    }

    std::string jobsArg() {
        return std::to_string(std::max(1u, std::thread::hardware_concurrency()));
    }

} // namespace

void ProjectBuilder::State::pushLine(std::string line) {
    std::lock_guard<std::mutex> lock(linesMutex);
    lines.push_back(std::move(line));
}

ProjectBuilder::~ProjectBuilder() {
    m_state->cancelRequested = true;
}

bool ProjectBuilder::isRunning() const {
    return m_state->running;
}

BuildTargetKind ProjectBuilder::activeKind() const {
    return m_state->kind;
}

void ProjectBuilder::cancel() {
    if (m_state->running) { m_state->cancelRequested = true; }
}

bool ProjectBuilder::tryBegin(BuildTargetKind kind) {
    bool expected = false;
    if (!m_state->running.compare_exchange_strong(expected, true)) { return false; }
    m_state->cancelRequested = false;
    m_state->finished.reset();
    m_state->kind = kind;
    return true;
}

bool ProjectBuilder::failStart(BuildTargetKind kind, BuildAction action, std::string message) {
    m_state->pushLine(std::move(message));
    m_state->finished = Finished{kind, action, false, false};
    m_state->running = false;
    return true;
}

bool ProjectBuilder::startScripts(BuildAction action, ScriptsBuildConfig config, const path_t &scriptsDir) {
    if (!tryBegin(BuildTargetKind::Scripts)) { return false; }

    // The environment is resolved on the main thread; problems are reported to the output and the
    // task completes unsuccessfully.
    if (!std::filesystem::exists(scriptsDir / "CMakeLists.txt")) {
        return failStart(BuildTargetKind::Scripts, action, "error: scripts project not found at " + scriptsDir.string());
    }
    const std::optional<path_t> cmakePath = SystemTools::findCMakeExecutable();
    if (!cmakePath) {
        return failStart(BuildTargetKind::Scripts, action, "error: cmake executable not found (bundled, SDK or PATH)");
    }
    const std::optional<path_t> sdkDir = findPandaSdkConfigDirectory();
    if (!sdkDir) { return failStart(BuildTargetKind::Scripts, action, "error: PandaSDK not found near the editor"); }

    const path_t buildDir = scriptsDir / buildDirName(config);
    std::vector<CommandStep> steps;
    // Build without a configuration auto-configures (like CLion): a fresh checkout is built with
    // a single button.
    if (action == BuildAction::Configure || !std::filesystem::exists(buildDir / "CMakeCache.txt")) {
        steps.push_back(
            {*cmakePath,
             {"-S", scriptsDir.string(), "-B", buildDir.string(),
              std::string("-DCMAKE_BUILD_TYPE=") + buildTypeName(config), "-DPandaSDK_DIR=" + sdkDir->string(),
              "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"},
             {}}
        );
    }
    if (action == BuildAction::Build) {
        steps.push_back({*cmakePath, {"--build", buildDir.string(), "-j", jobsArg()}, {}});
    }
    submit(BuildTargetKind::Scripts, action, std::move(steps));
    return true;
}

const char *ProjectBuilder::exportBuildDirName(ExportProjectTarget target) {
    // The names match the old preset directories — existing build directories are reused.
    switch (target) {
        case ExportProjectTarget::XcodeMacOS: return "macos-debug";
        case ExportProjectTarget::XcodeIOSSimulator: return "ios-debug";
        case ExportProjectTarget::XcodeIOSDevice: return "ios-device-debug";
        case ExportProjectTarget::VisualStudioWindows: return "windows-debug";
    }
    return "macos-debug";
}

std::vector<std::string>
ProjectBuilder::exportConfigureArgs(ExportProjectTarget target, const path_t &exportDir, const path_t &sdkConfigDir) {
    const path_t buildDir = exportDir / "build" / exportBuildDirName(target);
    std::vector<std::string> args = {"-S", exportDir.string(), "-B", buildDir.string()};
    switch (target) {
        case ExportProjectTarget::XcodeMacOS:
            args.insert(args.end(), {"-G", "Xcode"});
            break;
        case ExportProjectTarget::XcodeIOSSimulator:
            args.insert(
                args.end(),
                {"-G", "Xcode", "-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=iphonesimulator",
                 "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0"}
            );
            break;
        case ExportProjectTarget::XcodeIOSDevice:
            args.insert(
                args.end(),
                {"-G", "Xcode", "-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=iphoneos",
                 "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0"}
            );
            break;
        case ExportProjectTarget::VisualStudioWindows:
            // No -G: cmake defaults to the newest installed Visual Studio.
            args.insert(args.end(), {"-A", "x64"});
            break;
    }
    args.push_back("-DPandaSDK_DIR=" + sdkConfigDir.string());
    return args;
}

bool ProjectBuilder::startExport(BuildAction action, ExportProjectTarget target, const path_t &exportDir) {
    if (!tryBegin(BuildTargetKind::Export)) { return false; }

    if (!std::filesystem::exists(exportDir / "CMakeLists.txt")) {
        return failStart(BuildTargetKind::Export, action, "error: export project not found at " + exportDir.string());
    }
    const std::optional<path_t> cmakePath = SystemTools::findCMakeExecutable();
    if (!cmakePath) {
        return failStart(BuildTargetKind::Export, action, "error: cmake executable not found (bundled, SDK or PATH)");
    }
    const std::optional<path_t> sdkDir = findPandaSdkConfigDirectory();
    if (!sdkDir) { return failStart(BuildTargetKind::Export, action, "error: PandaSDK not found near the editor"); }

    const path_t buildDir = exportDir / "build" / exportBuildDirName(target);
    std::vector<CommandStep> steps;
    if (action == BuildAction::Configure || !std::filesystem::exists(buildDir / "CMakeCache.txt")) {
        steps.push_back({*cmakePath, exportConfigureArgs(target, exportDir, *sdkDir), {}});
    }
    if (action == BuildAction::Build) {
        // Xcode/VS are multi-configuration generators: the configuration is set at the build stage.
        steps.push_back({*cmakePath, {"--build", buildDir.string(), "--config", "Debug", "-j", jobsArg()}, {}});
    }
    submit(BuildTargetKind::Export, action, std::move(steps));
    return true;
}

void ProjectBuilder::submit(BuildTargetKind kind, BuildAction action, std::vector<CommandStep> steps) {
    // Background priority: a long process must not displace texture/asset decoding from the workers.
    Application::get()->getScheduler().submit<bool>(
        PandaAsync::JobDesc{"project.build", PandaAsync::Priority::Background},
        [state = m_state, steps = std::move(steps)](PandaAsync::Context &) -> bool {
            for (const CommandStep &step : steps) {
                std::string display = "> " + step.executable.filename().string();
                for (const std::string &arg : step.args) {
                    display += " " + arg;
                }
                state->pushLine(std::move(display));
                const bool ok = SystemTools::runCommand(
                    step.executable, step.args, step.workingDirectory,
                    [&state](std::string_view line) { state->pushLine(std::string(line)); },
                    &state->cancelRequested
                );
                if (!ok || state->cancelRequested) { return false; }
            }
            return true;
        },
        [state = m_state, kind, action](bool &&success) {
            const bool canceled = state->cancelRequested;
            state->finished = Finished{kind, action, success && !canceled, canceled};
            state->running = false;
        }
    );
}

std::vector<std::string> ProjectBuilder::drainLines() {
    std::lock_guard<std::mutex> lock(m_state->linesMutex);
    return std::exchange(m_state->lines, {});
}

std::optional<ProjectBuilder::Finished> ProjectBuilder::takeFinished() {
    return std::exchange(m_state->finished, std::nullopt);
}

} // namespace Panda
