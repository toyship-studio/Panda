#include "SystemTools.hpp"

#include "PandaSdkLocator.hpp"

#include <Fern/Fern.hpp>
#include <Foundation/Logger.hpp>
#include <Foundation/PlatformDetection.hpp>

#include <nfd.hpp>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#ifdef PLATFORM_POSIX
#    include <cstring>
#    include <spawn.h>
#    include <sys/wait.h>
#    include <unistd.h>
extern char **environ;
#endif

#ifdef PLATFORM_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#    include <shellapi.h>
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

    // An executable by its name: the folders of PATH, then the usual install folders — a GUI
    // application on macOS does not inherit the PATH of the user's shell.
    std::optional<path_t> findInPath(const std::string &name) {
#ifdef PLATFORM_WINDOWS
        const char separator = ';';
        const std::vector<std::string> names = {name + ".exe", name + ".cmd", name + ".bat", name};
#else
        const char separator = ':';
        const std::vector<std::string> names = {name};
#endif
        std::vector<path_t> directories;
        if (const char *pathEnvironment = std::getenv("PATH")) {
            const std::string paths = pathEnvironment;
            size_t start = 0;
            while (start <= paths.size()) {
                const size_t end = paths.find(separator, start);
                const std::string directory =
                    paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!directory.empty()) { directories.emplace_back(directory); }
                if (end == std::string::npos) { break; }
                start = end + 1;
            }
        }
#ifdef PLATFORM_POSIX
        directories.emplace_back("/usr/local/bin");
        directories.emplace_back("/opt/homebrew/bin");
        if (const char *home = std::getenv("HOME")) { directories.push_back(path_t(home) / ".local" / "bin"); }
