#include "Project/Build/ProjectBuilds.hpp"

#include "Project/Documents/FileDocuments.hpp"
#include "Project/Export/GameExporter.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Loader/ProjectLoader.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/System/PandaSdkLocator.hpp"
#include "Project/System/SystemTools.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
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

    // The toolchain a build tree of the scripts was made with (Toolchain::scriptsSignature), kept
    // in the tree; no file — the defaults.
    constexpr const char *TOOLCHAIN_STAMP = "panda-toolchain.txt";

} // namespace

void ProjectBuilds::State::pushLine(std::string line) {
    std::lock_guard<std::mutex> lock(linesMutex);
    lines.push_back(std::move(line));
}

ProjectBuilds::ProjectBuilds(ProjectLoader &project, FileDocuments &documents, std::function<void()> scriptsBuilt)
    : m_project(project)
    , m_documents(documents)
    , m_scriptsBuilt(std::move(scriptsBuilt))
#ifdef PLATFORM_WINDOWS
    , m_exportTarget(ExportProjectTarget::VisualStudioWindows)
#else
    , m_exportTarget(ExportProjectTarget::XcodeMacOS)
#endif
{}

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

path_t ProjectBuilds::buildFolder(BuildTargetKind kind) {
    return kind == BuildTargetKind::Export ? folder(kind) / "build" / exportBuildDirectoryName(m_exportTarget)
                                           : folder(kind) / scriptsBuildDirectoryName(m_scriptsConfig);
}

/* ------------------------------ The steps of a build ------------------------------ */

ProjectBuilds::Step ProjectBuilds::commandStep(path_t executable, std::vector<std::string> args) {
    return [executable = std::move(executable), args = std::move(args)](State &state) {
        std::string display = "> " + executable.filename().string();
        for (const std::string &arg : args) {
            display += " " + arg;
        }
        state.pushLine(std::move(display));
        const bool ok = SystemTools::runCommand(
            executable, args, {}, [&state](std::string_view line) { state.pushLine(std::string(line)); },
            &state.cancelRequested
        );
        return ok && !state.cancelRequested;
    };
}

ProjectBuilds::Step ProjectBuilds::removeStep(path_t directory, std::string why) {
    return [directory = std::move(directory), why = std::move(why)](State &state) {
        if (!why.empty()) { state.pushLine(why); }
        std::error_code error;
        if (!std::filesystem::exists(directory, error)) {
            state.pushLine("Nothing to clean: " + directory.string() + " does not exist.");
            return true;
        }
        state.pushLine("> remove " + directory.string());
        std::filesystem::remove_all(directory, error);
        if (error) {
            state.pushLine("error: the folder could not be removed: " + error.message());
            return false;
        }
        return true;
    };
}

ProjectBuilds::Step ProjectBuilds::stampStep(path_t buildDirectory, std::string signature) {
    return [file = buildDirectory / TOOLCHAIN_STAMP, signature = std::move(signature)](State &) {
        std::error_code error;
        if (signature.empty()) {
            std::filesystem::remove(file, error); // the defaults need no mark
        } else {
            std::ofstream(file) << signature;
        }
        return true;
    };
}

void ProjectBuilds::run(std::vector<Step> steps) {
    // Background priority: a long process does not take the workers from the decoding of assets.
    Application::get()->getScheduler().submit<bool>(
        PandaAsync::JobDesc{"project.build", PandaAsync::Priority::Background},
        [state = m_state, steps = std::move(steps)](PandaAsync::Context &) -> bool {
            for (const Step &step : steps) {
                if (!step(*state) || state->cancelRequested) { return false; }
            }
            return true;
        },
        [state = m_state](bool &&success) {
            const bool canceled = state->cancelRequested;
            state->finished = Finished{success && !canceled, canceled, /*ran*/ true};
            state->running = false;
        }
    );
}

/* ------------------------------ Starting ------------------------------ */

