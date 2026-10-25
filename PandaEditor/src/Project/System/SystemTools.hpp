#pragma once

#include <Panda/Assets/Base/Path.hpp>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Panda {

class SystemTools {
public:
    using CommandOutputCallback = std::function<void(std::string_view)>;

    // The user data folder of the editor (not of the project): macOS ~/Library/Application Support/Panda,
    // Windows %APPDATA%/Panda, Linux $XDG_DATA_HOME|~/.local/share/Panda. Plugins —
    // <folder>/Plugins. Empty — the home folder could not be determined.
    static path_t userDataDirectory();
    static std::optional<path_t> openFileDialog();
    static std::optional<path_t> openFolderDialog(const char *initialFolder = "");
    static std::optional<path_t> saveFileDialog(
        const char *filter = "All\0*.*\0", const char *defaultPath = nullptr, const char *defaultName = nullptr
    );

    // A native BLOCKING dialog — only where the answer is needed synchronously in the middle of the
    // close flow (a tab/application with unsaved changes). Everything else is PandaUI modals
    // (UI/EditorDialogs). macOS: NSAlert; Windows: MessageBox; Linux: GTK3.
    // Call on the main thread. On Linux, initialization failure, Escape and window close return Cancel.
    enum class SaveChangesChoice { Save, DontSave, Cancel };
    static SaveChangesChoice showSaveChangesDialog(const char *title, const char *message);
    static void copyFolder(const path_t &sourcePath, const path_t &newPath);
    static void open(const path_t &path);
    static bool openWithDefaultApplication(const path_t &path);
    static bool openUrl(const std::string &url); // the default browser
    static void show(const path_t &path);
    static void openCppProject(const path_t &path);
    // Restart of the editor: the UI sets the flag and closes the application through the usual
    // flow; main() starts the new instance after the teardown, so the settings written on exit
    // are what the new instance reads.
    static void setRelaunchOnExit(bool relaunch);
    static bool relaunchOnExit();
    // Starts a new instance of this application (macOS: `open -n` on the bundle; Windows: the
    // executable of the process; Linux: /proc/self/exe). Returns false when it could not start.
    static bool relaunchApplication();
    // Starts a program without waiting for it or wiring its pipes (the update helper, the new
    // editor after an update).
    static bool startDetached(const path_t &executable, const std::vector<std::string> &args);
    static uint32_t currentProcessId();
    static std::optional<path_t> findCMakeExecutable();
    // slangc for compiling user shaders (MirenTools): editor bundle → PandaSDK.
    static std::optional<path_t> findSlangExecutable();
    // cancelRequested (optional): cooperative cancellation — checked on every output line; once it
    // is set, reading is aborted and the pipe is closed (SIGPIPE finishes the process off on its
    // next write).
    static bool runCommand(
        const path_t &executable,
        const std::vector<std::string> &args = {},
        const path_t &workingDirectory = {},
        CommandOutputCallback outputCallback = {},
        const std::atomic<bool> *cancelRequested = nullptr
    );
};

} // namespace Panda