#endif
        for (const path_t &directory : directories) {
            for (const std::string &candidateName : names) {
                std::error_code errorCode;
                const path_t candidate = directory / candidateName;
                if (std::filesystem::is_regular_file(candidate, errorCode)) { return candidate; }
            }
        }
        return std::nullopt;
    }

    std::optional<path_t> pathCMakePath() {
        return findInPath("cmake");
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

namespace {
    SystemTools::Unattended *s_unattended = nullptr; // the innermost scope; the main thread only
}

SystemTools::Unattended::Unattended(SaveChangesChoice answer)
    : m_answer(answer)
    , m_outer(s_unattended) {
    s_unattended = this;
}

SystemTools::Unattended::~Unattended() {
    s_unattended = m_outer;
}

bool SystemTools::answerUnattended(const char *question, SaveChangesChoice *choice) {
    if (s_unattended == nullptr) { return false; }
    s_unattended->m_questions.emplace_back(question != nullptr ? question : "");
    if (choice != nullptr) { *choice = s_unattended->m_answer; }
    return true;
}

std::optional<path_t> SystemTools::openFolderDialog(const char *initialFolder) {
    if (answerUnattended("Choose a folder")) { return std::nullopt; }
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

namespace {
    path_t s_userDataDirectory; // --user-data; empty — the folder of the system
}

void SystemTools::setUserDataDirectory(path_t directory) {
    s_userDataDirectory = std::move(directory);
}

bool SystemTools::isUserDataDirectoryOverridden() {
    return !s_userDataDirectory.empty();
}

path_t SystemTools::userDataDirectory() {
    if (!s_userDataDirectory.empty()) { return s_userDataDirectory; }
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
    if (answerUnattended("Choose a file")) { return std::nullopt; }
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
    if (answerUnattended("Choose where to save the file")) { return std::nullopt; }
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

bool SystemTools::openWithDefaultApplication(const path_t &path) {
#ifdef PLATFORM_MACOS
    return runCommand("/usr/bin/open", {path.string()});
#elif defined(PLATFORM_LINUX)
    return runCommand("xdg-open", {path.string()});
#elif defined(PLATFORM_WINDOWS)
    return runCommand("cmd.exe", {"/C", "start", "", path.string()});
#else
#    error Unsupported platform
#endif
}

namespace {

    // The words of a command line: blanks part them, double quotes keep the blanks of a word.
    std::vector<std::string> wordsOf(const std::string &text) {
        std::vector<std::string> words;
        std::string word;
        bool quoted = false, any = false;
        for (const char c : text) {
            if (c == '"') {
                quoted = !quoted;
                any = true;
            } else if ((c == ' ' || c == '\t') && !quoted) {
                if (any) { words.push_back(std::move(word)); }
                word.clear();
                any = false;
            } else {
                word += c;
                any = true;
            }
        }
        if (any) { words.push_back(std::move(word)); }
        return words;
    }

    void replaceAll(std::string &text, const std::string &what, const std::string &with) {
        for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size())) {
            text.replace(at, what.size(), with);
        }
    }

    // What an editor goes by: the name of its command, without the folder and the extension.
    std::string editorName(const std::string &command) {
        std::string name = path_t(command).stem().string();
        for (char &c : name) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return name;
    }

    bool isOneOf(const std::string &name, std::initializer_list<const char *> names) {
        for (const char *known : names) {
            if (name == known) { return true; }
        }
        return false;
    }

    // How an editor is told the file and the place in it. One this code does not know gets the
    // file alone: a place written its own way would be opened as a file of that name.
    std::vector<std::string> editorArguments(const std::string &name, const path_t &path, int line, int column) {
        const std::string file = path.string();
        if (line <= 0) { return {file}; }
        const std::string place = file + ":" + std::to_string(line) + (column > 0 ? ":" + std::to_string(column) : "");
        if (isOneOf(name, {"zed", "zeditor", "zed-preview", "subl", "sublime_text"})) { return {place}; }
        if (isOneOf(name, {"code", "code-insiders", "codium", "cursor", "windsurf"})) { return {"--goto", place}; }
        if (name == "xed") { return {"--line", std::to_string(line), file}; }
        return {file};
    }

#ifdef PLATFORM_MACOS
    // An editor that is installed as an application only, with no command in PATH: the title of
    // the application, and where its command line tool is inside the bundle.
    struct EditorApplication {
        const char *command;
        const char *title;
        const char *tool;
    };
    constexpr EditorApplication EDITOR_APPLICATIONS[] = {
        {"zed", "Zed", "Contents/MacOS/cli"},
        {"code", "Visual Studio Code", "Contents/Resources/app/bin/code"},
        {"cursor", "Cursor", "Contents/Resources/app/bin/cursor"},
        {"subl", "Sublime Text", "Contents/SharedSupport/bin/subl"},
    };

    std::optional<path_t> toolOfApplication(const std::string &name) {
        std::vector<path_t> folders = {"/Applications"};
        if (const char *home = std::getenv("HOME")) { folders.push_back(path_t(home) / "Applications"); }
        for (const EditorApplication &application : EDITOR_APPLICATIONS) {
            if (name != application.command) { continue; }
            for (const path_t &folder : folders) {
                std::error_code errorCode;
                const path_t tool = folder / (std::string(application.title) + ".app") / application.tool;
                if (std::filesystem::is_regular_file(tool, errorCode)) { return tool; }
            }
        }
        return std::nullopt;
    }
#endif

} // namespace

void SystemTools::openInEditor(const std::string &editor, const path_t &path, int line, int column) {
    if (editor.empty()) {
        openWithDefaultApplication(path);
        return;
    }
    // A command line of the user's own, or a command whose arguments are known here.
    std::string command = editor;
    std::vector<std::string> arguments;
    if (editor.find("{file}") != std::string::npos) {
        std::vector<std::string> words = wordsOf(editor);
        command = words.empty() ? std::string() : words.front();
        for (size_t i = 1; i < words.size(); i++) {
            replaceAll(words[i], "{file}", path.string());
            replaceAll(words[i], "{line}", std::to_string(line > 0 ? line : 1));
            replaceAll(words[i], "{column}", std::to_string(column > 0 ? column : 1));
            arguments.push_back(std::move(words[i]));
        }
    } else {
        arguments = editorArguments(editorName(editor), path, line, column);
    }
    std::optional<path_t> executable = findExecutable(command);
#ifdef PLATFORM_MACOS
    if (!executable) { executable = toolOfApplication(editorName(command)); }
#endif
    if (executable) {
#ifdef PLATFORM_WINDOWS
        // A command of an editor is often a .cmd script: the shell starts both it and an .exe,
        // without waiting for the editor to close.
        std::vector<std::string> shell = {"/C", "start", "", executable->string()};
        shell.insert(shell.end(), arguments.begin(), arguments.end());
        if (runCommand("cmd.exe", shell)) { return; }
#else
        if (startDetached(*executable, arguments)) { return; }
#endif
    }
#ifdef PLATFORM_MACOS
    // No command line tool at all: the application itself, by its name or the path of its
    // bundle. It is given the file; a place cannot be told this way.
    std::string application = command;
    for (const EditorApplication &known : EDITOR_APPLICATIONS) {
        if (command == known.command) { application = known.title; }
    }
    if (runCommand("/usr/bin/open", {"-a", application, path.string()})) { return; }
#endif
    LOG_WARN_EDITOR("External editor \"%s\" was not found: the file is opened by the system", command.c_str());
    openWithDefaultApplication(path);
}

void SystemTools::revealInFileManager(const path_t &path) {
#ifdef PLATFORM_MACOS
    runCommand("/usr/bin/open", {"-R", path.string()});
#elif defined(PLATFORM_LINUX)
    // No common way to select a file: its folder opens.
    std::error_code errorCode;
    runCommand("xdg-open", {(std::filesystem::is_directory(path, errorCode) ? path : path.parent_path()).string()});
#elif defined(PLATFORM_WINDOWS)
    std::string command = "explorer /select, \"" + path.string() + "\"";
    system(command.c_str());
#else
#    error Unsupported platform
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
    if (SaveChangesChoice unattended; answerUnattended(title, &unattended)) { return unattended; }
    const std::wstring wideTitle = toWideString(title);
    const std::wstring wideMessage = toWideString(message);
    const int choice =
        MessageBoxW(GetActiveWindow(), wideMessage.c_str(), wideTitle.c_str(), MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDYES) { return SaveChangesChoice::Save; }
    if (choice == IDNO) { return SaveChangesChoice::DontSave; }
    return SaveChangesChoice::Cancel;
}

#endif

std::optional<path_t> SystemTools::findExecutable(const std::string &command) {
    if (command.empty()) { return std::nullopt; }
    std::error_code errorCode;
    if (std::filesystem::is_regular_file(command, errorCode)) { return path_t(command); }
    return findInPath(command);
}

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
    std::vector<std::string> s_relaunchArguments;

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

bool SystemTools::openUrl(const std::string &url) {
#ifdef PLATFORM_MACOS
    return spawnDetached("/usr/bin/open", {url});
#elif defined(PLATFORM_LINUX)
    return spawnDetached("/usr/bin/xdg-open", {url});
#elif defined(PLATFORM_WINDOWS)
    const std::wstring wide = path_t(url).wstring();
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
    return false;
#endif
}

bool SystemTools::startDetached(const path_t &executable, const std::vector<std::string> &args) {
#ifdef PLATFORM_POSIX
    return spawnDetached(executable.string(), args);
#elif defined(PLATFORM_WINDOWS)
    std::wstring commandLine;
    appendArgvQuoted(commandLine, executable.wstring());
    for (const std::string &arg : args) {
        commandLine += L' ';
        appendArgvQuoted(commandLine, path_t(arg).wstring());
    }
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startupInfo, &processInfo)) {
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

uint32_t SystemTools::currentProcessId() {
#ifdef PLATFORM_WINDOWS
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

void SystemTools::setRelaunchOnExit(bool relaunch, std::vector<std::string> arguments) {
    s_relaunchArguments = std::move(arguments);
    s_relaunchOnExit = relaunch;
}

bool SystemTools::relaunchOnExit() {
    return s_relaunchOnExit;
}

const std::vector<std::string> &SystemTools::relaunchArguments() {
    return s_relaunchArguments;
}

bool SystemTools::startNewInstance(const std::vector<std::string> &args) {
#ifdef PLATFORM_MACOS
    // Contents/Resources is two levels below the bundle; `open -n` starts a second instance while
    // this one is exiting.
    const path_t bundle = Fern::getStaticResourcesPath().parent_path().parent_path();
    std::vector<std::string> openArgs = {"-n", bundle.string()};
    if (!args.empty()) {
        openArgs.push_back("--args");
        openArgs.insert(openArgs.end(), args.begin(), args.end());
    }
    return spawnDetached("/usr/bin/open", openArgs);
#elif defined(PLATFORM_LINUX)
    std::error_code error;
    const path_t executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        LOG_ERROR("Failed to resolve the executable for the restart: %s", error.message().c_str());
        return false;
    }
    return spawnDetached(executable.string(), args);
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
    for (const std::string &arg : args) {
        commandLine += L' ';
        appendArgvQuoted(commandLine, widenUtf8(arg));
    }
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
