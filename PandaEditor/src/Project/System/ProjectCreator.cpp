//
// Created by Michael Andreichev on 08.09.2024.
//

#include "ProjectCreator.hpp"
#include "PandaSdkLocator.hpp"

#include <Fern/Fern.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace Panda {

std::string replaceAll(std::string str, const std::string &from, const std::string &to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

using TextReplacements = std::vector<std::pair<std::string, std::string>>;

struct ZedTaskDefinition {
    std::string label;
    std::string command;
    std::vector<std::string> args;
    std::string cwd;
    bool useNewTerminal = false;
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

std::string escapeJsonString(std::string value) {
    value = replaceAll(value, "\\", "\\\\");
    value = replaceAll(value, "\"", "\\\"");
    return value;
}

void writeJsonString(std::ostream &file, const std::string &value) {
    file << "\"" << escapeJsonString(value) << "\"";
}

void writeJsonStringArray(std::ostream &file, const std::vector<std::string> &values, int indent) {
    const std::string indentation(indent, ' ');
    const std::string itemIndentation(indent + 2, ' ');
    file << "[\n";
    for (size_t i = 0; i < values.size(); ++i) {
        file << itemIndentation;
        writeJsonString(file, values[i]);
        if (i + 1 < values.size()) { file << ","; }
        file << "\n";
    }
    file << indentation << "]";
}

void writeZedTask(std::ostream &file, const ZedTaskDefinition &task, bool isLast) {
    file << "  {\n";
    file << "    \"label\": ";
    writeJsonString(file, task.label);
    file << ",\n";
    file << "    \"command\": ";
    writeJsonString(file, task.command);
    file << ",\n";
    file << "    \"args\": ";
    writeJsonStringArray(file, task.args, 4);
    file << ",\n";
    file << "    \"cwd\": ";
    writeJsonString(file, task.cwd);
    file << ",\n";
    file << "    \"use_new_terminal\": " << (task.useNewTerminal ? "true" : "false") << ",\n";
    file << "    \"allow_concurrent_runs\": false,\n";
    file << "    \"reveal\": \"always\",\n";
    file << "    \"hide\": \"never\"\n";
    file << "  }";
    if (!isLast) { file << ","; }
    file << "\n";
}

// CMake presets are NOT generated: they carried the machine-specific PandaSDK_DIR path and broke the
// project build on another computer. The editor builds cmake commands from conventions (ProjectBuilder),
// the external scripts of the template take the SDK from the environment (PANDA_SDK_DIR).

void createZedTasks(const std::string &projectName, const path_t &projectPath) {
    const std::string rootCwd = "$ZED_WORKTREE_ROOT";
    const std::string scriptsPath = "./Scripts/" + projectName + "/scripts/";

    std::vector<ZedTaskDefinition> tasks = {
        {
            "Configure " + projectName + " Debug",
            scriptsPath + "configure_scripts.sh",
            {"debug"},
            rootCwd,
        },
        {
            "Build " + projectName + " Debug",
            scriptsPath + "build_scripts.sh",
            {"debug"},
            rootCwd,
        },
        {
            "Configure " + projectName + " Release",
            scriptsPath + "configure_scripts.sh",
            {"release"},
            rootCwd,
        },
        {
            "Build " + projectName + " Release",
            scriptsPath + "build_scripts.sh",
            {"release"},
            rootCwd,
        },
    };

    struct ExportOpenTaskDefinition {
        std::string label;
        std::string scriptName;
        std::string target; // name of the build target/directory (macos-debug, ios-debug, …)
    };

    const std::vector<ExportOpenTaskDefinition> exportOpenTasks = {
        {"Open Export Xcode macOS", "open_export_xcode.sh", "macos-debug"},
        {"Open Export Xcode iOS Simulator", "open_export_xcode.sh", "ios-debug"},
        {"Open Export Xcode iOS Device", "open_export_xcode.sh", "ios-device-debug"},
        {"Open Export Visual Studio Windows", "open_export_visual_studio.sh", "windows-debug"},
    };

    for (const auto &exportOpenTask : exportOpenTasks) {
        tasks.push_back({
            exportOpenTask.label,
            scriptsPath + exportOpenTask.scriptName,
            {exportOpenTask.target},
            rootCwd,
            true,
        });
    }

    tasks.push_back({
        "Format Code",
        scriptsPath + "format_code.sh",
        {},
        rootCwd,
    });

    const path_t zedDirectory = projectPath / ".zed";
    std::filesystem::create_directories(zedDirectory);

    std::ofstream file(zedDirectory / "tasks.json");
    if (!file.is_open()) {
        LOG_ERROR("FAILED TO CREATE .zed/tasks.json FOR PROJECT %s", projectName.c_str());
        return;
    }

    file << "[\n";
    for (size_t i = 0; i < tasks.size(); ++i) {
        writeZedTask(file, tasks[i], i + 1 == tasks.size());
    }
    file << "]\n";
}

void createRuntimeCookedDirectories(const path_t &projectPath) {
    const path_t runtimeDataPath = projectPath / ".Panda" / "Cooked" / "Runtime";
    std::filesystem::create_directories(runtimeDataPath / "Worlds");
    std::filesystem::create_directories(runtimeDataPath / "Textures");
    std::filesystem::create_directories(runtimeDataPath / "Shaders" / "cooked");
    std::filesystem::create_directories(runtimeDataPath / "Materials");
    std::filesystem::create_directories(runtimeDataPath / "Meshes");
}

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
}

} // namespace Panda
