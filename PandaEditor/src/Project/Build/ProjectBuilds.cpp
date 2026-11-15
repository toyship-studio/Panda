#include "Project/Build/ProjectBuilds.hpp"

#include "Project/Documents/FileDocuments.hpp"
#include "Project/Export/GameExporter.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Loader/ProjectLoader.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/System/PandaSdkLocator.hpp"
#include "Project/System/SystemTools.hpp"

#include <Panda/Application/Application.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <algorithm>
#include <filesystem>
#include <thread>
#include <utility>

namespace Panda {

namespace {

    const char *buildTypeName(ScriptsBuildConfig config) {
        return config == ScriptsBuildConfig::Debug ? "Debug" : "RelWithDebInfo";
    }

    // The directories the tasks of an external editor use too: one build tree for both.
    const char *scriptsBuildDirectoryName(ScriptsBuildConfig config) {
        return config == ScriptsBuildConfig::Debug ? "cmake-build-debug" : "cmake-build-release";
    }

    // Export/build/<name>.
    const char *exportBuildDirectoryName(ExportProjectTarget target) {
        switch (target) {
            case ExportProjectTarget::XcodeMacOS: return "macos-debug";
            case ExportProjectTarget::XcodeIOSSimulator: return "ios-debug";
            case ExportProjectTarget::XcodeIOSDevice: return "ios-device-debug";
            case ExportProjectTarget::VisualStudioWindows: return "windows-debug";
        }
        return "macos-debug";
    }

    // The generator and the platform of the target; the SDK is named by an explicit PandaSDK_DIR.
    std::vector<std::string> exportConfigureArguments(
        ExportProjectTarget target, const path_t &exportDirectory, const path_t &buildDirectory,
        const path_t &sdkConfigDirectory
    ) {
        std::vector<std::string> args = {"-S", exportDirectory.string(), "-B", buildDirectory.string()};
        switch (target) {
            case ExportProjectTarget::XcodeMacOS: args.insert(args.end(), {"-G", "Xcode"}); break;
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
                // No -G: cmake takes the newest Visual Studio installed.
                args.insert(args.end(), {"-A", "x64"});
                break;
        }
        args.push_back("-DPandaSDK_DIR=" + sdkConfigDirectory.string());
        return args;
    }

    struct GeneratedProject {
        const char *extension;
        const char *ide;
    };

    GeneratedProject generatedProjectOf(ExportProjectTarget target) {
        switch (target) {
            case ExportProjectTarget::XcodeMacOS:
            case ExportProjectTarget::XcodeIOSSimulator:
            case ExportProjectTarget::XcodeIOSDevice: return {".xcodeproj", "Xcode"};
            case ExportProjectTarget::VisualStudioWindows: return {".sln", "Visual Studio"};
        }
        return {"", ""};
    }

