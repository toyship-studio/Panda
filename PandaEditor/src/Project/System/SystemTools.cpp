#include "SystemTools.hpp"

#include "PandaSdkLocator.hpp"

#include <Fern/Fern.hpp>
#include <Foundation/Logger.hpp>
#include <Foundation/PlatformDetection.hpp>

#include <nfd.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>

#ifdef PLATFORM_POSIX
#    include <cstring>
#    include <spawn.h>
#    include <sys/wait.h>
extern char **environ;
#endif

#ifdef PLATFORM_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#endif

namespace Panda {

namespace {

    std::string quoteShellArg(const std::string &value) {
#ifdef PLATFORM_WINDOWS
        std::string result = "\"";
        for (char c : value) {
            if (c == '"') {
                result += "\\\"";
            } else {
                result += c;
            }
        }
        result += "\"";
        return result;
#else
        std::string result = "'";
        for (char c : value) {
            if (c == '\'') {
                result += "'\\''";
            } else {
                result += c;
            }
        }
        result += "'";
        return result;
#endif
    }

    std::vector<path_t> bundledCMakeRelativePaths() {
#ifdef PLATFORM_MACOS
#    if defined(__aarch64__) || defined(__arm64__)
        return {"Tools/cmake/mac/aarch64/bin/cmake"};
#    elif defined(__x86_64__)
        return {"Tools/cmake/mac/x86_64/bin/cmake"};
#    else
        return {};
#    endif
#elif defined(PLATFORM_LINUX)
        return {"Tools/cmake/linux/x86_64/bin/cmake"};
#elif defined(PLATFORM_WINDOWS)
        return {"Tools/cmake/windows/x86_64/bin/cmake.exe"};
#else
        return {};
#endif
    }

    std::vector<path_t> sdkCMakeRelativePaths() {
#ifdef PLATFORM_MACOS
#    if defined(__aarch64__) || defined(__arm64__)
        return {"tools/cmake/mac/aarch64/bin/cmake"};
#    elif defined(__x86_64__)
        return {"tools/cmake/mac/x86_64/bin/cmake"};
#    else
        return {};
#    endif
#elif defined(PLATFORM_LINUX)
        return {"tools/cmake/linux/x86_64/bin/cmake"};
#elif defined(PLATFORM_WINDOWS)
        return {"tools/cmake/windows/x86_64/bin/cmake.exe"};
#else
        return {};
#endif
    }

    // The bundle/SDK layout is the same for all tools (see cmake/bundled_slang.cmake).
    std::vector<path_t> bundledSlangRelativePaths() {
#ifdef PLATFORM_MACOS
#    if defined(__aarch64__) || defined(__arm64__)
        return {"Tools/slang/mac/aarch64/bin/slangc"};
#    elif defined(__x86_64__)
        return {"Tools/slang/mac/x86_64/bin/slangc"};
#    else
        return {};
#    endif
#elif defined(PLATFORM_LINUX)
        return {"Tools/slang/linux/x86_64/bin/slangc"};
#elif defined(PLATFORM_WINDOWS)
        return {"Tools/slang/windows/x86_64/bin/slangc.exe"};
#else
        return {};
#endif
    }

    std::vector<path_t> sdkSlangRelativePaths() {
        std::vector<path_t> paths = bundledSlangRelativePaths();
        for (path_t &path : paths) {
            path = path_t("tools") / path.lexically_relative("Tools");
        }
        return paths;
    }

    std::optional<path_t> findExistingExecutable(const path_t &basePath, const std::vector<path_t> &relativePaths) {
        for (const path_t &relativePath : relativePaths) {
            path_t candidate = basePath / relativePath;
            if (std::filesystem::exists(candidate)) { return candidate; }
        }
        return std::nullopt;
    }

    std::optional<path_t> bundledCMakePath() {
        return findExistingExecutable(Fern::getStaticResourcesPath(), bundledCMakeRelativePaths());
    }

    std::optional<path_t> sdkCMakePath() {
        std::optional<path_t> sdkConfigDirectory = findPandaSdkConfigDirectory();
        if (!sdkConfigDirectory.has_value()) { return std::nullopt; }

        path_t sdkRoot = std::filesystem::weakly_canonical(*sdkConfigDirectory / ".." / ".." / "..");
        return findExistingExecutable(sdkRoot, sdkCMakeRelativePaths());
    }

