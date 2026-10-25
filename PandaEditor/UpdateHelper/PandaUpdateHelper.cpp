// PandaUpdateHelper (Windows): finishes an editor update after the editor exited
// (context/infrastructure.md, section 5; App/EditorUpdater.cpp). The running executable cannot be
// replaced on Windows, so the editor starts this program from the updates folder and quits; it
// waits for the process, moves the install folder aside, moves the unpacked new build into place,
// rolls back when that fails, removes the old folder and starts the new editor when asked.
//
//   PandaUpdateHelper.exe --wait <pid> --install <folder> --staged <folder> [--launch]

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::ofstream g_log;

void log(const std::wstring &line) {
    if (!g_log.is_open()) { return; }
    const int length = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), int(line.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(size_t(length > 0 ? length : 0), '\0');
    if (length > 0) { WideCharToMultiByte(CP_UTF8, 0, line.c_str(), int(line.size()), utf8.data(), length, nullptr, nullptr); }
    g_log << utf8 << std::endl;
}

// Repeats an operation while the files are still held by the exiting editor.
bool retry(const std::function<bool()> &operation, std::chrono::seconds limit) {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        if (operation()) { return true; }
        if (std::chrono::steady_clock::now() - start > limit) { return false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

bool moveDirectory(const fs::path &from, const fs::path &to, std::wstring &error) {
    std::error_code code;
    fs::rename(from, to, code);
    if (!code) { return true; }
    // Another volume: copy, then remove the source.
    fs::copy(from, to, fs::copy_options::recursive, code);
    if (code) {
        error = fs::path(code.message()).wstring();
        fs::remove_all(to, code);
        return false;
    }
    fs::remove_all(from, code);
    return true;
}

int fail(const std::wstring &message) {
    log(L"FAILED: " + message);
    MessageBoxW(nullptr, (L"The Panda Editor update could not be finished.\n\n" + message + L"\n\nThe previous version is kept.").c_str(),
                L"Panda Editor Update", MB_OK | MB_ICONERROR);
    return 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    DWORD pid = 0;
    fs::path install;
    fs::path staged;
    bool launch = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--wait" && i + 1 < argc) {
            pid = DWORD(_wtoi(argv[++i]));
        } else if (arg == L"--install" && i + 1 < argc) {
            install = argv[++i];
        } else if (arg == L"--staged" && i + 1 < argc) {
            staged = argv[++i];
        } else if (arg == L"--launch") {
            launch = true;
        }
    }
    LocalFree(argv);
    if (install.empty() || staged.empty()) { return fail(L"The install and the staged folders were not given."); }

    g_log.open(staged.parent_path().parent_path() / "update-helper.log", std::ios::app);
    log(L"==== update: " + staged.wstring() + L" -> " + install.wstring());

    if (pid != 0) {
        if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
            WaitForSingleObject(process, 120000);
            CloseHandle(process);
        }
    }
    std::error_code code;
    if (!fs::is_directory(staged, code)) { return fail(L"The new build is missing at " + staged.wstring()); }

    fs::path old = install;
    old += L".old";
    fs::remove_all(old, code);
    // 1. The current install moves aside; it comes back when anything after this fails.
    if (!retry([&] { std::error_code error; fs::rename(install, old, error); return !error; }, std::chrono::seconds(60))) {
        return fail(L"The editor folder is still in use: " + install.wstring());
    }
    log(L"moved aside");
    // 2. The new build takes its place.
    std::wstring error;
    if (!moveDirectory(staged, install, error)) {
        fs::remove_all(install, code);
        fs::rename(old, install, code);
        return fail(L"The new build could not be moved into place: " + error);
    }
    log(L"new build in place");
    // 3. The old folder goes; the editor removes it on its next start if this fails.
    retry([&] { std::error_code e; fs::remove_all(old, e); return !fs::exists(old, e); }, std::chrono::seconds(10));
    fs::remove_all(staged.parent_path(), code);

    if (launch) {
        const fs::path editor = install / L"PandaEditor.exe";
        ShellExecuteW(nullptr, L"open", editor.c_str(), nullptr, install.c_str(), SW_SHOWNORMAL);
        log(L"launched");
    }
    log(L"done");
    return 0;
}