    std::string jobsArgument() {
        return std::to_string(std::max(1u, std::thread::hardware_concurrency()));
    }

} // namespace

void ProjectBuilds::State::pushLine(std::string line) {
    std::lock_guard<std::mutex> lock(linesMutex);
    lines.push_back(std::move(line));
}

ProjectBuilds::ProjectBuilds(ProjectLoader &project, FileDocuments &documents, std::function<void()> scriptsBuilt)
    : m_project(project)
    , m_documents(documents)
    , m_scriptsBuilt(std::move(scriptsBuilt)) {}

ProjectBuilds::~ProjectBuilds() {
    m_state->cancelRequested = true;
}

void ProjectBuilds::setOutput(ProjectBuildsOutput *output) {
    m_output = output;
}

bool ProjectBuilds::isRunning() const {
    return m_state->running;
}

void ProjectBuilds::cancel() {
    if (m_state->running) { m_state->cancelRequested = true; }
}

path_t ProjectBuilds::folder(BuildTargetKind kind) {
    const path_t &projectPath = m_project.getOpenedProjectPath();
    return kind == BuildTargetKind::Export ? projectPath / "Export" : projectPath / "Scripts" / projectPath.filename();
}

bool ProjectBuilds::begin(BuildTargetKind kind, BuildAction action) {
    if (!m_project.hasOpenedProject()) { return false; }
    update(); // what the previous build still has to say belongs to it
    bool expected = false;
    if (!m_state->running.compare_exchange_strong(expected, true)) { return false; }
    m_state->cancelRequested = false;
    m_kind = kind;
    m_action = action;
    m_openWhenConfigured.reset();
    if (m_output != nullptr) { m_output->buildStarted(kind); }
    return true;
}

void ProjectBuilds::endWithError(const std::string &reason) {
    m_state->pushLine("error: " + reason);
    m_state->finished = Finished{};
    m_state->running = false;
}

void ProjectBuilds::fail(const std::string &reason) {
    LOG_ERROR_EDITOR("Build: %s", reason.c_str());
    endWithError(reason);
}

std::optional<ProjectBuilds::Toolchain> ProjectBuilds::findToolchain() {
    const std::optional<path_t> cmake = SystemTools::findCMakeExecutable();
    if (!cmake) {
        fail("cmake executable not found (bundled, SDK or PATH)");
        return std::nullopt;
    }
    const std::optional<path_t> sdkConfigDirectory = findPandaSdkConfigDirectory();
    if (!sdkConfigDirectory) {
        fail("PandaSDK not found near the editor");
        return std::nullopt;
    }
    return Toolchain{*cmake, *sdkConfigDirectory};
}

void ProjectBuilds::startScripts(BuildAction action, ScriptsBuildConfig config) {
    if (!begin(BuildTargetKind::Scripts, action)) { return; }
    const path_t scriptsDirectory = folder(BuildTargetKind::Scripts);
    if (!std::filesystem::exists(scriptsDirectory / "CMakeLists.txt")) {
        fail("scripts project not found at " + scriptsDirectory.string());
        return;
    }
    const std::optional<Toolchain> toolchain = findToolchain();
    if (!toolchain) { return; }

    const path_t buildDirectory = scriptsDirectory / scriptsBuildDirectoryName(config);
    std::vector<CommandStep> steps;
    // Build configures a tree that is not configured yet: a fresh checkout is built with one button.
    if (action == BuildAction::Configure || !std::filesystem::exists(buildDirectory / "CMakeCache.txt")) {
        steps.push_back(
            {toolchain->cmake,
             {"-S", scriptsDirectory.string(), "-B", buildDirectory.string(),
              std::string("-DCMAKE_BUILD_TYPE=") + buildTypeName(config),
              "-DPandaSDK_DIR=" + toolchain->sdkConfigDirectory.string(), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"}}
        );
    }
    if (action == BuildAction::Build) {
        steps.push_back({toolchain->cmake, {"--build", buildDirectory.string(), "-j", jobsArgument()}});
    }
    run(std::move(steps));
}

void ProjectBuilds::startExport(BuildAction action, ExportProjectTarget target) {
    startExport(action, target, /*openWhenConfigured*/ false);
}

void ProjectBuilds::openExportProject(ExportProjectTarget target) {
    if (isRunning()) {
        LOG_WARN_EDITOR("A build is running. Open the Export project when it is done.");
        return;
    }
    startExport(BuildAction::Configure, target, /*openWhenConfigured*/ true);
}

void ProjectBuilds::startExport(BuildAction action, ExportProjectTarget target, bool openWhenConfigured) {
    if (!begin(BuildTargetKind::Export, action)) { return; }
    // The Export project is built from the cooked data of the game: it is made first.
    if (!cook()) {
        endWithError("the runtime data was not cooked (the Output panel says why)");
        return;
    }
    const path_t exportDirectory = folder(BuildTargetKind::Export);
    if (!std::filesystem::exists(exportDirectory / "CMakeLists.txt")) {
        fail("export project not found at " + exportDirectory.string());
        return;
    }
    const std::optional<Toolchain> toolchain = findToolchain();
    if (!toolchain) { return; }

    const path_t buildDirectory = exportDirectory / "build" / exportBuildDirectoryName(target);
    std::vector<CommandStep> steps;
    if (action == BuildAction::Configure || !std::filesystem::exists(buildDirectory / "CMakeCache.txt")) {
        steps.push_back(
            {toolchain->cmake,
             exportConfigureArguments(target, exportDirectory, buildDirectory, toolchain->sdkConfigDirectory)}
        );
    }
    if (action == BuildAction::Build) {
        // Xcode and Visual Studio are multi-configuration generators: the configuration is chosen
        // at the build.
        steps.push_back(
            {toolchain->cmake, {"--build", buildDirectory.string(), "--config", "Debug", "-j", jobsArgument()}}
        );
    }
    if (openWhenConfigured) {
        m_openWhenConfigured = OpenRequest{buildDirectory, target};
        LOG_INFO_EDITOR(
            "Configuring the Export project (%s): cmake writes to the CMake panel, the project opens when it is done.",
            exportBuildDirectoryName(target)
        );
    }
    run(std::move(steps));
}

void ProjectBuilds::run(std::vector<CommandStep> steps) {
    // Background priority: a long process does not take the workers from the decoding of assets.
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
                    step.executable, step.args, {},
                    [&state](std::string_view line) { state->pushLine(std::string(line)); }, &state->cancelRequested
                );
                if (!ok || state->cancelRequested) { return false; }
            }
            return true;
        },
        [state = m_state](bool &&success) {
            const bool canceled = state->cancelRequested;
            state->finished = Finished{success && !canceled, canceled, /*cmakeRan*/ true};
            state->running = false;
        }
    );
}

