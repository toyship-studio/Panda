#include "Project/Build/ProjectBuilds.hpp"

#include "Project/Documents/FileDocuments.hpp"
#include "Project/Export/GameExporter.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Loader/ProjectLoader.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/System/PandaSdkLocator.hpp"
#include "Project/System/SystemTools.hpp"

#include <Rain/Coders/JsonDecoder.hpp>
#include <fstream>
#include <cstdio>
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

    const char *buildTypeName(BuildConfig config) {
        return config == BuildConfig::Debug ? "Debug" : "RelWithDebInfo";
    }

    // The directories the tasks of an external editor use too: one build tree for both.
    const char *scriptsBuildDirectoryName(BuildConfig config) {
        return config == BuildConfig::Debug ? "cmake-build-debug" : "cmake-build-release";
    }

    // The targets of the machine the editor runs on: an Xcode project is made on a Mac, a Visual
    // Studio one on Windows.
    constexpr ExportProjectTarget HOST_EXPORT_TARGETS[] = {
#if defined(PLATFORM_MACOS)
        ExportProjectTarget::XcodeMacOS,
        ExportProjectTarget::XcodeIOSSimulator,
        ExportProjectTarget::XcodeIOSDevice,
#elif defined(PLATFORM_WINDOWS)
        ExportProjectTarget::VisualStudioWindows,
#else
        ExportProjectTarget::XcodeMacOS, // nothing is exported here: exportTargets() is empty
#endif
    };

    // The platform of a target as the files name it: the build folder, the session of the project.
    const char *exportPlatformName(ExportProjectTarget target) {
        switch (target) {
            case ExportProjectTarget::XcodeMacOS: return "macos";
            case ExportProjectTarget::XcodeIOSSimulator: return "ios";
            case ExportProjectTarget::XcodeIOSDevice: return "ios-device";
            case ExportProjectTarget::VisualStudioWindows: return "windows";
        }
        return "macos";
    }

    const char *exportConfigurationName(BuildConfig config) {
        return config == BuildConfig::Debug ? "debug" : "release";
    }

    // Export/build/<name>: the platform and the configuration, the names the shell scripts of a
    // project use too (configure_export.sh macos-release).
    std::string exportBuildDirectoryName(ExportProjectTarget target, BuildConfig config) {
        return std::string(exportPlatformName(target)) + "-" + exportConfigurationName(config);
    }

    // The configuration of the Export project, as Xcode and Visual Studio name it.
    const char *exportConfigName(BuildConfig config) {
        return config == BuildConfig::Debug ? "Debug" : "Release";
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
    // Written when cmake has configured and generated the tree: a configure that failed leaves
    // CMakeCache.txt without a build system, and such a tree is configured again.
    constexpr const char *CONFIGURED_STAMP = "panda-configured.txt";

} // namespace

void ProjectBuilds::State::pushLine(std::string line) {
    std::lock_guard<std::mutex> lock(linesMutex);
    lines.push_back(std::move(line));
}

ProjectBuilds::ProjectBuilds(ProjectLoader &project, FileDocuments &documents, std::function<void()> scriptsBuilt)
    : m_project(project)
    , m_documents(documents)
    , m_scriptsBuilt(std::move(scriptsBuilt)) {}

std::vector<ExportProjectTarget> ProjectBuilds::exportTargets() {
#if defined(PLATFORM_MACOS) || defined(PLATFORM_WINDOWS)
    return {std::begin(HOST_EXPORT_TARGETS), std::end(HOST_EXPORT_TARGETS)};
#else
    return {};
#endif
}

ExportProjectTarget ProjectBuilds::exportTarget() const {
    const std::string &chosen = m_project.projectSession().exportTarget;
    for (ExportProjectTarget target : HOST_EXPORT_TARGETS) {
        if (chosen == exportPlatformName(target)) { return target; }
    }
    return HOST_EXPORT_TARGETS[0]; // not chosen yet, or chosen on another machine
}

