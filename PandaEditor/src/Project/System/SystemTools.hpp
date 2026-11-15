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
    // Nobody is at the screen: an agent drives the editor (a tool of the MCP server runs,
    // App/EditorMcp). While the scope lives a native blocking dialog is not shown: the
    // save-changes question answers as the scope says, a file dialog answers "cancelled", and
    // the scope remembers what was asked — the tool says it in its answer.
    class Unattended final {
    public:
        explicit Unattended(SaveChangesChoice answer = SaveChangesChoice::Cancel);
        ~Unattended();
        Unattended(const Unattended &) = delete;
        Unattended &operator=(const Unattended &) = delete;
        // What the editor wanted to ask the user, in order.
        const std::vector<std::string> &questions() const {
            return m_questions;
        }

    private:
        friend class SystemTools;
        SaveChangesChoice m_answer;
        std::vector<std::string> m_questions;
        Unattended *m_outer; // the scopes nest
    };
    static void copyFolder(const path_t &sourcePath, const path_t &newPath);
    static void open(const path_t &path);
    static bool openWithDefaultApplication(const path_t &path);
    static bool openUrl(const std::string &url); // the default browser
    static void show(const path_t &path);
    // The file in a text editor: `command` is what the user chose (a command found in PATH and the
    // usual install folders, the path of an executable, on macOS also an application name);
    // empty or not found — the application the system opens the file with.
    static void openInEditor(const std::string &command, const path_t &path);
    static void openCppProject(const path_t &path);
    // Restart of the editor: the UI sets the flag and closes the application through the usual
    // flow; main() starts the new instance after the teardown, so the settings written on exit
    // are what the new instance reads.
    // `arguments` — the command line of the new instance (the project to open again).
    static void setRelaunchOnExit(bool relaunch, std::vector<std::string> arguments = {});
    static bool relaunchOnExit();
    static const std::vector<std::string> &relaunchArguments();
    // Starts a new instance of this application (macOS: `open -n` on the bundle; Windows: the
    // executable of the process; Linux: /proc/self/exe) with the given arguments. Returns false
    // when it could not start. A restart is a new instance started as this one exits; a second
    // window is a new instance next to this one (NEW_WINDOW_ARGUMENT) — one project per process.
    static bool startNewInstance(const std::vector<std::string> &args = {});
    // The argument of an instance started as a second window: it shows the start screen instead
    // of reopening the project of the last session.
    static constexpr const char *NEW_WINDOW_ARGUMENT = "--new-window";
    // `--project <path>`: the instance opens this project instead of the one of the last session
    // (a restart of the editor, an agent or a script that starts the editor for a project).
    static constexpr const char *PROJECT_ARGUMENT = "--project";
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

private:
    // The start of every native blocking dialog. true — nobody is there to answer (Unattended):
    // the question is recorded, `choice` (optional) gets the answer of the scope, no dialog.
    static bool answerUnattended(const char *question, SaveChangesChoice *choice = nullptr);
};

} // namespace Panda