bool ProjectBuilds::begin(BuildTargetKind kind, BuildAction action) {
    if (!m_project.hasOpenedProject()) { return false; }
    update(); // what the previous build still has to say belongs to it
    bool expected = false;
    if (!m_state->running.compare_exchange_strong(expected, true)) { return false; }
    m_state->cancelRequested = false;
    m_kind = kind;
    m_action = action;
    m_openWhenConfigured.reset();
    m_lastResult = Result::Running;
    m_lastOutput.clear();
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
    const EditorToolchainSettings &settings = m_documents.editorSettings().get<EditorToolchainSettings>();
    const char *where = " (Settings, Editor, Toolchain)";
    Toolchain toolchain;

    const std::optional<path_t> cmake =
        settings.cmake.empty() ? SystemTools::findCMakeExecutable() : SystemTools::findExecutable(settings.cmake);
    if (!cmake) {
        fail(settings.cmake.empty() ? "cmake executable not found (bundled, SDK or PATH)"
                                    : "cmake \"" + settings.cmake + "\" was not found" + where);
        return std::nullopt;
    }
    toolchain.cmake = *cmake;

    const std::optional<path_t> sdkConfigDirectory = findPandaSdkConfigDirectory();
    if (!sdkConfigDirectory) {
        fail("PandaSDK not found near the editor");
        return std::nullopt;
    }
    toolchain.sdkConfigDirectory = *sdkConfigDirectory;

    // The scripts: the generator and the compilers. Commands are resolved to files here — cmake
    // run by a GUI application does not see the PATH of the user's shell.
    if (!settings.generator.empty()) {
        toolchain.scriptsArguments.insert(toolchain.scriptsArguments.end(), {"-G", settings.generator});
        toolchain.scriptsSignature += "generator=" + settings.generator + ";";
        if (settings.generator.find("Ninja") != std::string::npos) {
            const std::optional<path_t> ninja = SystemTools::findExecutable("ninja");
            if (!ninja) {
                fail(std::string("ninja was not found: install it or choose another generator") + where);
                return std::nullopt;
            }
            toolchain.scriptsArguments.push_back("-DCMAKE_MAKE_PROGRAM=" + ninja->string());
        }
    }
    const std::pair<const std::string *, const char *> compilers[] = {
        {&settings.cCompiler, "CMAKE_C_COMPILER"},
        {&settings.cxxCompiler, "CMAKE_CXX_COMPILER"},
    };
    for (const auto &[command, variable] : compilers) {
        if (command->empty()) { continue; }
        const std::optional<path_t> compiler = SystemTools::findExecutable(*command);
        if (!compiler) {
            fail("compiler \"" + *command + "\" was not found" + where);
            return std::nullopt;
        }
        toolchain.scriptsArguments.push_back(std::string("-D") + variable + "=" + compiler->string());
        toolchain.scriptsSignature += std::string(variable) + "=" + compiler->string() + ";";
    }
    return toolchain;
}

void ProjectBuilds::start(BuildTargetKind kind, BuildAction action) {
    start(kind, action, /*openWhenConfigured*/ false);
}

void ProjectBuilds::openExportProject(ExportProjectTarget target) {
    if (isRunning()) {
        LOG_WARN_EDITOR("A build is running. Open the Export project when it is done.");
        return;
    }
    m_exportTarget = target;
    start(BuildTargetKind::Export, BuildAction::Configure, /*openWhenConfigured*/ true);
}