    std::optional<path_t> bundledSlangPath() {
        return findExistingExecutable(Fern::getStaticResourcesPath(), bundledSlangRelativePaths());
    }

    std::optional<path_t> sdkSlangPath() {
        std::optional<path_t> sdkConfigDirectory = findPandaSdkConfigDirectory();
        if (!sdkConfigDirectory.has_value()) { return std::nullopt; }

        path_t sdkRoot = std::filesystem::weakly_canonical(*sdkConfigDirectory / ".." / ".." / "..");
        return findExistingExecutable(sdkRoot, sdkSlangRelativePaths());
    }

    std::optional<path_t> pathCMakePath() {
        const char *pathEnvironment = std::getenv("PATH");
        if (pathEnvironment == nullptr) { return std::nullopt; }

#ifdef PLATFORM_WINDOWS
        const char separator = ';';
        const path_t executableName = "cmake.exe";
#else
        const char separator = ':';
        const path_t executableName = "cmake";
#endif

        std::string paths = pathEnvironment;
        size_t start = 0;
        while (start <= paths.size()) {
            size_t end = paths.find(separator, start);
            std::string directory = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!directory.empty()) {
                path_t candidate = path_t(directory) / executableName;
                if (std::filesystem::exists(candidate)) { return candidate; }
            }
            if (end == std::string::npos) { break; }
            start = end + 1;
        }

        return std::nullopt;
    }

    void emitCommandOutput(const SystemTools::CommandOutputCallback &callback, std::string line) {
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (callback) {
            callback(line);
        } else if (!line.empty()) {
            LOG_INFO("%s", line.c_str());
        }
    }

#ifdef PLATFORM_WINDOWS
    std::wstring widenUtf8(const std::string &utf8) {
        if (utf8.empty()) { return L""; }
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (length <= 1) { return L""; }
        std::wstring result(static_cast<size_t>(length - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), length);
        return result;
    }

