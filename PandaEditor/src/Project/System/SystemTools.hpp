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
    // `--user-data <dir>`: this instance keeps its settings, its session and its plugins there
    // instead (a test run, an agent's own editor: the layout of the user stays as it is). Set
    // before anything is read; empty — the folder of the system.
    static void setUserDataDirectory(path_t directory);
    static bool isUserDataDirectoryOverridden();
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
    // A file — in the application of its type; a folder — in the file manager.
    static bool openWithDefaultApplication(const path_t &path);
    static bool openUrl(const std::string &url); // the default browser
    // The file manager shows the folder of `path` (with `path` selected where it can select).
    static void revealInFileManager(const path_t &path);
    // The file in a text editor, at a line of it when one is given (from 1; the column too, 0 —
    // none). `editor` is what the user chose:
    //  * a command — found in PATH and the usual install folders, or the path of an executable, on
    //    macOS also the name of an application. The editors of the settings list are told the
    //    place the way each takes it (Zed and Sublime Text: `file:line:column`; Visual Studio
    //    Code and Cursor: `--goto file:line:column`; Xcode's xed: `--line`); an editor that is not
    //    known here gets the file alone.
    //  * a command line with `{file}` in it — for any other editor: its words are the command and
    //    its arguments, with `{file}`, `{line}` and `{column}` put in
    //    (`clion --line {line} {file}`). A word with blanks is written in double quotes.
    //  * empty, or not found — the application the system opens the file with (no place there).
    static void openInEditor(const std::string &editor, const path_t &path, int line = 0, int column = 0);
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
    // `--mcp <port>`: the MCP server of the editor (App/EditorMcp) listens on this port from the
    // start, whatever is open — the start screen included. For work on the editor itself; a
    // project turns its own server on in its settings.
    static constexpr const char *MCP_ARGUMENT = "--mcp";
    static constexpr const char *USER_DATA_ARGUMENT = "--user-data";
    // `--unattended`: nobody is at the screen for the whole run — a UI test, a script that drives
    // the editor through its MCP server. The mouse and the keyboard of the machine are not the
    // editor's input (a hand that happens to move does not reach the test), the window has one
    // size whatever the display and the last session, a native dialog is never shown (Unattended
    // below), the editor does not look for updates.
    static constexpr const char *UNATTENDED_ARGUMENT = "--unattended";
    // Starts a program without waiting for it or wiring its pipes (the update helper, the new
    // editor after an update).
    static bool startDetached(const path_t &executable, const std::vector<std::string> &args);
    static uint32_t currentProcessId();
    // An executable by its command: the folders of PATH, then the usual install folders (a GUI
    // application on macOS does not inherit the PATH of the user's shell). A path of a file is
    // answered as it is.
    static std::optional<path_t> findExecutable(const std::string &command);
    // The cmake that comes with the editor, then the SDK's, then PATH.
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