void ProjectBuilds::start(BuildTargetKind kind, BuildAction action, bool openWhenConfigured) {
    if (!begin(kind, action)) { return; }
    const bool scripts = kind == BuildTargetKind::Scripts;
    const path_t projectDirectory = folder(kind);
    const path_t buildDirectory = buildFolder(kind);
    std::vector<Step> steps;

    if (action == BuildAction::Clean) {
        steps.push_back(removeStep(buildDirectory, {}));
        run(std::move(steps));
        return;
    }
    // The Export project is built from the cooked data of the game: it is made first.
    if (!scripts && !cook()) {
        endWithError("the runtime data was not cooked (the Output panel says why)");
        return;
    }
    if (!std::filesystem::exists(projectDirectory / "CMakeLists.txt")) {
        fail(std::string(scripts ? "scripts" : "export") + " project not found at " + projectDirectory.string());
        return;
    }
    const std::optional<Toolchain> toolchain = findToolchain();
    if (!toolchain) { return; }

    // A tree made with another generator or compiler is not one cmake goes on with: it is made
    // again. The Export project is an IDE project and takes neither from the toolchain.
    const bool configured = std::filesystem::exists(buildDirectory / "CMakeCache.txt");
    const std::string signature = scripts ? toolchain->scriptsSignature : std::string();
    bool stale = false;
    if (scripts && configured) {
        std::ifstream stamp(buildDirectory / TOOLCHAIN_STAMP);
        const std::string made((std::istreambuf_iterator<char>(stamp)), std::istreambuf_iterator<char>());
        stale = made != signature;
    }
    if (stale) {
        steps.push_back(removeStep(buildDirectory, "The toolchain changed: the build folder is made again."));
    }
    // Build configures a tree that is not configured yet: a fresh checkout is built with one button.
    if (action == BuildAction::Configure || stale || !configured) {
        std::vector<std::string> arguments;
        if (scripts) {
            arguments = {"-S", projectDirectory.string(), "-B", buildDirectory.string(),
                         std::string("-DCMAKE_BUILD_TYPE=") + buildTypeName(m_scriptsConfig),
                         "-DPandaSDK_DIR=" + toolchain->sdkConfigDirectory.string(), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"};
            arguments.insert(arguments.end(), toolchain->scriptsArguments.begin(), toolchain->scriptsArguments.end());
        } else {
            arguments =
                exportConfigureArguments(m_exportTarget, projectDirectory, buildDirectory, toolchain->sdkConfigDirectory);
        }
        steps.push_back(commandStep(toolchain->cmake, std::move(arguments)));
        if (scripts) { steps.push_back(stampStep(buildDirectory, signature)); }
    }
    if (action == BuildAction::Build) {
        std::vector<std::string> arguments = {"--build", buildDirectory.string()};
        // Xcode and Visual Studio are multi-configuration generators: the configuration is chosen
        // at the build.
        if (!scripts) { arguments.insert(arguments.end(), {"--config", "Debug"}); }
        arguments.insert(arguments.end(), {"-j", jobsArgument()});
        steps.push_back(commandStep(toolchain->cmake, std::move(arguments)));
    }
    if (openWhenConfigured) {
        m_openWhenConfigured = OpenRequest{buildDirectory, m_exportTarget};
        LOG_INFO_EDITOR(
            "Configuring the Export project (%s): cmake writes to the CMake panel, the project opens when it is done.",
            exportBuildDirectoryName(m_exportTarget)
        );
    }
    run(std::move(steps));
}

void ProjectBuilds::update() {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(m_state->linesMutex);
        lines = std::exchange(m_state->lines, {});
    }
    if (!lines.empty()) {
        if (m_output != nullptr) { m_output->buildOutput(m_kind, lines); }
        m_lastOutput.insert(m_lastOutput.end(), lines.begin(), lines.end());
    }

    const std::optional<Finished> finished = std::exchange(m_state->finished, std::nullopt);
    if (!finished) { return; }
    m_lastResult = finished->canceled ? Result::Canceled : finished->success ? Result::Succeeded : Result::Failed;
    if (m_output != nullptr) { m_output->buildFinished(m_kind, finished->success, finished->canceled); }
    const std::optional<OpenRequest> open = std::exchange(m_openWhenConfigured, std::nullopt);
    if (!finished->success) {
        if (finished->ran && !finished->canceled) {
            static const char *const ACTIONS[] = {"Configure", "Build", "Clean"};
            LOG_ERROR_EDITOR(
                "%s of the %s project failed: see the CMake panel.", ACTIONS[static_cast<int>(m_action)],
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