void ProjectBuilds::update() {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(m_state->linesMutex);
        lines = std::exchange(m_state->lines, {});
    }
    if (!lines.empty() && m_output != nullptr) { m_output->buildOutput(m_kind, lines); }

    const std::optional<Finished> finished = std::exchange(m_state->finished, std::nullopt);
    if (!finished) { return; }
    if (m_output != nullptr) { m_output->buildFinished(m_kind, finished->success, finished->canceled); }
    const std::optional<OpenRequest> open = std::exchange(m_openWhenConfigured, std::nullopt);
    if (!finished->success) {
        if (finished->cmakeRan && !finished->canceled) {
            LOG_ERROR_EDITOR(
                "cmake failed for the %s project: see the CMake panel.",
                m_kind == BuildTargetKind::Export ? "Export" : "scripts"
            );
        }
        return;
    }
    if (m_kind == BuildTargetKind::Scripts && m_action == BuildAction::Build && m_scriptsBuilt) { m_scriptsBuilt(); }
    if (open) { openGeneratedProject(open->buildDirectory, open->target); }
}

void ProjectBuilds::openGeneratedProject(const path_t &buildDirectory, ExportProjectTarget target) {
    const GeneratedProject generated = generatedProjectOf(target);
    std::error_code errorCode;
    for (const auto &entry : std::filesystem::directory_iterator(buildDirectory, errorCode)) {
        if (entry.path().extension() != generated.extension) { continue; }
        if (SystemTools::openWithDefaultApplication(entry.path())) {
            LOG_INFO_EDITOR("Opened the %s project: %s", generated.ide, entry.path().string().c_str());
        } else {
            LOG_ERROR_EDITOR("Failed to open the %s project: %s", generated.ide, entry.path().string().c_str());
        }
        return;
    }
    LOG_ERROR_EDITOR("No %s project in %s", generated.ide, buildDirectory.string().c_str());
}

bool ProjectBuilds::cook() {
    if (!m_project.hasOpenedProject()) {
        LOG_WARN_EDITOR("No project is open. Open a project before exporting.");
        return false;
    }
    const path_t projectPath = m_project.getOpenedProjectPath();
    auto *assetHandler = AssetHandlerEditor::current();

    // The game starts in the world that is open, as it is edited (not the copy a running game
    // plays in); the start world of the project settings takes over with the list of worlds
    // (context/roadmap.md).
    WorldFile *world = m_documents.activeWorld();
    std::string worldPath;
    if (world != nullptr && !world->path().empty()) {
        worldPath = std::filesystem::relative(world->path(), projectPath).generic_string();
    }
    const std::vector<std::string> errors = GameExporter::validate(projectPath, worldPath, *assetHandler);
    if (!errors.empty()) {
        for (const std::string &error : errors) {
            LOG_ERROR_EDITOR("Export: %s", error.c_str());
        }
        LOG_ERROR_EDITOR("Nothing was exported: fix the errors above and export again.");
        return false;
    }

    ExportSettings settings;
    settings.runtimeDataDirectory = projectPath / ".Panda" / "Cooked" / "Runtime";
    settings.appName = projectPath.filename().string();
    settings.modules = ProjectModules::enabledIds(m_documents.projectSettings().get<ProjectModuleSettings>());
    LOG_INFO_EDITOR("Cooking the runtime data to %s", settings.runtimeDataDirectory.string().c_str());

    GameExporter exporter;
    // A world without a file does not pass the validation: `world` is one of the open worlds here.
    const ExportResult result = exporter.exportGame(settings, projectPath, *assetHandler, world->world());
    if (!result.success) {
        LOG_ERROR_EDITOR("Export failed: %s", result.errorMessage.c_str());
        return false;
    }
    for (const std::string &warning : result.warnings) {
        LOG_WARN_EDITOR("Export: %s", warning.c_str());
    }
    LOG_INFO_EDITOR(
        "The runtime data is cooked. The Export project builds the game from it: %s",
        folder(BuildTargetKind::Export).string().c_str()
    );
    return true;
}

void ProjectBuilds::showFolder(BuildTargetKind kind) {
    if (!m_project.hasOpenedProject()) { return; }
    const path_t directory = folder(kind);
    if (!std::filesystem::exists(directory)) {
        LOG_ERROR_EDITOR("Project folder not found: %s", directory.string().c_str());
        return;
    }
    SystemTools::openWithDefaultApplication(directory); // a folder opens in the file manager
}

} // namespace Panda