    bool isValidUtf8(const std::string &text) {
        size_t i = 0;
        while (i < text.size()) {
            const unsigned char byte = static_cast<unsigned char>(text[i]);
            size_t continuation = 0;
            if (byte < 0x80) {
                ++i;
                continue;
            } else if ((byte >> 5) == 0x6) {
                continuation = 1;
            } else if ((byte >> 4) == 0xE) {
                continuation = 2;
            } else if ((byte >> 3) == 0x1E) {
                continuation = 3;
            } else {
                return false;
            }
            if (i + continuation >= text.size()) { return false; }
            for (size_t k = 1; k <= continuation; ++k) {
                if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) { return false; }
            }
            i += continuation + 1;
        }
        return true;
    }

    // MSVC, MSBuild and cmake write localized messages in the OEM code page when
    // piped; the UI renders UTF-8. Pass valid UTF-8 through, re-encode the rest.
    std::string decodeConsoleOutput(const std::string &line) {
        if (line.empty() || isValidUtf8(line)) { return line; }
        const int wideLength =
            MultiByteToWideChar(CP_OEMCP, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0);
        if (wideLength <= 0) { return line; }
        std::wstring wide(static_cast<size_t>(wideLength), L'\0');
        MultiByteToWideChar(CP_OEMCP, 0, line.c_str(), static_cast<int>(line.size()), wide.data(), wideLength);
        const int utf8Length =
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLength, nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0) { return line; }
        std::string utf8(static_cast<size_t>(utf8Length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLength, utf8.data(), utf8Length, nullptr, nullptr);
        return utf8;
    }

    // Windows argv quoting rules ("Everyone quotes command line arguments the
    // wrong way"): backslashes are literal unless they precede a double quote.
    void appendArgvQuoted(std::wstring &commandLine, const std::wstring &arg) {
        if (!commandLine.empty()) { commandLine += L' '; }
        if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
            commandLine += arg;
            return;
        }
        commandLine += L'"';
        size_t backslashes = 0;
        for (const wchar_t character : arg) {
            if (character == L'\\') {
                ++backslashes;
                continue;
            }
            if (character == L'"') {
                commandLine.append(backslashes * 2 + 1, L'\\');
            } else {
                commandLine.append(backslashes, L'\\');
            }
            backslashes = 0;
            commandLine += character;
        }
        commandLine.append(backslashes * 2, L'\\');
        commandLine += L'"';
    }

    // cmd.exe mangles quoted command lines (it strips the first and last quote
    // when the string starts with one), so processes are spawned directly.
    bool runProcessWindows(
        const path_t &executable,
        const std::vector<std::string> &args,
        const path_t &workingDirectory,
        const SystemTools::CommandOutputCallback &outputCallback,
        const std::atomic<bool> *cancelRequested
    ) {
        SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
        HANDLE pipeRead = nullptr;
        HANDLE pipeWrite = nullptr;
        if (!CreatePipe(&pipeRead, &pipeWrite, &security, 0)) { return false; }
        SetHandleInformation(pipeRead, HANDLE_FLAG_INHERIT, 0);

        std::wstring commandLine;
        appendArgvQuoted(commandLine, executable.wstring());
        for (const std::string &arg : args) {
            appendArgvQuoted(commandLine, widenUtf8(arg));
        }

        STARTUPINFOW startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdOutput = pipeWrite;
        startupInfo.hStdError = pipeWrite;
        PROCESS_INFORMATION processInfo = {};
        const std::wstring workingDirectoryWide = workingDirectory.empty() ? L"" : workingDirectory.wstring();
        const BOOL created = CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectoryWide.empty() ? nullptr : workingDirectoryWide.c_str(),
            &startupInfo,
            &processInfo
        );
        CloseHandle(pipeWrite); // the parent copy must close, or ReadFile never sees EOF
        if (!created) {
            CloseHandle(pipeRead);
            LOG_ERROR("Failed to run command: %ls", commandLine.c_str());
            return false;
        }
        CloseHandle(processInfo.hThread);

        bool canceled = false;
        std::string pending;
        std::array<char, 512> buffer{};
        DWORD bytesRead = 0;
        while (ReadFile(pipeRead, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
               bytesRead > 0) {
            if (cancelRequested && cancelRequested->load()) {
                canceled = true;
                TerminateProcess(processInfo.hProcess, 1);
                break;
            }
            pending.append(buffer.data(), bytesRead);
            size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos) {
                emitCommandOutput(outputCallback, decodeConsoleOutput(pending.substr(0, newline)));
                pending.erase(0, newline + 1);
            }
        }
        if (!canceled && !pending.empty()) { emitCommandOutput(outputCallback, decodeConsoleOutput(pending)); }

        WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hProcess);
        CloseHandle(pipeRead);
        return !canceled && exitCode == 0;
    }
#endif

    bool runShellCommand(
        const std::string &command,
        const SystemTools::CommandOutputCallback &outputCallback,
        const std::atomic<bool> *cancelRequested = nullptr
    ) {
#ifdef PLATFORM_MACOS
        FILE *pipe = popen((command + " 2>&1").c_str(), "r");
#elif defined(PLATFORM_POSIX)
        FILE *pipe = popen((command + " 2>&1").c_str(), "r");
#else
        FILE *pipe = _popen((command + " 2>&1").c_str(), "r");
#endif
        if (pipe == nullptr) {
            LOG_ERROR("Failed to run command: %s", command.c_str());
            return false;
        }

        bool canceled = false;
        std::array<char, 512> buffer{};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            if (cancelRequested && cancelRequested->load()) {
                canceled = true;
                break;
            }
            emitCommandOutput(outputCallback, buffer.data());
        }
        // On cancellation pclose closes the pipe (the process dies of SIGPIPE on its next write) and
        // waits for it to exit.
        if (canceled) {
#ifdef PLATFORM_WINDOWS
            _pclose(pipe);
#else
            pclose(pipe);
#endif
            return false;
        }

#ifdef PLATFORM_WINDOWS
        int result = _pclose(pipe);
        return result == 0;
#else
        int result = pclose(pipe);
        if (result == -1) { return false; }
        if (WIFEXITED(result)) { return WEXITSTATUS(result) == 0; }
        return false;
#endif
    }

} // namespace

std::optional<path_t> SystemTools::openFolderDialog(const char *initialFolder) {
    NFD::Init();
    nfdu8char_t *outPath;
    nfdresult_t result = NFD::PickFolder(outPath);
    std::optional<std::filesystem::path> res;
    if (result == NFD_OKAY) {
        res = outPath;
        NFD::FreePath(outPath);
    } else if (result == NFD_CANCEL) {
        LOG_INFO("User pressed cancel.");
    } else {
        LOG_ERROR("Error: %s", NFD::GetError());
    }
    NFD::Quit();
    return res;
}