void ProjectBuilds::setExportTarget(ExportProjectTarget target) {
    m_project.setExportChoice(exportPlatformName(target), m_project.projectSession().exportConfiguration);
}

BuildConfig ProjectBuilds::exportConfig() const {
    return m_project.projectSession().exportConfiguration == exportConfigurationName(BuildConfig::Release) ? BuildConfig::Release
                                                                                                         : BuildConfig::Debug;
}

void ProjectBuilds::setExportConfig(BuildConfig config) {
    m_project.setExportChoice(m_project.projectSession().exportTarget, exportConfigurationName(config));
}

const char *ProjectBuilds::exportTargetName(ExportProjectTarget target) {
    switch (target) {
        case ExportProjectTarget::XcodeMacOS: return "macOS";
        case ExportProjectTarget::XcodeIOSSimulator: return "iOS Simulator";
        case ExportProjectTarget::XcodeIOSDevice: return "iOS Device";
        case ExportProjectTarget::VisualStudioWindows: return "Windows";
    }
    return "";
}

const char *ProjectBuilds::exportIdeName(ExportProjectTarget target) {
    return generatedProjectOf(target).ide;
}

ExportStep ProjectBuilds::exportStep() const {
    return m_kind == BuildTargetKind::Export ? m_state->exportStep.load() : ExportStep::None;
}

ProjectBuilds::~ProjectBuilds() {
    m_state->cancelRequested = true;
}

