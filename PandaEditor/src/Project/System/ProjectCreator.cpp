#include "ProjectCreator.hpp"
#include "PandaSdkLocator.hpp"

#include "Project/Settings/ProjectSettings.hpp"
#include "Project/System/JsonFiles.hpp"

#include <Fern/Fern.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace Panda {

namespace {

std::string replaceAll(std::string str, const std::string &from, const std::string &to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

using TextReplacements = std::vector<std::pair<std::string, std::string>>;

// A task of Zed — an item of `.zed/tasks.json`; the fields are named by the keys of Zed.
struct ZedTask final : public Rain::Codable {
    std::string label;
    std::string command;
    std::vector<std::string> args;
    std::string cwd = "$ZED_WORKTREE_ROOT";
    bool use_new_terminal = false;
    bool allow_concurrent_runs = false;
    std::string reveal = "always";
    std::string hide = "never";

    RAIN_FIELDS_BEGIN(ZedTask)
    RAIN_FIELD(label)
    RAIN_FIELD(command)
    RAIN_FIELD(args)
    RAIN_FIELD(cwd)
    RAIN_FIELD(use_new_terminal)
    RAIN_FIELD(allow_concurrent_runs)
    RAIN_FIELD(reveal)
    RAIN_FIELD(hide)
    RAIN_FIELDS_END
};

std::string applyReplacements(std::string value, const TextReplacements &replacements) {
    for (const auto &[oldText, newText] : replacements) {
        value = replaceAll(value, oldText, newText);
    }
    return value;
}

void copyAndReplace(const path_t &src, const path_t &dest, const TextReplacements &replacements) {
    namespace fs = std::filesystem;
    for (const auto &entry : fs::recursive_directory_iterator(src)) {
        const auto &path = entry.path();
        if (path.filename() == ".DS_Store") { continue; }
        auto relativePath = fs::relative(path, src);
        auto newPath = dest / path_t(applyReplacements(relativePath.generic_string(), replacements));

        if (fs::is_directory(path)) {
            fs::create_directories(newPath);
        } else {
            fs::create_directories(newPath.parent_path());
            std::ifstream inFile(path, std::ios::binary);
            std::ofstream outFile(newPath, std::ios::binary);

            std::stringstream buffer;
            buffer << inFile.rdbuf();
            std::string content = buffer.str();

            content = applyReplacements(content, replacements);

            outFile << content;

            inFile.close();
            outFile.close();
            auto permissions = fs::status(path).permissions();
            if (newPath.extension() == ".sh") {
                permissions |= fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
            }
            fs::permissions(newPath, permissions);
        }
    }
}

// CMake presets are NOT generated: they carried the machine-specific PandaSDK_DIR path and broke the
// project build on another computer. The editor builds cmake commands from conventions (ProjectBuilder),
// the external scripts of the template take the SDK from the environment (PANDA_SDK_DIR).

void createZedTasks(const std::string &projectName, const path_t &projectPath) {
    const std::string scripts = "./Scripts/" + projectName + "/scripts/";
    const auto task = [&](std::string label, const char *script, std::string argument, bool newTerminal = false) {
        ZedTask result;
        result.label = std::move(label);
        result.command = scripts + script;
        if (!argument.empty()) { result.args.push_back(std::move(argument)); }
        result.use_new_terminal = newTerminal;
        return result;
    };
    // The export tasks open the project of a build target (macos-debug, ios-debug, …) in its IDE.
    const std::vector<ZedTask> tasks = {
        task("Configure " + projectName + " Debug", "configure_scripts.sh", "debug"),
        task("Build " + projectName + " Debug", "build_scripts.sh", "debug"),
        task("Configure " + projectName + " Release", "configure_scripts.sh", "release"),
        task("Build " + projectName + " Release", "build_scripts.sh", "release"),
        task("Open Export Xcode macOS", "open_export_xcode.sh", "macos-debug", true),
        task("Open Export Xcode iOS Simulator", "open_export_xcode.sh", "ios-debug", true),
        task("Open Export Xcode iOS Device", "open_export_xcode.sh", "ios-device-debug", true),
        task("Open Export Visual Studio Windows", "open_export_visual_studio.sh", "windows-debug", true),
        task("Format Code", "format_code.sh", ""),
    };

    const path_t zedDirectory = projectPath / ".zed";
    std::error_code created;
    std::filesystem::create_directories(zedDirectory, created);
    std::string error;
    if (!writeJsonFile(zedDirectory / "tasks.json", tasks, &error)) { LOG_ERROR("The tasks of Zed are not made: %s.", error.c_str()); }
}

void createRuntimeCookedDirectories(const path_t &projectPath) {
    const path_t runtimeDataPath = projectPath / ".Panda" / "Cooked" / "Runtime";
    std::filesystem::create_directories(runtimeDataPath / "Worlds");
    std::filesystem::create_directories(runtimeDataPath / "Textures");
    std::filesystem::create_directories(runtimeDataPath / "Shaders" / "cooked");
    std::filesystem::create_directories(runtimeDataPath / "Materials");
    std::filesystem::create_directories(runtimeDataPath / "Meshes");
}

// The session of a new project names the world it comes with (the one its project.json starts
// the game in). The editor opens the worlds of the session of a project and nothing else, so
// without the file a project that was just made would show no world. The template cannot carry
// it: the .gitignore a project gets from the template ignores the session.
void createSession(const path_t &projectPath) {
    const char *world = "Assets/world.world";
    ProjectSession session;
    session.worlds.push_back({});
    session.worlds.back().path = world;
    session.activeWorld = world;
    writeJsonFile(projectPath / ".Panda" / "session.json", session); // without it the project opens with no world
}

} // namespace

void ProjectCreator::createProject(const std::string &name, const Panda::path_t &path) {
    std::filesystem::create_directories(path);

    // No machine-specific paths in the project files: the editor resolves the SDK itself
    // (ProjectBuilder), the external scripts of the template — from the environment (PANDA_SDK_DIR).
    copyAndReplace(
        Fern::getStaticResourcesPath() / "Templates" / "GameProject",
        path,
        {
            {"___PROJECTNAME___", name},
            {"___BUNDLE_IDENTIFIER___", "com.company." + name},
            {"___VERSION___", "1.0.0"},
        }
    );

    createZedTasks(name, path);
    createRuntimeCookedDirectories(path);
    createSession(path);
}

} // namespace Panda