path_t SystemTools::userDataDirectory() {
#ifdef PLATFORM_WINDOWS
    const char *base = std::getenv("APPDATA");
    if (base == nullptr || *base == '\0') { return {}; }
    return path_t(base) / "Panda";
#else
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') { return {}; }
#    ifdef PLATFORM_MACOS
    return path_t(home) / "Library" / "Application Support" / "Panda";
#    else
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') { return path_t(xdg) / "Panda"; }
    return path_t(home) / ".local" / "share" / "Panda";
#    endif
#endif
}

std::optional<path_t> SystemTools::openFileDialog() {
    NFD::Init();
    nfdu8char_t *outPath;
    nfdresult_t result = NFD::OpenDialog(outPath);
    std::optional<std::filesystem::path> res;
    if (result == NFD_OKAY) {
        res = outPath;
        NFD::FreePath(outPath);
    } else if (result == NFD_CANCEL) {
        LOG_INFO("User pressed cancel.");
    } else {
        LOG_ERROR("Error: %s", NFD::GetError());
    }
    NFD::Quit();
    return res;
}

std::optional<path_t>
SystemTools::saveFileDialog(const char *filter, const char *defaultPath, const char *defaultName) {
    NFD::Init();
    nfdu8char_t *outPath;
    nfdresult_t result = NFD::SaveDialog(outPath, nullptr, 0, defaultPath, defaultName);
    std::optional<std::filesystem::path> res;
    if (result == NFD_OKAY) {
        res = outPath;
        NFD::FreePath(outPath);
    } else if (result == NFD_CANCEL) {
        LOG_INFO("User pressed cancel.");
    } else {
        LOG_ERROR("Error: %s", NFD::GetError());
    }
    NFD::Quit();
    return res;
}

void SystemTools::copyFolder(const path_t &sourcePath, const path_t &newPath) {
#ifdef PLATFORM_POSIX
    std::string command = "cp -rf " + sourcePath.string() + " " + newPath.string();
    system(command.c_str());
#elif defined(PLATFORM_WINDOWS)
    std::string command = "copy " + sourcePath.string() + " " + newPath.string();
    system(command.c_str());
#else
#    error Unsupported platrofm
#endif
}

void SystemTools::open(const path_t &path) {
#ifdef PLATFORM_POSIX
    std::string command = "open -R " + path.string();
    system(command.c_str());
#elif defined(PLATFORM_WINDOWS)
    std::string command = "start " + path.string();
    system(command.c_str());
#else
#    error Unsupported platrofm
#endif
}

bool SystemTools::openWithDefaultApplication(const path_t &path) {
#ifdef PLATFORM_MACOS
    return runCommand("/usr/bin/open", {path.string()});
#elif defined(PLATFORM_LINUX)
    return runCommand("xdg-open", {path.string()});
#elif defined(PLATFORM_WINDOWS)
    return runCommand("cmd.exe", {"/C", "start", "", path.string()});
#else
#    error Unsupported platrofm
#endif
}

void SystemTools::show(const Panda::path_t &path) {
#ifdef PLATFORM_POSIX
    std::string command = "open -R \"" + path.string() + "\"";
    system(command.c_str());
#elif defined(PLATFORM_WINDOWS)
    std::string command = "explorer /select, \"" + path.string() + "\"";
    system(command.c_str());
#else
#    error Unsupported platrofm
#endif
}

void SystemTools::openCppProject(const Panda::path_t &path) {
#ifdef PLATFORM_MACOS
    if (std::filesystem::exists("/Applications/CLion.app")) {
        std::string command = "/Applications/CLion.app/Contents/MacOS/clion \"" + path.string() + "\"";
        system(command.c_str());
    } else if (std::filesystem::exists("/Applications/Visual Studio Code.app")) {
        std::string command = "/Applications/Visual Studio Code.app/Contents/MacOS/Electron \"" + path.string() + "\"";
        system(command.c_str());
    }
#endif
}

#ifdef PLATFORM_WINDOWS
// Native dialogs for Windows (macOS lives in SystemToolsDialog.mm via NSAlert).
namespace {