void ProjectBuilds::setOutput(ScriptsBuildOutput *output) {
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
    return kind == BuildTargetKind::Export
               ? folder(kind) / "build" / exportBuildDirectoryName(exportTarget(), exportConfig())
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

ProjectBuilds::Step ProjectBuilds::exportStepMark(ExportStep step) {
    return [step](State &state) {
        state.exportStep = step;
        return true;
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
    m_state->exportStep = ExportStep::None;
    m_kind = kind;
    m_action = action;
    m_openWhenConfigured.reset();
    m_pendingExport.reset();
    m_lastResult = Result::Running;
    m_lastOutput.clear();
    if (m_output != nullptr && kind == BuildTargetKind::Scripts) { m_output->buildStarted(); }
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

void ProjectBuilds::openExportProject() {
    start(BuildTargetKind::Export, BuildAction::Configure, /*openWhenConfigured*/ true);
}

void ProjectBuilds::start(BuildTargetKind kind, BuildAction action, bool openWhenConfigured) {
    if (kind == BuildTargetKind::Scripts && action == BuildAction::Cook) { return; } // the game is cooked, not the scripts
    if (!begin(kind, action)) { return; }
    if (kind == BuildTargetKind::Export && action != BuildAction::Clean) {
        // The cook reads the files of the game on the main thread, and the editor stands still
        // meanwhile: it begins two frames later (update), when the screen says an export runs.
        m_state->exportStep = ExportStep::Cook;
        m_pendingExport = PendingExport{openWhenConfigured, 2};
        return;
    }
    launch(openWhenConfigured);
}

void ProjectBuilds::launch(bool openWhenConfigured) {
    const BuildTargetKind kind = m_kind;
    const BuildAction action = m_action;
    const bool scripts = kind == BuildTargetKind::Scripts;
    const path_t projectDirectory = folder(kind);
    const path_t buildDirectory = buildFolder(kind);
    std::vector<Step> steps;

    if (action == BuildAction::Clean) {
        steps.push_back(removeStep(buildDirectory, {}));
        run(std::move(steps));
        return;
    }
    // The Export project is built from the cooked data of the game: it is made first, and what
    // the cook said — where the game starts, the pack, what was left out — opens the output.
    if (!scripts) {
        const bool cooked = cook();
        for (const std::string &line : m_cookMessages) { m_state->pushLine(line); }
        if (!cooked) {
            endWithError("the data of the game was not cooked");
            return;
        }
        if (action == BuildAction::Cook) { // the data alone
            m_state->finished = Finished{/*success*/ true, /*canceled*/ false, /*ran*/ true};
            m_state->running = false;
            return;
        }
        m_state->exportStep = ExportStep::Configure; // what fails from here on is not the cook
    }
    if (!std::filesystem::exists(projectDirectory / "CMakeLists.txt")) {
        fail(std::string(scripts ? "scripts" : "export") + " project not found at " + projectDirectory.string());
        return;
    }
    const std::optional<Toolchain> toolchain = findToolchain();
    if (!toolchain) { return; }

    // A tree made with another generator or compiler is not one cmake goes on with: it is made
    // again. The Export project is an IDE project and takes neither from the toolchain.
    const bool configured =
        std::filesystem::exists(buildDirectory / "CMakeCache.txt") && std::filesystem::exists(buildDirectory / CONFIGURED_STAMP);
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
        if (!scripts) { steps.push_back(exportStepMark(ExportStep::Configure)); }
        std::vector<std::string> arguments;
        if (scripts) {
            arguments = {"-S", projectDirectory.string(), "-B", buildDirectory.string(),
                         std::string("-DCMAKE_BUILD_TYPE=") + buildTypeName(m_scriptsConfig),
                         "-DPandaSDK_DIR=" + toolchain->sdkConfigDirectory.string(), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"};
            arguments.insert(arguments.end(), toolchain->scriptsArguments.begin(), toolchain->scriptsArguments.end());
        } else {
            arguments =
                exportConfigureArguments(exportTarget(), projectDirectory, buildDirectory, toolchain->sdkConfigDirectory);
        }
        steps.push_back(commandStep(toolchain->cmake, std::move(arguments)));
        if (scripts) { steps.push_back(stampStep(buildDirectory, signature)); }
        steps.push_back([file = buildDirectory / CONFIGURED_STAMP](State &) {
            std::ofstream(file) << "configured\n";
            return true;
        });
    }
    if (action == BuildAction::Build) {
        std::vector<std::string> arguments = {"--build", buildDirectory.string()};
        // Xcode and Visual Studio are multi-configuration generators: the configuration is chosen
        // at the build.
        if (!scripts) {
            steps.push_back(exportStepMark(ExportStep::Build));
            arguments.insert(arguments.end(), {"--config", exportConfigName(exportConfig())});
        }
        arguments.insert(arguments.end(), {"-j", jobsArgument()});
        // A device build signed by the team of the Export project gets its profile from Apple as
        // Xcode would.
        if (!scripts && exportTarget() == ExportProjectTarget::XcodeIOSDevice) {
            arguments.insert(arguments.end(), {"--", "-allowProvisioningUpdates"});
        }
        steps.push_back(commandStep(toolchain->cmake, std::move(arguments)));
    }
    if (openWhenConfigured) {
        m_openWhenConfigured = OpenRequest{buildDirectory, exportTarget()};
        LOG_INFO_EDITOR(
            "Configuring the Export project (%s): the project opens when cmake is done.",
            exportBuildDirectoryName(exportTarget(), exportConfig()).c_str()
        );
    }
    run(std::move(steps));
}

void ProjectBuilds::update() {
    // An export that began: its cook, then its steps of cmake. Stopped before that — it ends here.
    if (m_pendingExport && --m_pendingExport->frames <= 0) {
        const bool openWhenConfigured = std::exchange(m_pendingExport, std::nullopt)->openWhenConfigured;
        if (m_state->cancelRequested) {
            m_state->finished = Finished{/*success*/ false, /*canceled*/ true, /*ran*/ false};
            m_state->running = false;
        } else {
            launch(openWhenConfigured);
        }
    }
    const bool scripts = m_kind == BuildTargetKind::Scripts;
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(m_state->linesMutex);
        lines = std::exchange(m_state->lines, {});
    }
    if (!lines.empty()) {
        if (m_output != nullptr && scripts) { m_output->buildOutput(lines); }
        m_lastOutput.insert(m_lastOutput.end(), lines.begin(), lines.end());
    }

    const std::optional<Finished> finished = std::exchange(m_state->finished, std::nullopt);
    if (!finished) { return; }
    m_lastResult = finished->canceled ? Result::Canceled : finished->success ? Result::Succeeded : Result::Failed;
    if (m_output != nullptr && scripts) { m_output->buildFinished(finished->success, finished->canceled); }
    const std::optional<OpenRequest> open = std::exchange(m_openWhenConfigured, std::nullopt);
    if (!finished->success) {
        if (finished->ran && !finished->canceled) {
            static const char *const ACTIONS[] = {"Configure", "Build", "Clean", "Cook"};
            LOG_ERROR_EDITOR(
                "%s of the %s project failed: see %s.", ACTIONS[static_cast<int>(m_action)], scripts ? "scripts" : "Export",
                scripts ? "the CMake panel" : "the Export window (File, Export Game)"
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
    ++m_cookCount;
    m_cookMessages.clear();
    // What the cook says goes to the log of the editor and is kept for the panel: an Export build
    // shows it before what cmake says.
    const auto error = [this](const std::string &text) {
        LOG_ERROR_EDITOR("Export: %s", text.c_str());
        m_cookMessages.push_back("error: " + text);
    };
    const auto warning = [this](const std::string &text) {
        LOG_WARN_EDITOR("Export: %s", text.c_str());
        m_cookMessages.push_back("warning: " + text);
    };
    if (!m_project.hasOpenedProject()) {
        error("No project is open. Open a project before exporting.");
        return false;
    }
    const path_t projectPath = m_project.getOpenedProjectPath();
    auto *assetHandler = AssetHandlerEditor::current();

    // The cook is a function of the files of the project: the game starts in the start world of
    // the project settings, whatever world the editor shows now, and carries what that world
    // reaches. An edit that is not saved is not in the game — the cook says which.
    const std::string startWorld = m_documents.projectSettings().get<ProjectWorldSettings>().startWorld;
    const std::vector<std::string> errors = GameExporter::validate(projectPath, startWorld, *assetHandler);
    if (!errors.empty()) {
        for (const std::string &text : errors) { error(text); }
        error("Nothing was exported: fix the errors above and export again.");
        return false;
    }

    {
        FileDocumentList documents = m_documents.list();
        std::string unsaved;
        int count = 0;
        for (FileDocument *document : documents.all) {
            if (!document->isDirty()) { continue; }
            unsaved += (count++ > 0 ? ", " : "") + document->title();
        }
        if (count > 0) {
            warning(
                std::to_string(count) + " document(s) have edits that are not saved, and the game is cooked from the files: " +
                unsaved + ". Save All and cook again to have them in the game."
            );
        }
    }

    ExportSettings settings;
    settings.runtimeDataDirectory = projectPath / ".Panda" / "Cooked" / "Runtime";
    settings.reportFile = cookReportFile();
    settings.startWorld = startWorld;
    settings.appName = projectPath.filename().string();
    settings.modules = ProjectModules::enabledIds(m_documents.projectSettings().get<ProjectModuleSettings>());
    LOG_INFO_EDITOR("Cooking the runtime data to %s", settings.runtimeDataDirectory.string().c_str());

    GameExporter exporter;
    const ExportResult result = exporter.exportGame(settings, projectPath, *assetHandler);
    // The exporter logged its warnings as it met them: the panel gets them here.
    for (const std::string &text : result.report.warnings) { m_cookMessages.push_back("warning: " + text); }
    if (!result.success) {
        error("The game was not cooked: " + result.errorMessage);
        return false;
    }
    const CookReport &report = result.report;
    const std::string summary = "The game is cooked: it starts in " + report.startWorld + "; " +
                                std::filesystem::path(report.pack).filename().string() + ", " +
                                CookReport::sizeText(report.packBytes) + " in " + std::to_string(report.packFiles) + " files.";
    LOG_INFO_EDITOR(
        "%s The Export project builds the game from it: %s", summary.c_str(),
        folder(BuildTargetKind::Export).string().c_str()
    );
    m_cookMessages.push_back(summary);
    // What the project has and the game does not: said every time, so that a file the game was
    // meant to have is noticed here and not in the game.
    if (!report.unused.empty()) {
        const std::string head = std::to_string(report.unused.size()) + " asset(s) of the project (" +
                                 CookReport::sizeText(report.unusedBytes) +
                                 ") are not reached from the start world and were left out of the game";
        const std::string advice = "The whole list is in " + settings.reportFile.string() +
                                   ". An asset the game needs is named by a world of the game: in a field of a "
                                   "component or of a script.";
        std::string names;
        const size_t shown = std::min<size_t>(report.unused.size(), 5);
        for (size_t i = 0; i < shown; i++) { names += (i > 0 ? ", " : "") + report.unused[i].path; }
        if (report.unused.size() > shown) { names += ", …"; }
        LOG_WARN_EDITOR("Export: %s: %s. %s", head.c_str(), names.c_str(), advice.c_str());
        // The panel shows a line as a line: the names go one under another.
        m_cookMessages.push_back("warning: " + head + ":");
        for (size_t i = 0; i < shown; i++) {
            m_cookMessages.push_back("    " + report.unused[i].path + "  (" + CookReport::sizeText(report.unused[i].bytes) + ")");
        }
        if (report.unused.size() > shown) {
            m_cookMessages.push_back("    … and " + std::to_string(report.unused.size() - shown) + " more");
        }
        m_cookMessages.push_back(
            "    The whole list: " + settings.reportFile.lexically_relative(projectPath).generic_string()
        );
        m_cookMessages.push_back(
            "    An asset the game needs is named by a world of the game: in a field of a component or of a script."
        );
    }
    return true;
}

ProjectBuilds::BuiltGame ProjectBuilds::builtGame() {
    BuiltGame game;
    if (!m_project.hasOpenedProject()) { return game; }
    const std::string name = m_project.getOpenedProjectPath().filename().string();
    const path_t tree = buildFolder(BuildTargetKind::Export);
    const std::string config = exportConfigName(exportConfig());
    // Where Xcode and Visual Studio put what they build: <tree>/<configuration>[-<sdk>]/.
    path_t executable;
    switch (exportTarget()) {
        case ExportProjectTarget::XcodeMacOS:
            game.product = tree / config / (name + ".app");
            executable = game.product / "Contents" / "MacOS" / name;
            break;
        case ExportProjectTarget::XcodeIOSSimulator: game.product = tree / (config + "-iphonesimulator") / (name + ".app"); break;
        case ExportProjectTarget::XcodeIOSDevice: game.product = tree / (config + "-iphoneos") / (name + ".app"); break;
        case ExportProjectTarget::VisualStudioWindows:
            game.product = tree / config / (name + ".exe");
            executable = game.product;
            break;
    }
    std::error_code error;
    if (!std::filesystem::exists(game.product, error)) { return {}; }
    if (std::filesystem::is_regular_file(executable, error)) { game.executable = executable; }
    return game;
}

bool ProjectBuilds::runBuiltGame() {
    const BuiltGame game = builtGame();
    if (game.executable.empty()) { return false; }
#if defined(PLATFORM_MACOS)
    // A bundle is started by the system: the game comes to the front as an application of its own.
    return SystemTools::openWithDefaultApplication(game.product);
#else
    return SystemTools::startDetached(game.executable, {});
#endif
}

path_t ProjectBuilds::cookReportFile() const {
    return m_project.getOpenedProjectPath() / ".Panda" / "Cooked" / "report.json";
}

bool ProjectBuilds::readCookReport(CookReport &report) const {
    std::ifstream file(cookReportFile());
    if (!file.is_open()) { return false; }
    Rain::JsonDecoder jsonDecoder;
    Rain::Decoder *decoder = &jsonDecoder;
    return decoder->decode(file, report);
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