    std::wstring toWideString(const char *utf8) {
        if (!utf8 || !*utf8) { return L""; }
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (length <= 1) { return L""; }
        std::wstring result(static_cast<size_t>(length - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, result.data(), length);
        return result;
    }

} // namespace

SystemTools::SaveChangesChoice SystemTools::showSaveChangesDialog(const char *title, const char *message) {
    const std::wstring wideTitle = toWideString(title);
    const std::wstring wideMessage = toWideString(message);
    const int choice =
        MessageBoxW(GetActiveWindow(), wideMessage.c_str(), wideTitle.c_str(), MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDYES) { return SaveChangesChoice::Save; }
    if (choice == IDNO) { return SaveChangesChoice::DontSave; }
    return SaveChangesChoice::Cancel;
}

#endif

std::optional<path_t> SystemTools::findCMakeExecutable() {
    if (std::optional<path_t> path = bundledCMakePath()) { return path; }
    if (std::optional<path_t> path = sdkCMakePath()) { return path; }
    return pathCMakePath();
}

std::optional<path_t> SystemTools::findSlangExecutable() {
    // PATH is not tried: slangc is not installed system-wide — only the bundle and the SDK
    // (cmake/bundled_slang.cmake).
    if (std::optional<path_t> path = bundledSlangPath()) { return path; }
    return sdkSlangPath();
}

bool SystemTools::runCommand(
    const path_t &executable,
    const std::vector<std::string> &args,
    const path_t &workingDirectory,
    CommandOutputCallback outputCallback,
    const std::atomic<bool> *cancelRequested
) {
    if (!workingDirectory.empty() && !std::filesystem::exists(workingDirectory)) {
        LOG_ERROR("Working directory not found: %s", workingDirectory.string().c_str());
        return false;
    }

#ifdef PLATFORM_WINDOWS
    return runProcessWindows(executable, args, workingDirectory, outputCallback, cancelRequested);
#else
    std::string command;
    if (!workingDirectory.empty()) { command += "cd " + quoteShellArg(workingDirectory.string()) + " && "; }
    command += quoteShellArg(executable.string());
    for (const std::string &arg : args) {
        command += " " + quoteShellArg(arg);
    }

    return runShellCommand(command, outputCallback, cancelRequested);
#endif
}

namespace {
    std::atomic<bool> s_relaunchOnExit{false};

#ifdef PLATFORM_POSIX
    // Starts a process without waiting for it or wiring its pipes.
    bool spawnDetached(const std::string &executable, const std::vector<std::string> &args) {
        std::vector<std::string> storage;
        storage.reserve(args.size() + 1);
        storage.push_back(executable);
        storage.insert(storage.end(), args.begin(), args.end());
        std::vector<char *> argv;
        for (std::string &value : storage) { argv.push_back(value.data()); }
        argv.push_back(nullptr);
        pid_t pid = 0;
        const int result = posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv.data(), environ);
        if (result != 0) {
            LOG_ERROR("Failed to start %s: %s", executable.c_str(), strerror(result));
            return false;
        }
        return true;
    }
#endif
} // namespace

void SystemTools::setRelaunchOnExit(bool relaunch) {
    s_relaunchOnExit = relaunch;
}

bool SystemTools::relaunchOnExit() {
    return s_relaunchOnExit;
}

bool SystemTools::relaunchApplication() {
#ifdef PLATFORM_MACOS
    // Contents/Resources is two levels below the bundle; `open -n` starts a second instance while
    // this one is exiting.
    const path_t bundle = Fern::getStaticResourcesPath().parent_path().parent_path();
    return spawnDetached("/usr/bin/open", {"-n", bundle.string()});
#elif defined(PLATFORM_LINUX)
    std::error_code error;
    const path_t executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        LOG_ERROR("Failed to resolve the executable for the restart: %s", error.message().c_str());
        return false;
    }
    return spawnDetached(executable.string(), {});
#elif defined(PLATFORM_WINDOWS)
    std::wstring executable(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        LOG_ERROR("Failed to resolve the executable for the restart");
        return false;
    }
    executable.resize(length);
    std::wstring commandLine;
    appendArgvQuoted(commandLine, executable);
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo)) {
        LOG_ERROR("Failed to start %ls", executable.c_str());
        return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
#else
    return false;
#endif
}

} // namespace Panda
