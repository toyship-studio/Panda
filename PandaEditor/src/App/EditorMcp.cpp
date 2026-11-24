#include "App/EditorMcp.hpp"

#include "App/EditorLayer.hpp"
#include "App/SystemStats.hpp"
#include "Project/Export/GameExporter.hpp"
#include <Panda/Runtime/CheckRun.hpp>
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/EditorController.hpp"

#include <Fern/Window/Window.hpp>
#include <Foundation/GraphicsAPI.hpp>
#include <Foundation/Logger.hpp>
#include <Miren/Miren.hpp>
#include <Panda/Application/Application.hpp>
#include <Fern/Events/KeyEvents.hpp>
#include <Fern/Events/MouseEvents.hpp>
#include <Fern/Events/TouchEvents.hpp>
#include <Panda/GameLogic/Input.hpp>
#include <Panda/Base/Version.hpp>
#include <Panda/Renderer/ShaderFeatures.hpp>
#include <PandaUI/Elements/ScrollView.hpp>
#include <PandaUI/Runtime/AccessibilityTree.hpp>
#include <miniz.h>
#include <Rain/Value.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace Panda {

namespace {

    using Foundation::LogHistory;
    using Foundation::LogRecord;
    using LogLevel = Foundation::Logger::MessageType;
    using SaveChangesChoice = SystemTools::SaveChangesChoice;

    const char *INSTRUCTIONS =
        "The Panda editor with a game project open in it. The files of the project are the contract: edit worlds "
        "(.world), materials (.mat), shaders (.hlsl), .Panda/project.json and the C++ scripts as text with your own "
        "tools, then call `reload` so that the editor takes them, `screenshot` to look at the result and `logs` to "
        "read what the editor said (shader and import errors are there). `play` runs the game inside the editor, "
        "`stop` returns to editing. `status` says what is open. The editor keeps running between the calls; "
        "`restart` starts it again with the same project when the editor itself was rebuilt or the module set of "
        "project.json has changed.";

    constexpr int SCREENSHOT_DEFAULT_WIDTH = 1280;
    // Frames the wanted viewport has rendered before it is read: its tab may have come forward a
    // frame ago, and the first frames of a surface are drawn at its old size.
    constexpr int SCREENSHOT_LIVE_FRAMES = 3;
    constexpr int SCREENSHOT_TIMEOUT_FRAMES = 180;
    constexpr size_t CALL_LOG_LINES = 60; // of what a call logged, in its answer

    double nowSeconds() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    const char *levelName(LogLevel level) {
        switch (level) {
            case LogLevel::_TRACE: return "trace";
            case LogLevel::_INFO: return "info ";
            case LogLevel::_WARNING: return "WARN ";
            case LogLevel::_ERROR: return "ERROR";
            case LogLevel::_CRITICAL: return "FATAL";
        }
        return "info ";
    }

    void appendLines(std::string &text, const std::vector<LogRecord> &lines) {
        for (const LogRecord &line : lines) {
            text += "[";
            text += line.time;
            text += "] ";
            text += levelName(line.type);
            text += " ";
            text += line.text;
            text += "\n";
        }
    }

    // What the editor logged since `from`: the tail of the answer of a tool that changed something.
    std::string logSince(uint64_t from) {
        size_t total = 0;
        const std::vector<LogRecord> lines = LogHistory::read(from, LogLevel::_INFO, CALL_LOG_LINES, &total);
        std::string text;
        if (total > lines.size()) {
            text += "(the last " + std::to_string(lines.size()) + " of " + std::to_string(total) + " log lines; `logs` has all)\n";
        }
        appendLines(text, lines);
        return text;
    }

    // A number an agent reads: the decimals beyond the third go (cut, not rounded).
    double cut(double value) {
        return std::abs(value) < 1e15 ? std::trunc(value * 1000.0) / 1000.0 : value;
    }

    Rain::Value vectorValue(glm::vec3 value) {
        Rain::Value array = Rain::Value::array();
        for (float component : {value.x, value.y, value.z}) { array.push(cut(component)); }
        return array;
    }

    Mcp::ToolResult textResult(std::string text) {
        Mcp::ToolResult result;
        result.text = std::move(text);
        return result;
    }

    const char *viewName(ViewportKind view) {
        return view == ViewportKind::Scene ? "world" : "game"; // as the switch of the toolbar names them
    }

} // namespace

// A screenshot in flight: the tab of the viewport comes forward, the viewport renders a few
// frames, its output texture is read back from the GPU (two more frames), then the picture is
// scaled and packed.
struct EditorMcp::Screenshot {
    bool window = false; // the whole window of the editor, not a viewport
    bool settings = false; // window: the Settings window, not the main one
    ViewportKind view = ViewportKind::Scene;
    ViewportKind shownBefore = ViewportKind::Scene; // the viewport that rendered when the shot was asked for
    bool switched = false;                          // another tab was brought forward for the shot: it goes back
    int maxWidth = SCREENSHOT_DEFAULT_WIDTH;
    bool returnImage = true;
    path_t file;             // also written here; empty — not written
    double notBefore = 0.0;  // the `wait` of the call
    int frames = 0;
    int liveFrames = 0;
    bool reading = false;
    uint32_t readyFrame = 0; // Miren::getFrameNumber() at which the pixels are there
    uint32_t width = 0;
    uint32_t height = 0;
    // RGBA of the output texture; the render command of the readback holds it too, so it outlives
    // a connection that went away.
    std::shared_ptr<std::vector<uint8_t>> pixels;
};

// One frame of input made in place of the user.
struct EditorMcp::InputStep {
    enum class Kind { Move, Down, Up, Scroll, KeyDown, KeyUp, Text, Drop, Idle };
    Kind kind = Kind::Idle;
    PandaUI::Point position;                                    // Move, Down, Up, Scroll (window points)
    PandaUI::Point delta;                                       // Scroll
    bool trackpad = false;                                      // Scroll: a two-finger swipe, not a wheel
    float magnification = 0.f;                                  // Scroll: a pinch of the trackpad
    PandaUI::PointerButton button = PandaUI::PointerButton::Left; // Down, Up
    PandaUI::Key key = PandaUI::Key::Unknown;                   // KeyDown, KeyUp
    uint32_t codepoint = 0;                                     // Text
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;
    bool newClick = false; // Down: not the second click of a double click
    std::vector<std::string> files; // Drop: the paths dropped from the file manager
};

// A gesture of the pointer as a call of `pointer` describes it.
struct EditorMcp::Gesture {
    std::string action = "click";
    PandaUI::Point at; // window points
    PandaUI::Point to; // drag: where it ends
    PandaUI::PointerButton button = PandaUI::PointerButton::Left; // drag, down, up
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;
    int steps = 0;              // drag: the frames of the movement; scroll: the events of the swipe; 0 — the default
    PandaUI::Point scrollDelta; // scroll
    bool trackpad = false;      // scroll
    float magnification = 0.f;  // pinch
    std::vector<std::string> files; // drop
};

// A place a gesture goes to, named by the path of an element (PandaUI/Runtime/AccessibilityTree.hpp).
// The element is looked for anew every frame until it stands still where a press reaches it: the
// form it is in may be on its way — rebuilt by the last call, scrolled, opening.
struct EditorMcp::Target {
    std::string path;
    PandaUI::Point at = PandaUI::Point(0.5f, 0.5f); // where in the element, in shares of its size
    bool takesPress = true; // the press of the gesture lands here: nothing may lie over it
    PandaUI::Rect seen;     // the frame of the element a frame ago
    bool wasSeen = false;
    PandaUI::Point point; // the answer, window points
};

EditorMcp::EditorMcp(EditorLayer &editor)
    : m_editor(editor)
    , m_server("panda-editor", "Panda Editor", ENGINE_VERSION, INSTRUCTIONS) {
    registerTools();
}

EditorMcp::~EditorMcp() {
    if (m_windowCapture.isValid()) {
        Miren::deleteFrameBuffer(m_windowCaptureBuffer);
        Miren::deleteTexture(m_windowCapture);
    }
}

void EditorMcp::update() {
    syncServer();
    m_server.update();
    performExit();
}

void EditorMcp::syncServer() {
    int port = 0;
    if (m_editor.launch().mcpPort > 0 && m_editor.launch().mcpPort < 65536) {
        // Asked for on the command line: the server is there whatever is open (the start screen
        // too) — work on the editor itself.
        port = m_editor.launch().mcpPort;
    } else if (hasProject()) {
        const ProjectMcpSettings &settings = m_editor.documents().projectSettings().mcp;
        if (settings.enabled && settings.port > 0 && settings.port < 65536) { port = settings.port; }
    }
    // Decided once per change: a port that could not be taken is not tried again every frame.
    if (port == m_port) { return; }
    // The answer on its way leaves first (Reload rereads project.json inside a call).
    if (m_server.isRunning() && m_server.hasUnsentData()) { return; }
    m_port = port;
    m_server.stop();
    if (port == 0) { return; }
    if (m_server.start(static_cast<uint16_t>(port))) {
        LOG_INFO_EDITOR("MCP server: http://127.0.0.1:%d/mcp", port);
    } else {
        LOG_ERROR_EDITOR(
            "MCP server: port %d is taken (another editor with the same port?). Change \"mcp\".\"port\" in "
            ".Panda/project.json and the url in .mcp.json of the project.",
            port
        );
    }
}

void EditorMcp::performExit() {
    if (m_exit == Exit::None || m_server.hasUnsentData()) { return; }
    // The answer has left the process; a couple of frames more and the client has it.
    if (++m_exitFrames < 3) { return; }
    const Exit kind = m_exit;
    m_exit = Exit::None;
    // The unsaved documents were asked about by the tool (`discard`): the question is answered.
    SystemTools::Unattended unattended(SaveChangesChoice::DontSave);
    if (kind == Exit::Restart) {
        m_editor.restartEditor();
    } else if (kind == Exit::OpenProject) {
        m_editor.openProject(m_projectToOpen);
    } else {
        m_editor.closeAppRequest();
    }
}

bool EditorMcp::hasProject() const {
    return m_editor.project().hasOpenedProject();
}

std::vector<std::string> EditorMcp::dirtyDocuments() const {
    std::vector<std::string> titles;
    const FileDocumentList files = m_editor.documents().list();
    for (FileDocument *file : files.all) {
        if (file->isDirty()) { titles.push_back(file->title()); }
    }
    return titles;
}

Rain::Value EditorMcp::cameraValue() const {
    CameraController &camera = m_editor.viewports().cameraController();
    Rain::Value answer = Rain::Value::object();
    answer.add("position", vectorValue(camera.getPosition()));
    answer.add("forward", vectorValue(camera.getForward()));
    return answer;
}

void EditorMcp::registerTools() {
    m_server.addTool({
        "status",
        "The state of the editor: the project, Edit or Play, the open worlds, the unsaved documents, the viewports, "
        "the editor camera, the frame statistics, the disk (whether the editor is in front and takes what changes "
        "on disk by itself, whether a change waits to be taken), the shader features of the project, the number of "
        "errors in the log. Call it first.",
        {},
        [this](const Rain::Value &) { return status(); },
    });
    m_server.addTool({
        "logs",
        "The log of the editor and the engine, the oldest line first: what the Output panel shows the user "
        "(shader, import and script errors) and the engine console. The first line of the answer is `next=<n>`: "
        "pass it as `since` in the next call to get only the new lines.",
        R"({"type":"object","properties":{)"
        R"("since":{"type":"integer","description":"Only the lines from this sequence number on (`next` of an earlier answer). Without it: the last lines."},)"
        R"("level":{"type":"string","enum":["trace","info","warning","error"],"description":"The lowest level to return. Default: info."},)"
        R"("limit":{"type":"integer","description":"At most this many lines, the newest ones. Default 100, at most 1000."}}})",
        [this](const Rain::Value &arguments) { return logs(arguments); },
    });
    m_server.addTool({
        "screenshot",
        "A picture of a viewport as the editor shows it, or of the whole editor window. `world`: the world "
        "through the editor camera, with the grid, the selection outline and the helper icons (place the camera "
        "with `camera`). `game`: the frame of the game through the main camera of the world, with the game UI — "
        "the running game in Play, a still frame in Edit. The world tab shows that view for the shot (the switch "
        "World | Game of the toolbar). "
        "`window`: the editor itself — its panels, tabs and dialogs with the viewport inside (the menu bar and the "
        "title of the system are not part of it). Takes a few frames.",
        R"({"type":"object","properties":{)"
        R"("view":{"type":"string","enum":["world","game","window","settings","export"],"description":"Default: game while playing, world while editing. settings, export: the second window of the editor (Settings, Export Game) while it is open."},)"
        R"("width":{"type":"integer","description":"The picture is scaled down to this width in pixels when the viewport is wider. Default 1280."},)"
        R"("wait":{"type":"number","description":"Seconds to wait before the shot: assets still loading, a moment of the game to reach. Default 0, at most 30."},)"
        R"("file":{"type":"string","description":"Also write the PNG to this file: an absolute path, or one relative to the project folder."},)"
        R"("image":{"type":"boolean","description":"Return the picture in the answer. Default true; false when the file is enough."}}})",
        [this](const Rain::Value &arguments) { return screenshot(arguments); },
    });
    m_server.addTool({
        "reload",
        "Makes the editor take what changed on disk — worlds, assets, shaders, the settings, the script build — "
        "now, whether it is the application in front or not. Call it after editing files of the project. Stops the game if it plays. A "
        "document with unsaved edits in the editor that was also changed on disk is a conflict: the tool fails "
        "and names it. Answers with what the editor logged.",
        R"({"type":"object","properties":{)"
        R"("discard":{"type":"boolean","description":"The disk wins: every unsaved edit made in the editor is dropped and its document is read again from its file, changed on disk or not. Default false."}}})",
        [this](const Rain::Value &arguments) { return reload(arguments); },
    });
    m_server.addTool({
        "focus",
        "Tells the editor that it is the application in front, or behind. In front, the editor takes by itself "
        "what changed on disk — the way `reload` does, but without stopping at a conflict: a document with unsaved "
        "edits is left as it is — within a moment of the change; behind, the changes wait until it comes back. An "
        "editor nobody is at (--unattended) is behind until this tool says otherwise, so a test decides when the "
        "user \"comes back\"; in an editor a person works with, the real window decides and this tool holds only "
        "until the window says otherwise. While the game plays the changes wait for Stop. Answers after `wait` "
        "seconds with what the editor logged meanwhile.",
        R"({"type":"object","properties":{)"
        R"("front":{"type":"boolean","description":"true (the default) — the editor is in front; false — it is behind."},)"
        R"("wait":{"type":"number","description":"Seconds before the answer: the time the editor has to take what waits. Default 1, at most 30."}}})",
        [this](const Rain::Value &arguments) { return focus(arguments); },
    });
    m_server.addTool({
        "save",
        "Saves every unsaved document of the editor (Save All): what the user changed in the editor reaches the "
        "files. A file changed on disk since the editor read it is a conflict: the tool fails and names it.",
        R"({"type":"object","properties":{)"
        R"("overwrite":{"type":"boolean","description":"In a conflict the version of the editor wins and the file on disk is overwritten. Default false."}}})",
        [this](const Rain::Value &arguments) { return save(arguments); },
    });
    m_server.addTool({
        "play",
        "Starts the game inside the editor (Play): the scripts and the physics run in a copy of the active world, "
        "the world tab shows the frame of the Game. When the sources of the scripts changed since their build, they are built "
        "first and the answer waits for it. Answers with what the start logged.",
        {},
        [this](const Rain::Value &) { return play(); },
    });
    m_server.addTool({
        "stop",
        "Stops the game (Stop): the editor returns to the world being edited; what the game changed is gone.",
        {},
        [this](const Rain::Value &) { return stop(); },
    });
    m_server.addTool({
        "camera",
        "The editor camera of the World view. Without arguments answers where it is; `position` moves it, "
        "`target` turns it to look at a point. World space, meters, Y up. The camera of the game is an entity "
        "of the world: it is edited in the world file.",
        R"({"type":"object","properties":{)"
        R"("position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"[x, y, z] of the camera."},)"
        R"("target":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"[x, y, z] of the point to look at."}}})",
        [this](const Rain::Value &arguments) { return camera(arguments); },
    });
    m_server.addTool({
        "open_world",
        "Opens a world file of the project — or a prefab file — in a tab of the editor and makes it the document "
        "in front: the one the viewports and the Hierarchy show. A world is what Play runs; a prefab is edited "
        "the same way and never played (`status` marks it `prefab`). Stops the game if it plays.",
        R"({"type":"object","properties":{)"
        R"("path":{"type":"string","description":"The .world or .prefab file: relative to the project folder (Assets/world.world, Assets/Prefabs/Lamp.prefab) or absolute."}},)"
        R"("required":["path"]})",
        [this](const Rain::Value &arguments) { return openWorld(arguments); },
    });
    m_server.addTool({
        "open_project",
        "Opens another project in this editor window, as File → Open Project does: the open project is saved and "
        "closed, the other one takes its place. The answer comes first, then the editor switches; its server "
        "follows the `mcp` section of the project it opens — the same address when the editor was started with "
        "`--mcp <port>`, otherwise the port of that project (poll `status`). Fails when documents are unsaved.",
        R"({"type":"object","properties":{)"
        R"("path":{"type":"string","description":"The folder of the project (the one that holds .Panda/project.json), absolute."},)"
        R"("discard":{"type":"boolean","description":"Switch although documents are unsaved: their changes are lost. Default false."}},)"
        R"("required":["path"]})",
        [this](const Rain::Value &arguments) { return openProject(arguments); },
    });
    m_server.addTool({
        "show",
        "Brings a part of the editor in front, as a click of the user would, so that a `screenshot` of the "
        "`window` shows it. One of: `panel` — a panel by its id (hierarchy and inspector are the columns of the "
        "world tab); `settings` — the settings; `asset` — opens an asset as a double click does: a world or a "
        "prefab in its world tab, an asset with a form in its tab (a material with its preview, a texture, a "
        "mesh); `entity` — selects an entity of the active world by its name: the inspector of the world tab "
        "shows it.",
        R"({"type":"object","properties":{)"
        R"("panel":{"type":"string","enum":["hierarchy","inspector","contentBrowser","output","cmake","statistics","profiler"]},)"
        R"("settings":{"type":"string","enum":["project","editor","keymap","close"],"description":"project, editor, keymap: a page of the Settings window (a window of its own). close: closes the second window of the editor — Settings or Export Game (the command export.open opens that one)."},)"
        R"("asset":{"type":"string","description":"The file of the asset, relative to the project folder (Assets/Materials/gold.mat)."},)"
        R"("entity":{"type":"string","description":"The name of the entity."}}})",
        [this](const Rain::Value &arguments) { return show(arguments); },
    });
    m_server.addTool({
        "ui",
        "The window of the editor as elements instead of pixels: what every control is (`role`: button, textField, "
        "numberField, toggle, property, section, tab, row, cell, menuItem, dialog, …), the `id` it was given, the "
        "`label` it shows, the `value` it holds (the text of a field, on / off, expanded / collapsed, the number of "
        "rows of a tree) and its `frame` [x, y, width, height] in the coordinates of a `window` screenshot; "
        "`disabled`, `selected`, `focused`, `outOfSight` when so. For the editor itself as the thing to test: find "
        "a control and read what it shows without looking at a picture. Without `path`: everything, the lowest "
        "layer first (the window, then the popups, menus and dialogs over it). `path` names elements: segments "
        "separated by `/`, each one an element inside the one before it at any depth, the first one anywhere — "
        "`inspector/Transform/Position/X`, `hierarchy/Camera`, `menu/Duplicate`. A segment is an id; where "
        "nothing has such an id — a label; else a role. `role:name` is an element of that role with that id or "
        "label (`tab:Player` where a row is named Player too); `name[2]` is the third of what the segment names. The "
        "call waits up to `wait` seconds for what `path` names to be there — or, with them, for `gone`, `count`, "
        "`value`, `label` to hold — and fails saying what is there instead. A row of a long list that is out of "
        "sight is scrolled to. `pointer` and `key` take the same path as `target`.",
        R"({"type":"object","properties":{)"
        R"("path":{"type":"string","description":"The elements to answer with (and what is in them). Without it: the whole window."},)"
        R"("depth":{"type":"integer","description":"How many levels inside each answered element to write; `more` then says how many elements were left out. Default: all."},)"
        R"("wait":{"type":"number","description":"Seconds to wait for the path (and the expectation). Default 2, at most 30; 0 — answer at once."},)"
        R"("gone":{"type":"boolean","description":"Wait until the path names nothing."},)"
        R"("count":{"type":"integer","description":"Wait until the path names exactly this many elements."},)"
        R"("value":{"type":"string","description":"Wait until the one element the path names has this value."},)"
        R"("label":{"type":"string","description":"Wait until the one element the path names has this label."},)"
        R"("window":{"type":"string","enum":["main","settings","export"],"description":"Default: the window in front — the second window of the editor (Settings, Export Game) while one is open."},)"
        R"("of":{"type":"number","description":"The width of the screenshot the frames are for. Default 1280."}}})",
        [this](const Rain::Value &arguments) { return ui(arguments); },
    });
    m_server.addTool({
        "pointer",
        "A gesture of the mouse in the editor window, made where the user would make it: for the editor itself as "
        "the thing to test — a button, a menu, a field, a drag, the gizmo and the camera of the World view. The place is "
        "an element by its path (`target`, see `ui`: the gesture goes to its middle, or to `at` in it; the call "
        "waits for the element to be there, scrolls it into sight, and fails when it is disabled or something "
        "lies over it) or a point of a `window` screenshot (`x`, `y`; of the default width — pass `of` when the "
        "screenshot was asked with another `width`). `click` presses and releases; `drag` presses, moves to "
        "`toTarget` or toX, toY over a few frames and releases; `down`, `move`, `up` make a gesture in parts, with "
        "a look in between; `scroll` turns the wheel by dx, dy (`trackpad` — a two-finger swipe instead); `pinch` "
        "is the pinch of a trackpad; `drop` drops `files` of the file manager there (a folder of the Content "
        "Browser takes them; elsewhere they go into the folder it shows). Answers with what is under the pointer afterwards: the elements from the "
        "innermost out, the cursor, the element in focus. While the Settings window is open the gesture goes to "
        "it (it is in front): the coordinates are those of a `settings` screenshot.",
        R"({"type":"object","properties":{)"
        R"("action":{"type":"string","enum":["click","doubleClick","rightClick","move","down","up","drag","scroll","pinch","drop"],"description":"Default: click."},)"
        R"("files":{"type":"array","items":{"type":"string"},"description":"drop: the paths of files dropped onto the place from the file manager, as the Finder or the Explorer drops them."},)"
        R"("target":{"type":"string","description":"The path of the element the gesture goes to (see `ui`), in place of x, y."},)"
        R"("at":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2,"description":"Where in the target, in shares of its width and height. Default [0.5, 0.5] — the middle."},)"
        R"("toTarget":{"type":"string","description":"drag: the path of the element it ends on, in place of toX, toY."},)"
        R"("toAt":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2,"description":"drag: where in toTarget. Default the middle."},)"
        R"("wait":{"type":"number","description":"Seconds to wait for the targets. Default 2, at most 30."},)"
        R"("x":{"type":"number"},"y":{"type":"number"},)"
        R"("toX":{"type":"number","description":"drag: where it ends."},"toY":{"type":"number"},)"
        R"("button":{"type":"string","enum":["left","right","middle"],"description":"drag, down, up: the button. Default left."},)"
        R"("dx":{"type":"number","description":"scroll: the wheel, in lines; dy > 0 scrolls the content down."},"dy":{"type":"number"},)"
        R"("trackpad":{"type":"boolean","description":"scroll: a two-finger swipe of the trackpad instead of a wheel (a viewport and a preview orbit by it)."},)"
        R"("scale":{"type":"number","description":"pinch: the share the fingers move apart by, 0.2 — 20% larger, negative — smaller."},)"
        R"("steps":{"type":"integer","description":"drag: the frames of the movement, default 8. scroll: the events of the swipe, each by dx, dy; default 1."},)"
        R"("of":{"type":"number","description":"The width of the screenshot the coordinates were read from. Default 1280."},)"
        R"("shift":{"type":"boolean"},"alt":{"type":"boolean"},"ctrl":{"type":"boolean"},"super":{"type":"boolean","description":"Command on macOS."}}})",
        [this](const Rain::Value &arguments) { return pointer(arguments); },
    });
    m_server.addTool({
        "key",
        "Keys and text into the editor window, to the element in focus: `text` is typed character by character, "
        "`key` is pressed and released — a letter or a digit, or Enter, Escape, Tab, Backspace, Delete, Space, Up, "
        "Down, Left, Right, Home, End, PageUp, PageDown, F1..F12. With both, the text goes first. `target` (the "
        "path of an element, see `ui`) is clicked first, so that it has the keyboard; without it click a field "
        "with `pointer`. Answers like `pointer`. While the Settings window is open the keys go to it.",
        R"({"type":"object","properties":{)"
        R"("key":{"type":"string"},"text":{"type":"string"},)"
        R"("target":{"type":"string","description":"The path of the element to click before the keys."},)"
        R"("wait":{"type":"number","description":"Seconds to wait for the target. Default 2, at most 30."},)"
        R"("shift":{"type":"boolean"},"alt":{"type":"boolean"},"ctrl":{"type":"boolean"},"super":{"type":"boolean","description":"Command on macOS."}}})",
        [this](const Rain::Value &arguments) { return key(arguments); },
    });
    m_server.addTool({
        "game_input",
        "Plays the game while it runs in Play: keys, the mouse and fingers go into the input of the game "
        "(Bamboo::Input), not into the editor — what `pointer` and `key` are for the editor, this is for the game. "
        "One call is one gesture over `seconds` (default 0.15): `keys` are held for the time (W, A, S, D, Space, "
        "Shift, Control, Alt, Escape, Tab, Enter, Up, Down, Left, Right, a letter, a digit); `mouse` [x, y] puts "
        "the pointer there and `to` [x, y] moves it there over the time (a drag when a `button` is held: left, "
        "right, middle — pressed at the start, released at the end); `scroll` turns the wheel by that many "
        "notches at the start; `touches` are fingers, each {\"at\": [x, y], \"to\": [x, y]} — down at the start, "
        "moving, up at the end (two at once: a stick and a look). Points are those of the Game view (the space "
        "of ApplicationAPI::getWidth / getHeight; a `game` screenshot of the default width is in the same points "
        "when the view is 1280 wide — pass `of` with the width of the screenshot the points were read from). "
        "From the first gesture until Stop the game has the input whatever has the focus (the editor of a test "
        "runs in the background), and a cursor the game locks is one of its own: the real one stays free, and "
        "the moves of `mouse` turn the view of a game that looks around with a locked cursor. Answers when it "
        "is over.",
        R"({"type":"object","properties":{)"
        R"("keys":{"type":"array","items":{"type":"string"}},)"
        R"("seconds":{"type":"number","description":"How long the gesture lasts. Default 0.15, at most 30."},)"
        R"("mouse":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},)"
        R"("to":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2,"description":"Where the mouse ends."},)"
        R"("button":{"type":"string","enum":["left","right","middle"]},)"
        R"("scroll":{"type":"number","description":"Notches of the wheel, positive — away from the user."},)"
        R"("touches":{"type":"array","items":{"type":"object","properties":{"at":{"type":"array","items":{"type":"number"}},"to":{"type":"array","items":{"type":"number"}}}}},)"
        R"("of":{"type":"number","description":"The width of the screenshot the points were read from; default: points of the view."}}})",
        [this](const Rain::Value &arguments) { return gameInput(arguments); },
    });
    m_server.addTool({
        "command",
        "The commands of the editor (what the menus, the command palette and the keys run). Without arguments: "
        "the list — id, group, name, the keys, where the key works. With `id`: runs the command the way the "
        "palette does, whatever has the keyboard (`file.save`, `world.play`, `entity.duplicate`, `keymap.open`, "
        "…) — prefer it to pressing keys. With `id` and `keys` (chords as keymap.json writes them: \"cmd-shift-p\", "
        "\"f5\"; an empty array unbinds) or `reset`: changes the keymap instead of running.",
        R"({"type":"object","properties":{)"
        R"("id":{"type":"string"},)"
        R"("keys":{"type":"array","items":{"type":"string"},"description":"Bind these chords to the command."},)"
        R"("reset":{"type":"boolean","description":"Return the command to its default keys."}}})",
        [this](const Rain::Value &arguments) { return command(arguments); },
    });
    m_server.addTool({
        "build",
        "Builds the C++ of the project the way the editor does and answers when the build has ended: how it "
        "ended and what cmake said (the compiler errors are there). `target`: `scripts` (the default) — the C++ "
        "scripts of the project, in the configuration the CMake panel shows; `export` — the game as an Xcode or "
        "Visual Studio project, from the data cooked first, for the target and the configuration the Export "
        "window shows. `action`: `build` (the default; configures a build folder that is "
        "not configured), `configure` (cmake again: after a source file was added), `clean` (deletes the build "
        "folder). The editor loads scripts that were built — at once, or when the game stops if it plays.",
        R"({"type":"object","properties":{)"
        R"("target":{"type":"string","enum":["scripts","export"]},)"
        R"("action":{"type":"string","enum":["build","configure","clean"]},)"
        R"("lines":{"type":"number","description":"How many of the last lines of the output to answer with. Default 80."}}})",
        [this](const Rain::Value &arguments) { return build(arguments); },
    });
    m_server.addTool({
        "export",
        "The data of the game as it will ship. `action`: `cook` (the default) — cooks it from the files of the "
        "project (an unsaved edit is not in it: `save` first): the start world of the project settings "
        "(startWorld of project.json) and everything it reaches — its assets, the worlds its scripts "
        "are given, what those name — in one pack (.Panda/Cooked/Runtime/Assets.pak); answers with what the pack "
        "holds by kind, its largest files, the warnings, and the assets of the project the game does not reach — "
        "those are left out (an asset the game needs is named by a world of the game: a field of a component "
        "or of a script). `report` — the same account of the "
        "last cook, without cooking. `files` — every file of the pack with its size (`filter`: a part of the "
        "path). The whole report is .Panda/Cooked/report.json. `build` with target `export` then builds the game, "
        "and `run` plays the built game by itself (a check run: `frames` frames, then it leaves) and answers with "
        "what it logged, how it ended and the picture of its last frame — the game as the player will see it, "
        "from the pack. The order of a full check: cook, build (target export), run.",
        R"({"type":"object","properties":{)"
        R"("action":{"type":"string","enum":["cook","report","files","run"]},)"
        R"("filter":{"type":"string","description":"files: only the paths that contain this text."},)"
        R"("limit":{"type":"number","description":"How many of the largest files and of the unused assets to list; run: how many of the last lines of the log. Default 15; run: 40."},)"
        R"("frames":{"type":"number","description":"run: how many frames the game plays before it leaves. Default 120."},)"
        R"("width":{"type":"number","description":"run: the picture is scaled down to this width. Default 1280."}}})",
        [this](const Rain::Value &arguments) { return exportGame(arguments); },
    });
    m_server.addTool({
        "restart",
        "Restarts the editor with the same project: after the editor itself was rebuilt, or when the module set "
        "of project.json has changed. The answer comes first, then the editor exits and starts again; the server "
        "answers again in a few seconds (poll `status`). Fails when documents are unsaved.",
        R"({"type":"object","properties":{)"
        R"("discard":{"type":"boolean","description":"Restart although documents are unsaved: their changes are lost. Default false."}}})",
        [this](const Rain::Value &arguments) { return exit(Exit::Restart, arguments); },
    });
    m_server.addTool({
        "quit",
        "Closes the editor. Fails when documents are unsaved.",
        R"({"type":"object","properties":{)"
        R"("discard":{"type":"boolean","description":"Close although documents are unsaved: their changes are lost. Default false."}}})",
        [this](const Rain::Value &arguments) { return exit(Exit::Quit, arguments); },
    });
}

/* ------------------------------ status, logs ------------------------------ */

Mcp::PendingResult EditorMcp::status() {
    Rain::Value editor = Rain::Value::object();
    editor.add("version", ENGINE_VERSION);
    if (ENGINE_BUILD_COMMIT[0] != '\0') { editor.add("commit", ENGINE_BUILD_COMMIT); }
    editor.add("pid", SystemTools::currentProcessId());
    Rain::Value answer = Rain::Value::object();
    answer.add("editor", std::move(editor));
    if (!hasProject()) {
        answer.add("project", nullptr);
        return Mcp::ready(textResult(Rain::writeJson(answer)));
    }
    const path_t &projectPath = m_editor.project().getOpenedProjectPath();
    Rain::Value project = Rain::Value::object();
    project.add("name", m_editor.project().projectName());
    project.add("path", projectPath.string());
    answer.add("project", std::move(project));
    answer.add("state", m_editor.isPlaying() ? "play" : "edit");

    Rain::Value worlds = Rain::Value::array();
    for (const std::unique_ptr<WorldFile> &world : m_editor.documents().worlds()) {
        Rain::Value item = Rain::Value::object();
        // Relative to the project; empty — a new world that has no file yet.
        std::error_code error;
        const path_t relative = world->path().empty() ? path_t() : std::filesystem::relative(world->path(), projectPath, error);
        item.add("file", error ? world->path().string() : relative.generic_string());
        item.add("active", world.get() == m_editor.documents().activeWorld());
        if (world->isPrefab()) { item.add("prefab", true); } // a prefab open in a tab: edited like a world, never played
        item.add("unsaved", world->isDirty());
        item.add("changedOnDisk", world->isChangedOnDisk());
        worlds.push(std::move(item));
    }
    answer.add("worlds", std::move(worlds));

    Rain::Value unsaved = Rain::Value::array(); // every document with edits the files do not have: `save` writes them
    for (const std::string &title : dirtyDocuments()) { unsaved.push(title); }
    answer.add("unsaved", std::move(unsaved));

    const EditorViewports &viewports = m_editor.viewports();
    Rain::Value views = Rain::Value::object();
    views.add("rendering", viewName(viewports.live())); // one viewport renders per frame: the one whose tab is in front
    for (ViewportKind view : {ViewportKind::Scene, ViewportKind::Game}) {
        const ViewportSurface &surface = viewports.surface(view);
        Rain::Value size = Rain::Value::object();
        size.add("width", surface.pixelWidth());
        size.add("height", surface.pixelHeight());
        views.add(viewName(view), std::move(size));
    }
    answer.add("viewports", std::move(views));
    answer.add("camera", cameraValue());

    const Renderer3D::Statistics stats3d = m_editor.viewports().statistics3D();
    const Renderer2D::Statistics stats2d = m_editor.viewports().statistics2D();
    const Miren::GpuStats gpu = Miren::getGpuStats();
    const Editor::SystemStats system = Editor::sampleSystemStats();
    const double megabyte = 1024.0 * 1024.0;
    Rain::Value frame = Rain::Value::object();
    frame.add("fps", Application::get()->fps);
    frame.add("gpuMs", cut(gpu.frameMs));
    frame.add("drawCalls", stats3d.drawCalls);
    frame.add("shadowDrawCalls", stats3d.shadowDrawCalls);
    frame.add("drawCalls2d", stats2d.drawCalls);
    frame.add("memoryMb", std::round(static_cast<double>(system.residentBytes) / megabyte));
    frame.add("videoMemoryMb", std::round(static_cast<double>(gpu.allocatedBytes) / megabyte));
    answer.add("frame", std::move(frame));

    // The disk of the project, followed: whether the editor is in front (it takes what changes on
    // disk by itself then) and whether a change waits to be taken.
    Rain::Value disk = Rain::Value::object();
    disk.add("inFront", m_editor.diskWatch().isInFront());
    disk.add("waiting", m_editor.diskWatch().isWaiting());
    answer.add("disk", std::move(disk));

    Rain::Value features = Rain::Value::array();
    for (const std::string &feature : m_editor.documents().projectSettings().shaderFeatures) { features.push(feature); }
    answer.add("shaderFeatures", std::move(features));

    size_t errors = 0;
    LogHistory::read(0, LogLevel::_ERROR, 0, &errors);
    Rain::Value log = Rain::Value::object();
    log.add("next", LogHistory::nextSequence());
    log.add("errors", errors);
    answer.add("log", std::move(log));
    return Mcp::ready(textResult(Rain::writeJson(answer)));
}

Mcp::PendingResult EditorMcp::logs(const Rain::Value &arguments) {
    const uint64_t since = static_cast<uint64_t>(std::max(0.0, Mcp::numberArgument(arguments, "since", 0.0)));
    const size_t limit = static_cast<size_t>(std::clamp(Mcp::numberArgument(arguments, "limit", 100.0), 1.0, 1000.0));
    const std::string level = Mcp::stringArgument(arguments, "level", "info");
    LogLevel minimum = LogLevel::_INFO;
    if (level == "trace") {
        minimum = LogLevel::_TRACE;
    } else if (level == "warning") {
        minimum = LogLevel::_WARNING;
    } else if (level == "error") {
        minimum = LogLevel::_ERROR;
    } else if (level != "info") {
        return Mcp::ready(Mcp::ToolResult::error("level is one of: trace, info, warning, error"));
    }
    size_t total = 0;
    // The sequence first: a line written between the two calls is returned again next time
    // rather than lost.
    const uint64_t next = LogHistory::nextSequence();
    const std::vector<LogRecord> lines = LogHistory::read(since, minimum, limit, &total);
    std::string text = "next=" + std::to_string(next) + "\n";
    if (total > lines.size()) {
        text += "(the last " + std::to_string(lines.size()) + " of " + std::to_string(total) + " lines)\n";
    }
    appendLines(text, lines);
    return Mcp::ready(textResult(std::move(text)));
}

/* ------------------------------ screenshot ------------------------------ */

void EditorMcp::showViewport(ViewportKind view) {
    if (!m_editor.controller()) { return; }
    if (view == ViewportKind::Scene) {
        m_editor.controller()->showWorldView();
    } else {
        m_editor.controller()->openGameView();
    }
}

Mcp::PendingResult EditorMcp::screenshot(const Rain::Value &arguments) {
    auto shot = std::make_shared<Screenshot>();
    const std::string view = Mcp::stringArgument(arguments, "view");
    // The window is there without a project (the start screen); a viewport is not.
    if (!hasProject() && view != "window") {
        return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor: only the `window` view can be shot"));
    }
    if (view.empty()) {
        shot->view = m_editor.isPlaying() ? ViewportKind::Game : ViewportKind::Scene;
    } else if (view == "world") {
        shot->view = ViewportKind::Scene;
    } else if (view == "game") {
        shot->view = ViewportKind::Game;
    } else if (view == "window") {
        shot->window = true;
    } else if (view == "settings" || view == "export") {
        if (m_editor.ui().auxiliaryWindow() == nullptr) {
            return Mcp::ready(Mcp::ToolResult::error(
                "That window is closed: `show` or the command settings.open opens the Settings, the command export.open — "
                "the Export window"
            ));
        }
        shot->window = true;
        shot->settings = true;
    } else {
        return Mcp::ready(Mcp::ToolResult::error("view is \"world\", \"game\", \"window\", \"settings\" or \"export\""));
    }
    // A viewport shows a world: with none open there is nothing to shoot but the window.
    if (!shot->window && !m_editor.hasWorld()) {
        return Mcp::ready(Mcp::ToolResult::error("No world is open: open one with `open_world`, or shoot the `window`"));
    }
    shot->maxWidth = static_cast<int>(std::clamp(Mcp::numberArgument(arguments, "width", SCREENSHOT_DEFAULT_WIDTH), 64.0, 8192.0));
    shot->returnImage = Mcp::boolArgument(arguments, "image", true);
    const std::string file = Mcp::stringArgument(arguments, "file");
    if (!file.empty()) {
        shot->file = path_t(file);
        if (shot->file.is_relative()) {
            if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("file is an absolute path when no project is open")); }
            shot->file = m_editor.project().getOpenedProjectPath() / shot->file;
        }
        shot->file = shot->file.lexically_normal();
        if (shot->file.extension() != ".png") { return Mcp::ready(Mcp::ToolResult::error("file is a .png file")); }
    } else if (!shot->returnImage) {
        return Mcp::ready(Mcp::ToolResult::error("Nothing to answer with: image is false and there is no file"));
    }
    shot->notBefore = nowSeconds() + std::clamp(Mcp::numberArgument(arguments, "wait", 0.0), 0.0, 30.0);
    if (hasProject()) { shot->shownBefore = m_editor.viewports().live(); }
    return [this, shot]() { return stepScreenshot(*shot); };
}

std::optional<Mcp::ToolResult> EditorMcp::stepScreenshot(Screenshot &shot) {
    const auto fail = [&](std::string message) {
        if (shot.switched && hasProject()) { showViewport(shot.shownBefore); }
        return Mcp::ToolResult::error(std::move(message));
    };
    if (!hasProject() && !shot.window) { return fail("The project was closed"); }
    if (shot.reading) {
        if (Miren::getFrameNumber() < shot.readyFrame) { return std::nullopt; }
        return finishScreenshot(shot);
    }
    if (shot.window) {
        // The window as it is: nothing comes forward for it. A frame passes first, so that what
        // an earlier call changed is on screen.
        if (++shot.frames < 2 || nowSeconds() < shot.notBefore) { return std::nullopt; }
        if (!captureWindow(shot)) { return fail("The window of the editor has no size"); }
        return std::nullopt;
    }
    const EditorViewports &viewports = m_editor.viewports();
    const ViewportSurface &surface = viewports.surface(shot.view);
    // The view renders while the world tab shows it and is in front.
    const bool live = viewports.live() == shot.view && viewports.isOnScreen() && surface.isReady() &&
                      surface.pixelWidth() > 0 && surface.pixelHeight() > 0;
    ++shot.frames;
    if (!live) {
        shot.liveFrames = 0;
        // The tab comes forward (again, should the user have switched away meanwhile).
        if (shot.frames % 30 == 1) {
            showViewport(shot.view);
            shot.switched = true;
        }
        if (shot.frames > SCREENSHOT_TIMEOUT_FRAMES) {
            return fail(
                std::string("The ") + viewName(shot.view) +
                " view does not render. One of the two renders per frame — the one the world tab shows — and "
                "the tab of the active world has to be in front."
            );
        }
        return std::nullopt;
    }
    ++shot.liveFrames;
    if (shot.liveFrames < SCREENSHOT_LIVE_FRAMES || nowSeconds() < shot.notBefore) { return std::nullopt; }

    shot.width = surface.pixelWidth();
    shot.height = surface.pixelHeight();
    shot.pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(shot.width) * shot.height * 4);
    // The command owns a reference of its own: the buffer lives until the render thread has
    // written it, whatever happens to the call.
    auto *keep = new std::shared_ptr<std::vector<uint8_t>>(shot.pixels);
    shot.readyFrame = Miren::readTexture(
        surface.result(), Foundation::Memory(shot.pixels->data(), keep, [](void *, void *userData) {
            delete static_cast<std::shared_ptr<std::vector<uint8_t>> *>(userData);
        })
    );
    shot.reading = true;
    return std::nullopt;
}

bool EditorMcp::captureWindow(Screenshot &shot) {
    Fern::Window *window = shot.settings ? m_editor.ui().auxiliaryWindow() : m_editor.window();
    if (window == nullptr) { return false; }
    const Fern::Size size = window->getSize();
    const Fern::Size dpi = window->getDpi();
    const uint32_t width = static_cast<uint32_t>(size.width * dpi.width);
    const uint32_t height = static_cast<uint32_t>(size.height * dpi.height);
    if (width == 0 || height == 0) { return false; }
    if (!m_windowCapture.isValid() || width != m_windowCaptureWidth || height != m_windowCaptureHeight) {
        if (m_windowCapture.isValid()) {
            Miren::deleteFrameBuffer(m_windowCaptureBuffer);
            Miren::deleteTexture(m_windowCapture);
        }
        Miren::TextureCreate create;
        create.m_width = width;
        create.m_height = height;
        create.m_format = Miren::TextureFormat::RGBA8;
        create.m_minFiltering = Miren::NEAREST;
        create.m_magFiltering = Miren::NEAREST;
        create.m_wrap = Miren::CLAMP;
        m_windowCapture = Miren::createTexture(create);
        Miren::FrameBufferAttachment attachments[] = {m_windowCapture};
        m_windowCaptureBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
        m_windowCaptureWidth = width;
        m_windowCaptureHeight = height;
    }
    // The chrome is drawn once more, into the texture: the window itself is drawn as always.
    const Miren::ViewDescription capture{
        .frameBuffer = m_windowCaptureBuffer,
        .viewport = Miren::Rect(0, 0, static_cast<int>(width), static_cast<int>(height)),
    };
    if (shot.settings) {
        m_editor.ui().renderAuxiliaryTo(capture);
    } else {
        m_editor.ui().renderTo(capture);
    }

    shot.width = width;
    shot.height = height;
    shot.pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(width) * height * 4);
    auto *keep = new std::shared_ptr<std::vector<uint8_t>>(shot.pixels);
    shot.readyFrame = Miren::readTexture(
        m_windowCapture, Foundation::Memory(shot.pixels->data(), keep, [](void *, void *userData) {
            delete static_cast<std::shared_ptr<std::vector<uint8_t>> *>(userData);
        })
    );
    shot.reading = true;
    return true;
}

Mcp::ToolResult EditorMcp::finishScreenshot(Screenshot &shot) {
    if (shot.switched) { showViewport(shot.shownBefore); }
    // RGBA in the row order of the graphics API → RGB from the top row, scaled down by averaging
    // the pixels each output pixel covers (a viewport on a 2x display is twice the asked width).
    const uint64_t width = shot.width;
    const uint64_t height = shot.height;
    const uint64_t outWidth = std::min<uint64_t>(width, static_cast<uint64_t>(shot.maxWidth));
    const uint64_t outHeight = std::max<uint64_t>(1, (height * outWidth + width / 2) / width);
    const bool bottomUp = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft;
    const uint8_t *source = shot.pixels->data();
    std::vector<uint8_t> rgb(outWidth * outHeight * 3);
    for (uint64_t y = 0; y < outHeight; ++y) {
        const uint64_t y0 = y * height / outHeight;
        const uint64_t y1 = std::max(y0 + 1, (y + 1) * height / outHeight);
        for (uint64_t x = 0; x < outWidth; ++x) {
            const uint64_t x0 = x * width / outWidth;
            const uint64_t x1 = std::max(x0 + 1, (x + 1) * width / outWidth);
            uint64_t sum[3] = {0, 0, 0};
            for (uint64_t sy = y0; sy < y1; ++sy) {
                const uint8_t *row = source + (bottomUp ? height - 1 - sy : sy) * width * 4;
                for (uint64_t sx = x0; sx < x1; ++sx) {
                    sum[0] += row[sx * 4];
                    sum[1] += row[sx * 4 + 1];
                    sum[2] += row[sx * 4 + 2];
                }
            }
            const uint64_t count = (y1 - y0) * (x1 - x0);
            uint8_t *out = rgb.data() + (y * outWidth + x) * 3;
            out[0] = static_cast<uint8_t>((sum[0] + count / 2) / count);
            out[1] = static_cast<uint8_t>((sum[1] + count / 2) / count);
            out[2] = static_cast<uint8_t>((sum[2] + count / 2) / count);
        }
    }
    shot.pixels.reset();

    size_t pngSize = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(
        rgb.data(), static_cast<int>(outWidth), static_cast<int>(outHeight), 3, &pngSize, 6, MZ_FALSE
    );
    if (png == nullptr || pngSize == 0) { return Mcp::ToolResult::error("The picture could not be packed as PNG"); }
    Mcp::ToolResult result;
    result.png.assign(static_cast<const uint8_t *>(png), static_cast<const uint8_t *>(png) + pngSize);
    mz_free(png);

    if (!shot.file.empty()) {
        std::error_code error;
        std::filesystem::create_directories(shot.file.parent_path(), error);
        std::ofstream output(shot.file, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(result.png.data()), static_cast<std::streamsize>(result.png.size()));
        output.close();
        if (!output) { return Mcp::ToolResult::error("The picture could not be written to " + shot.file.string()); }
    }

    Rain::Value answer = Rain::Value::object();
    answer.add("view", shot.settings ? "settings" : shot.window ? "window" : viewName(shot.view));
    answer.add("width", outWidth);
    answer.add("height", outHeight);
    if (!shot.file.empty()) { answer.add("file", shot.file.string()); }
    result.text = Rain::writeJson(answer);
    if (!shot.returnImage) { result.png.clear(); }
    return result;
}

/* ------------------------------ acting on the editor ------------------------------ */

Mcp::PendingResult EditorMcp::reload(const Rain::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    const bool discard = Mcp::boolArgument(arguments, "discard", false);
    const uint64_t from = LogHistory::nextSequence();
    // Don't Save of the question about a conflict is "take the version on disk".
    SystemTools::Unattended unattended(discard ? SaveChangesChoice::DontSave : SaveChangesChoice::Cancel);
    // With `discard` the disk wins everywhere: a document with unsaved edits is read again even
    // when its file has not changed.
    if (discard) { m_editor.documentActions().revertUnsaved(); }
    m_editor.documentActions().reloadFromDisk();
    Mcp::ToolResult result;
    if (!discard && !unattended.questions().empty()) {
        result.isError = true;
        result.text = "Reload stopped at a conflict: " + unattended.questions().front() +
                      ". `reload` with discard=true takes the version on disk; `save` with overwrite=true keeps "
                      "the version of the editor.\n";
    }
    result.text += logSince(from);
    return Mcp::ready(std::move(result));
}

Mcp::PendingResult EditorMcp::save(const Rain::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    const bool overwrite = Mcp::boolArgument(arguments, "overwrite", false);
    const uint64_t from = LogHistory::nextSequence();
    SystemTools::Unattended unattended(overwrite ? SaveChangesChoice::Save : SaveChangesChoice::Cancel);
    const bool saved = m_editor.documentActions().saveAll();
    Mcp::ToolResult result;
    if (!saved && unattended.questions().empty()) {
        // Nothing was asked: a file could not be written (a full disk, a read-only file).
        result.isError = true;
        result.text = "Not everything was saved: a file could not be written, the log below says why.\n";
    } else if (!saved) {
        result.isError = true;
        result.text = "Not everything was saved: " + unattended.questions().front() +
                      ". A file changed on disk needs `save` with overwrite=true (the version of the editor wins) "
                      "or `reload` with discard=true (the disk wins); a new world that has no file yet is named "
                      "by the user in the editor.\n";
    } else if (dirtyDocuments().empty()) {
        result.text = "Saved.\n";
    }
    result.text += logSince(from);
    return Mcp::ready(std::move(result));
}

Mcp::PendingResult EditorMcp::focus(const Rain::Value &arguments) {
    const bool front = Mcp::boolArgument(arguments, "front", true);
    const double until = nowSeconds() + std::clamp(Mcp::numberArgument(arguments, "wait", 1.0), 0.0, 30.0);
    const uint64_t from = LogHistory::nextSequence();
    m_editor.diskWatch().setInFront(front);
    return [this, front, until, from]() -> std::optional<Mcp::ToolResult> {
        if (nowSeconds() < until) { return std::nullopt; }
        return textResult(std::string(front ? "The editor is in front.\n" : "The editor is behind.\n") + logSince(from));
    };
}

Mcp::PendingResult EditorMcp::play() {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    if (m_editor.isPlaying()) { return Mcp::ready(textResult("The game is playing already.")); }
    const uint64_t from = LogHistory::nextSequence();
    SystemTools::Unattended unattended;
    // The scripts changed since their build: the answer waits for the build and the start.
    if (m_editor.buildBeforePlay(/*ask*/ false)) {
        return [this, from]() -> std::optional<Mcp::ToolResult> {
            if (m_editor.isPlaying()) { return textResult("Built the scripts, playing.\n" + logSince(from)); }
            if (m_editor.isWaitingToPlay()) { return std::nullopt; }
            return Mcp::ToolResult::error("The scripts did not build, so the game did not start.\n" + logSince(from));
        };
    }
    m_editor.play();
    return Mcp::ready(textResult("Playing.\n" + logSince(from)));
}

Mcp::PendingResult EditorMcp::stop() {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    if (!m_editor.isPlaying()) { return Mcp::ready(textResult("The game is not playing.")); }
    const uint64_t from = LogHistory::nextSequence();
    SystemTools::Unattended unattended;
    m_editor.stop();
    return Mcp::ready(textResult("Stopped.\n" + logSince(from)));
}

Mcp::PendingResult EditorMcp::camera(const Rain::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    CameraController &controller = m_editor.viewports().cameraController();
    double values[3];
    if (arguments.find("position") != nullptr) {
        if (!Mcp::numbersArgument(arguments, "position", values, 3)) {
            return Mcp::ready(Mcp::ToolResult::error("position is an array of three numbers: [x, y, z]"));
        }
        controller.setPosition(glm::vec3(values[0], values[1], values[2]));
    }
    if (arguments.find("target") != nullptr) {
        if (!Mcp::numbersArgument(arguments, "target", values, 3)) {
            return Mcp::ready(Mcp::ToolResult::error("target is an array of three numbers: [x, y, z]"));
        }
        controller.lookAt(glm::vec3(values[0], values[1], values[2]));
    }
    return Mcp::ready(textResult(Rain::writeJson(cameraValue())));
}

Mcp::PendingResult EditorMcp::openWorld(const Rain::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    const std::string name = Mcp::stringArgument(arguments, "path");
    if (name.empty()) { return Mcp::ready(Mcp::ToolResult::error("path is the .world or .prefab file to open")); }
    path_t file(name);
    if (file.is_relative()) { file = m_editor.project().getOpenedProjectPath() / file; }
    file = file.lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        return Mcp::ready(Mcp::ToolResult::error("No such file: " + file.string()));
    }
    const uint64_t from = LogHistory::nextSequence();
    SystemTools::Unattended unattended;
    // The game plays a copy of the active world: it ends before another world takes that place.
    m_editor.stop();
    if (m_editor.documents().openWorld(file) == nullptr) {
        return Mcp::ready(Mcp::ToolResult::error(file.filename().string() + " could not be opened: " + file.string() + "\n" + logSince(from)));
    }
    if (m_editor.controller()) { m_editor.controller()->showWorldView(); }
    return Mcp::ready(textResult("Opened " + file.filename().string() + ".\n" + logSince(from)));
}

namespace {

    /* ------------------------------ the elements of the UI ------------------------------ */

    using PandaUI::AccessibilityElement;

    constexpr double TARGET_WAIT_SECONDS = 2.0; // how long a tool waits for the element a path names
    constexpr size_t NAMES_IN_AN_ERROR = 24;

    // What an element is, in a line of an answer: `button "Add Item"`, `view #inspector`.
    std::string describeElement(const std::string &role, const std::string &id, const std::string &label) {
        std::string text = role;
        if (!id.empty()) { text += " #" + id; }
        if (!label.empty()) { text += " \"" + label + "\""; }
        return text;
    }

    std::string describeElement(const AccessibilityElement &element) {
        return describeElement(element.role, element.id, element.label);
    }

    // The elements a view is in, the innermost first: the view itself when it is one, then the
    // ones around it. A part of an element (the title of a button) is not named: the element is.
    std::vector<std::string> elementsAround(PandaUI::View *view, size_t limit) {
        std::vector<PandaUI::View *> chain;
        for (; view != nullptr; view = view->getSuperview()) { chain.push_back(view); }
        std::vector<std::string> names; // the outermost first
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            PandaUI::Accessibility described;
            (*it)->describeAccessibility(described);
            const std::string &id = (*it)->getAccessibilityId();
            if (described.role == nullptr && id.empty()) { continue; }
            names.push_back(describeElement(described.role != nullptr ? described.role : "view", id, described.label));
            if (described.atomic) { break; }
        }
        std::reverse(names.begin(), names.end());
        if (names.size() > limit) { names.resize(limit); }
        return names;
    }

    bool isInside(const PandaUI::View *view, const PandaUI::View *container) {
        for (; view != nullptr; view = view->getSuperview()) {
            if (view == container) { return true; }
        }
        return false;
    }

    // Asks every view that makes its rows on demand, `view` and what is in it, for the row `name`.
    bool revealIn(PandaUI::View &view, const std::string &name) {
        bool revealed = view.revealAccessibilityElement(name);
        for (const std::shared_ptr<PandaUI::View> &subview : view.getSubviews()) {
            if (!subview->isHidden() && revealIn(*subview, name)) { revealed = true; }
        }
        return revealed;
    }

    void appendNames(const std::vector<AccessibilityElement> &elements, std::vector<std::string> &names) {
        for (const AccessibilityElement &element : elements) {
            const std::string &name = element.name();
            if (std::find(names.begin(), names.end(), name) == names.end()) { names.push_back(name); }
        }
    }

    std::string pathPrefix(const PandaUI::AccessibilityMatch &match, size_t count) {
        std::string prefix;
        for (size_t i = 0; i < count && i < match.segments.size(); ++i) {
            if (i > 0) { prefix += "/"; }
            prefix += match.segments[i];
        }
        return prefix;
    }

    // Why a path names nothing: the segment that names nothing, where it was looked for and what
    // is there to name.
    std::string missingText(const std::vector<AccessibilityElement> &tree, const PandaUI::AccessibilityMatch &match) {
        std::string text = "No element `" + match.segments[match.resolved] + "`";
        std::vector<std::string> names;
        if (match.resolved == 0) {
            text += " in the window.";
            appendNames(tree, names);
        } else {
            text += " in `" + pathPrefix(match, match.resolved) + "`.";
            for (const AccessibilityElement *element : match.scope) { appendNames(element->children, names); }
        }
        if (names.empty()) { return text + " Nothing is there."; }
        text += " There: ";
        for (size_t i = 0; i < names.size() && i < NAMES_IN_AN_ERROR; ++i) {
            if (i > 0) { text += ", "; }
            text += "`" + names[i] + "`";
        }
        if (names.size() > NAMES_IN_AN_ERROR) { text += ", and " + std::to_string(names.size() - NAMES_IN_AN_ERROR) + " more"; }
        return text + ".";
    }

    std::string ambiguousText(const std::string &path, const PandaUI::AccessibilityMatch &match) {
        std::string text = "`" + path + "` names " + std::to_string(match.elements.size()) + " elements:";
        for (size_t i = 0; i < match.elements.size() && i < 6; ++i) { text += (i > 0 ? ", " : " ") + describeElement(*match.elements[i]); }
        if (match.elements.size() > 6) { text += ", …"; }
        return text + ". Say which one: `" + match.segments.back() + "[0]` is the first.";
    }

    // Window points per pixel of a screenshot of the window asked with the width `of`.
    double pointsPerScreenshotPixel(Fern::Window &window, double of) {
        const Fern::Size size = window.getSize();
        const Fern::Size dpi = window.getDpi();
        const double imageWidth = std::min(std::max(of, 1.0), static_cast<double>(size.width) * dpi.width);
        return imageWidth > 0.0 ? static_cast<double>(size.width) / imageWidth : 1.0;
    }

    // `depth` — how many levels of what is inside the element are written; negative — all of them.
    Rain::Value elementValue(const AccessibilityElement &element, double scale, int depth) {
        Rain::Value value = Rain::Value::object();
        value.add("role", element.role);
        if (!element.id.empty()) { value.add("id", element.id); }
        if (!element.label.empty()) { value.add("label", element.label); }
        if (!element.value.empty()) { value.add("value", element.value); }
        Rain::Value frame = Rain::Value::array();
        for (float side : {element.frame.origin.x, element.frame.origin.y, element.frame.size.width, element.frame.size.height}) {
            frame.push(static_cast<int>(std::lround(side / scale)));
        }
        value.add("frame", std::move(frame));
        if (!element.enabled) { value.add("disabled", true); }
        if (element.selected) { value.add("selected", true); }
        if (element.focused) { value.add("focused", true); }
        if (!element.inSight()) { value.add("outOfSight", true); }
        if (!element.children.empty()) {
            if (depth == 0) {
                value.add("more", static_cast<unsigned>(element.children.size()));
            } else {
                Rain::Value children = Rain::Value::array();
                for (const AccessibilityElement &child : element.children) { children.push(elementValue(child, scale, depth - 1)); }
                value.add("children", std::move(children));
            }
        }
        return value;
    }

    const char *cursorName(PandaUI::Cursor cursor) {
        switch (cursor) {
            case PandaUI::Cursor::IBeam: return "text";
            case PandaUI::Cursor::Crosshair: return "crosshair";
            case PandaUI::Cursor::PointingHand: return "hand";
            case PandaUI::Cursor::ResizeEW: return "resize-horizontal";
            case PandaUI::Cursor::ResizeNS: return "resize-vertical";
            case PandaUI::Cursor::ResizeNESW: return "resize-nesw";
            case PandaUI::Cursor::ResizeNWSE: return "resize-nwse";
            case PandaUI::Cursor::ResizeAll: return "move";
            case PandaUI::Cursor::NotAllowed: return "not-allowed";
            case PandaUI::Cursor::Inherit:
            case PandaUI::Cursor::Arrow: return "arrow";
        }
        return "arrow";
    }

    PandaUI::Key keyFromName(const std::string &name) {
        using PandaUI::Key;
        if (name.size() == 1) {
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { return static_cast<Key>(c); }
            switch (c) {
                case ' ': return Key::Space;
                case ',': return Key::Comma;
                case '-': return Key::Minus;
                case '.': return Key::Period;
                case '/': return Key::Slash;
                case '=': return Key::Equal;
                default: return Key::Unknown;
            }
        }
        std::string lower = name;
        for (char &c : lower) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        static const std::pair<const char *, Key> NAMES[] = {
            {"enter", Key::Enter}, {"return", Key::Enter}, {"escape", Key::Escape}, {"esc", Key::Escape},
            {"tab", Key::Tab}, {"backspace", Key::Backspace}, {"delete", Key::Delete}, {"space", Key::Space},
            {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right},
            {"home", Key::Home}, {"end", Key::End}, {"pageup", Key::PageUp}, {"pagedown", Key::PageDown},
        };
        for (const auto &[text, key] : NAMES) {
            if (lower == text) { return key; }
        }
        if (lower.size() >= 2 && lower[0] == 'f') {
            const int number = std::atoi(lower.c_str() + 1);
            if (number >= 1 && number <= 12) { return static_cast<Key>(static_cast<int>(Key::F1) + number - 1); }
        }
        return Key::Unknown;
    }

    // The code points of a UTF-8 text.
    std::vector<uint32_t> codepointsOf(const std::string &text) {
        std::vector<uint32_t> result;
        for (size_t i = 0; i < text.size();) {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            int length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
            if (length == 0 || i + static_cast<size_t>(length) > text.size()) { break; }
            uint32_t codepoint = length == 1 ? lead : lead & (0xFFu >> (length + 1));
            for (int k = 1; k < length; ++k) { codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu); }
            result.push_back(codepoint);
            i += static_cast<size_t>(length);
        }
        return result;
    }

} // namespace

// Input made in place of the user goes where the user's would: to the second window (the
// Settings, the Export) while one is open (it is in front of the main one), to the main window
// otherwise.
PandaUI::Context &EditorMcp::inputContext() const {
    PandaUI::Context *settings = m_editor.ui().auxiliaryContext();
    return settings != nullptr ? *settings : m_editor.ui().context();
}

Fern::Window *EditorMcp::inputWindow() const {
    Fern::Window *settings = m_editor.ui().auxiliaryWindow();
    return settings != nullptr ? settings : m_editor.window();
}

Rain::Value EditorMcp::pointerValue() const {
    PandaUI::Context &context = inputContext();
    // The elements under the pointer, from the innermost out: what a click there reaches.
    Rain::Value under = Rain::Value::array();
    for (const std::string &name : elementsAround(context.getHoveredView(), 6)) { under.push(name); }
    const std::vector<std::string> focused = elementsAround(context.getFocusedView(), 1);
    Rain::Value answer = Rain::Value::object();
    answer.add("under", std::move(under));
    answer.add("cursor", cursorName(context.getCurrentCursor()));
    answer.add("focus", focused.empty() ? Rain::Value() : Rain::Value(focused.front()));
    return answer;
}

Mcp::PendingResult EditorMcp::playInput(std::vector<InputStep> steps) {
    auto queue = std::make_shared<std::vector<InputStep>>(std::move(steps));
    auto next = std::make_shared<size_t>(0);
    // The real mouse steps aside for the gesture and comes back when the call is over — however
    // it ends: a client that went away mid-gesture drops the pending answer, and the guard with it.
    m_editor.ui().setPointerOverridden(true);
    EditorUI *ui = &m_editor.ui();
    std::shared_ptr<void> release(nullptr, [ui](void *) { ui->setPointerOverridden(false); });
    return [this, queue, next, release]() -> std::optional<Mcp::ToolResult> {
        PandaUI::Context &context = inputContext();
        if (*next >= queue->size()) {
            m_editor.ui().setPointerOverridden(false);
            Mcp::ToolResult result;
            result.text = Rain::writeJson(pointerValue());
            return result;
        }
        const InputStep &step = (*queue)[(*next)++];
        // Nobody is at the screen: what the input sets off asks no native dialog.
        SystemTools::Unattended unattended;
        auto pointerEvent = [&step] {
            PandaUI::PointerEvent event;
            event.type = PandaUI::PointerType::Mouse;
            event.position = step.position;
            event.isShiftDown = step.shift;
            event.isControlDown = step.control;
            event.isAltDown = step.alt;
            event.isSuperDown = step.super;
            return event;
        };
        switch (step.kind) {
            case InputStep::Kind::Move: {
                PandaUI::PointerEvent event = pointerEvent();
                const PandaUI::Point last = context.getMousePosition();
                event.delta = PandaUI::Point(step.position.x - last.x, step.position.y - last.y);
                context.handlePointerMove(event);
                break;
            }
            case InputStep::Kind::Down: {
                if (step.newClick) { context.forgetLastClick(); }
                PandaUI::PointerEvent event = pointerEvent();
                event.button = step.button;
                context.handlePointerDown(event);
                break;
            }
            case InputStep::Kind::Up: {
                PandaUI::PointerEvent event = pointerEvent();
                event.button = step.button;
                context.handlePointerUp(event);
                break;
            }
            case InputStep::Kind::Drop:
                m_editor.dropFiles(step.files, step.position); // as a drop of the window: EditorLayer::onEvent
                break;
            case InputStep::Kind::Scroll: {
                PandaUI::ScrollEvent event;
                event.position = step.position;
                event.delta = step.delta;
                event.isTrackpad = step.trackpad;
                event.magnification = step.magnification;
                context.handleScroll(event);
                break;
            }
            case InputStep::Kind::KeyDown:
            case InputStep::Kind::KeyUp: {
                PandaUI::KeyEvent event;
                event.key = step.key;
                event.isShiftDown = step.shift;
                event.isControlDown = step.control;
                event.isAltDown = step.alt;
                event.isSuperDown = step.super;
                if (step.kind == InputStep::Kind::KeyDown) {
                    context.handleKeyDown(event);
                } else {
                    context.handleKeyUp(event);
                }
                break;
            }
            case InputStep::Kind::Text: {
                PandaUI::TextInputEvent event;
                event.codepoint = step.codepoint;
                context.handleTextInput(event);
                break;
            }
            case InputStep::Kind::Idle: break;
        }
        return std::nullopt;
    };
}

bool EditorMcp::resolveTarget(PandaUI::Context &context, Target &target, std::string &why) {
    const std::vector<AccessibilityElement> tree = PandaUI::accessibilityTree(context);
    const PandaUI::AccessibilityMatch match = PandaUI::findAccessibilityElements(tree, target.path);
    if (match.segments.empty()) {
        why = "The path of the target is empty";
        return false;
    }
    if (match.elements.empty()) {
        // A row that a tree or a collection has not made: it is out of sight. Asked for, it is
        // there after the next update.
        const std::string &name = match.segments[match.resolved];
        if (match.resolved == 0) {
            for (PandaUI::View *layer : context.layers()) { revealIn(*layer, name); }
        } else {
            for (const AccessibilityElement *element : match.scope) { revealIn(*element->view, name); }
        }
        target.wasSeen = false;
        why = missingText(tree, match);
        return false;
    }
    if (match.elements.size() > 1) {
        target.wasSeen = false;
        why = ambiguousText(target.path, match);
        return false;
    }
    const AccessibilityElement &element = *match.elements.front();
    const std::string name = "`" + target.path + "` (" + describeElement(element) + ")";
    if (!element.enabled) {
        why = name + " is disabled";
        return false;
    }
    // Cut by what it scrolls in: the scroll views around it bring it into sight, the inner first.
    const bool cut = element.visible.size.width < element.frame.size.width || element.visible.size.height < element.frame.size.height;
    if (cut) {
        bool scrolled = false;
        for (PandaUI::View *view = element.view->getSuperview(); view != nullptr; view = view->getSuperview()) {
            auto *scroll = dynamic_cast<PandaUI::ScrollView *>(view);
            if (scroll != nullptr && scroll->scrollToVisible(element.view->getWorldFrame())) { scrolled = true; }
        }
        if (scrolled) {
            target.wasSeen = false;
            why = name + " is being scrolled into sight";
            return false;
        }
        if (!element.inSight()) {
            why = name + " is out of sight: what it is in leaves no part of it in the window";
            return false;
        }
    }
    // It stands still: the same place two frames in a row (a section that opens, a popup that
    // arrives move what is in them).
    const PandaUI::Rect &frame = element.frame;
    const bool still = target.wasSeen && frame.origin.x == target.seen.origin.x && frame.origin.y == target.seen.origin.y &&
                       frame.size.width == target.seen.size.width && frame.size.height == target.seen.size.height;
    target.seen = frame;
    target.wasSeen = true;
    if (!still) {
        why = name + " is moving";
        return false;
    }
    const PandaUI::Point point(
        element.visible.origin.x + element.visible.size.width * std::clamp(target.at.x, 0.f, 1.f),
        element.visible.origin.y + element.visible.size.height * std::clamp(target.at.y, 0.f, 1.f)
    );
    if (target.takesPress) {
        // What a press at the point reaches is the element, a part of it, or — where the element
        // takes no input itself (a label in a row) — what it is in. Anything else lies over it.
        PandaUI::View *hit = context.viewAt(point);
        if (hit == nullptr || (!isInside(hit, element.view) && !isInside(element.view, hit))) {
            const std::vector<std::string> over = elementsAround(hit, 1);
            why = name + " is covered" + (over.empty() ? std::string() : " by " + over.front());
            return false;
        }
    }
    target.point = point;
    return true;
}

Mcp::PendingResult EditorMcp::playAtTargets(
    std::vector<Target> targets, double wait, std::function<std::vector<InputStep>(const std::vector<PandaUI::Point> &)> steps
) {
    struct State {
        std::vector<Target> targets;
        double deadline = 0.0;
        std::string why;
        Mcp::PendingResult playing;
    };
    auto state = std::make_shared<State>();
    state->targets = std::move(targets);
    state->deadline = nowSeconds() + wait;
    return [this, state, steps = std::move(steps)]() -> std::optional<Mcp::ToolResult> {
        if (state->playing) { return state->playing(); }
        PandaUI::Context &context = inputContext();
        for (Target &target : state->targets) {
            if (resolveTarget(context, target, state->why)) { continue; }
            if (nowSeconds() < state->deadline) { return std::nullopt; }
            return Mcp::ToolResult::error(state->why);
        }
        std::vector<PandaUI::Point> points;
        for (const Target &target : state->targets) { points.push_back(target.point); }
        state->playing = playInput(steps(points));
        return state->playing();
    };
}

std::vector<EditorMcp::InputStep> EditorMcp::gestureSteps(const Gesture &gesture) {
    InputStep base;
    base.position = gesture.at;
    base.shift = gesture.shift;
    base.alt = gesture.alt;
    base.control = gesture.control;
    base.super = gesture.super;
    auto step = [&base](InputStep::Kind kind) {
        InputStep result = base;
        result.kind = kind;
        return result;
    };
    std::vector<InputStep> steps;
    auto press = [&](PandaUI::PointerButton which, bool newClick) {
        InputStep down = step(InputStep::Kind::Down);
        down.button = which;
        down.newClick = newClick;
        steps.push_back(down);
        InputStep up = step(InputStep::Kind::Up);
        up.button = which;
        steps.push_back(up);
    };
    // The pointer arrives first: the hover of a frame is what the press of the next one meets.
    steps.push_back(step(InputStep::Kind::Move));
    const std::string &action = gesture.action;
    if (action == "click") {
        press(PandaUI::PointerButton::Left, true);
    } else if (action == "doubleClick") {
        press(PandaUI::PointerButton::Left, true);
        press(PandaUI::PointerButton::Left, false);
    } else if (action == "rightClick") {
        press(PandaUI::PointerButton::Right, true);
    } else if (action == "move") {
        // The move alone.
    } else if (action == "down") {
        InputStep down = step(InputStep::Kind::Down);
        down.button = gesture.button;
        down.newClick = true;
        steps.push_back(down);
    } else if (action == "up") {
        InputStep up = step(InputStep::Kind::Up);
        up.button = gesture.button;
        steps.push_back(up);
    } else if (action == "drag") {
        const int count = std::clamp(gesture.steps > 0 ? gesture.steps : 8, 1, 240);
        InputStep down = step(InputStep::Kind::Down);
        down.button = gesture.button;
        down.newClick = true;
        steps.push_back(down);
        for (int i = 1; i <= count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count);
            InputStep move = step(InputStep::Kind::Move);
            move.position = PandaUI::Point(
                gesture.at.x + (gesture.to.x - gesture.at.x) * t, gesture.at.y + (gesture.to.y - gesture.at.y) * t
            );
            steps.push_back(move);
        }
        // The pointer rests over the place of the drop for a frame: what lies there has answered
        // whether it takes the drop before the button goes up.
        InputStep rest = step(InputStep::Kind::Move);
        rest.position = gesture.to;
        steps.push_back(rest);
        InputStep up = step(InputStep::Kind::Up);
        up.button = gesture.button;
        up.position = gesture.to;
        steps.push_back(up);
    } else if (action == "scroll") {
        InputStep scroll = step(InputStep::Kind::Scroll);
        scroll.delta = gesture.scrollDelta;
        scroll.trackpad = gesture.trackpad;
        // A swipe is many events in a row, a frame apart: `steps` of them, each by dx, dy.
        const int count = std::clamp(gesture.steps > 0 ? gesture.steps : 1, 1, 240);
        for (int i = 0; i < count; ++i) { steps.push_back(scroll); }
    } else if (action == "drop") {
        InputStep drop = step(InputStep::Kind::Drop);
        drop.files = gesture.files;
        steps.push_back(drop);
    } else if (action == "pinch") {
        InputStep pinch = step(InputStep::Kind::Scroll);
        pinch.trackpad = true;
        pinch.magnification = gesture.magnification;
        steps.push_back(pinch);
    }
    // What the gesture set off (a popover, a rebuilt form) is laid out and drawn before the answer.
    steps.push_back(step(InputStep::Kind::Idle));
    steps.push_back(step(InputStep::Kind::Idle));
    return steps;
}

Mcp::PendingResult EditorMcp::pointer(const Rain::Value &arguments) {
    Fern::Window *window = inputWindow();
    if (window == nullptr) { return Mcp::ready(Mcp::ToolResult::error("The editor has no window")); }
    const Fern::Size size = window->getSize();
    if (size.width <= 0.f || size.height <= 0.f) { return Mcp::ready(Mcp::ToolResult::error("The window of the editor has no size")); }
    // The coordinates are those of a screenshot of the window: its pixels, scaled down to `of`
    // when the window is wider.
    const double scale = pointsPerScreenshotPixel(*window, Mcp::numberArgument(arguments, "of", SCREENSHOT_DEFAULT_WIDTH));
    auto toWindow = [scale](double x, double y) {
        return PandaUI::Point(static_cast<float>(x * scale), static_cast<float>(y * scale));
    };

    Gesture gesture;
    gesture.action = Mcp::stringArgument(arguments, "action", "click");
    static const char *ACTIONS[] = {"click", "doubleClick", "rightClick", "move", "down", "up", "drag", "scroll", "pinch", "drop"};
    if (std::find(std::begin(ACTIONS), std::end(ACTIONS), gesture.action) == std::end(ACTIONS)) {
        return Mcp::ready(Mcp::ToolResult::error("action is click, doubleClick, rightClick, move, down, up, drag, scroll, pinch or drop"));
    }
    if (const Rain::Value *files = arguments.isObject() ? arguments.find("files") : nullptr; files && files->isArray()) {
        for (const Rain::Value &file : files->items()) {
            if (file.isString()) { gesture.files.push_back(file.getString()); }
        }
    }
    if (gesture.action == "drop" && gesture.files.empty()) {
        return Mcp::ready(Mcp::ToolResult::error("drop: pass `files`, the paths of the files the file manager drops"));
    }
    gesture.at = toWindow(Mcp::numberArgument(arguments, "x", 0.0), Mcp::numberArgument(arguments, "y", 0.0));
    gesture.to = toWindow(Mcp::numberArgument(arguments, "toX", 0.0), Mcp::numberArgument(arguments, "toY", 0.0));
    gesture.shift = Mcp::boolArgument(arguments, "shift", false);
    gesture.alt = Mcp::boolArgument(arguments, "alt", false);
    gesture.control = Mcp::boolArgument(arguments, "ctrl", false);
    gesture.super = Mcp::boolArgument(arguments, "super", false);
    // The button of a drag, a down and an up; a click names its button by its action.
    const std::string buttonName = Mcp::stringArgument(arguments, "button", "left");
    gesture.button = buttonName == "right"    ? PandaUI::PointerButton::Right
                     : buttonName == "middle" ? PandaUI::PointerButton::Middle
                                              : PandaUI::PointerButton::Left;
    gesture.steps = static_cast<int>(std::clamp(Mcp::numberArgument(arguments, "steps", 0.0), 0.0, 240.0));
    gesture.scrollDelta = PandaUI::Point(
        static_cast<float>(Mcp::numberArgument(arguments, "dx", 0.0)), static_cast<float>(-Mcp::numberArgument(arguments, "dy", 0.0))
    );
    gesture.trackpad = Mcp::boolArgument(arguments, "trackpad", false);
    gesture.magnification = static_cast<float>(Mcp::numberArgument(arguments, "scale", 0.0));

    // The place of the gesture: a point of a screenshot, or an element by its path.
    const std::string from = Mcp::stringArgument(arguments, "target");
    const std::string to = Mcp::stringArgument(arguments, "toTarget");
    const bool hasPoint = arguments.isObject() && arguments.find("x") != nullptr && arguments.find("y") != nullptr;
    if (from.empty() && !hasPoint) {
        return Mcp::ready(Mcp::ToolResult::error("Pass `target` (the path of an element, see `ui`) or `x` and `y` (a point of a `window` screenshot)"));
    }
    if (from.empty() && to.empty()) { return playInput(gestureSteps(gesture)); }

    auto share = [&arguments](const char *name) {
        double values[2] = {0.5, 0.5};
        Mcp::numbersArgument(arguments, name, values, 2);
        return PandaUI::Point(static_cast<float>(values[0]), static_cast<float>(values[1]));
    };
    std::vector<Target> targets;
    int fromIndex = -1;
    int toIndex = -1;
    if (!from.empty()) {
        fromIndex = static_cast<int>(targets.size());
        Target &target = targets.emplace_back();
        target.path = from;
        target.at = share("at");
    }
    if (!to.empty()) {
        toIndex = static_cast<int>(targets.size());
        Target &target = targets.emplace_back();
        target.path = to;
        target.at = share("toAt");
        target.takesPress = false; // the drop goes to whatever takes it there
    }
    const double wait = std::clamp(Mcp::numberArgument(arguments, "wait", TARGET_WAIT_SECONDS), 0.0, 30.0);
    return playAtTargets(std::move(targets), wait, [gesture, fromIndex, toIndex](const std::vector<PandaUI::Point> &points) {
        Gesture placed = gesture;
        if (fromIndex >= 0) { placed.at = points[static_cast<size_t>(fromIndex)]; }
        if (toIndex >= 0) { placed.to = points[static_cast<size_t>(toIndex)]; }
        return gestureSteps(placed);
    });
}

Mcp::PendingResult EditorMcp::key(const Rain::Value &arguments) {
    const std::string text = Mcp::stringArgument(arguments, "text");
    const std::string keyName = Mcp::stringArgument(arguments, "key");
    const std::string targetPath = Mcp::stringArgument(arguments, "target");
    if (text.empty() && keyName.empty()) { return Mcp::ready(Mcp::ToolResult::error("Pass `key`, `text` or both")); }
    std::vector<InputStep> steps;
    for (uint32_t codepoint : codepointsOf(text)) {
        InputStep step;
        step.kind = InputStep::Kind::Text;
        step.codepoint = codepoint;
        steps.push_back(step);
    }
    if (!keyName.empty()) {
        const PandaUI::Key key = keyFromName(keyName);
        if (key == PandaUI::Key::Unknown) { return Mcp::ready(Mcp::ToolResult::error("Unknown key: " + keyName)); }
        InputStep step;
        step.key = key;
        step.shift = Mcp::boolArgument(arguments, "shift", false);
        step.alt = Mcp::boolArgument(arguments, "alt", false);
        step.control = Mcp::boolArgument(arguments, "ctrl", false);
        step.super = Mcp::boolArgument(arguments, "super", false);
        step.kind = InputStep::Kind::KeyDown;
        steps.push_back(step);
        step.kind = InputStep::Kind::KeyUp;
        steps.push_back(step);
    }
    steps.push_back(InputStep{});
    steps.push_back(InputStep{});
    if (targetPath.empty()) { return playInput(std::move(steps)); }

    // The element takes the keyboard by a click, as from the user; then the keys.
    std::vector<Target> targets(1);
    targets.front().path = targetPath;
    const double wait = std::clamp(Mcp::numberArgument(arguments, "wait", TARGET_WAIT_SECONDS), 0.0, 30.0);
    return playAtTargets(std::move(targets), wait, [steps = std::move(steps)](const std::vector<PandaUI::Point> &points) {
        Gesture click;
        click.at = points.front();
        std::vector<InputStep> all = gestureSteps(click);
        all.insert(all.end(), steps.begin(), steps.end());
        return all;
    });
}

Mcp::PendingResult EditorMcp::ui(const Rain::Value &arguments) {
    struct Query {
        std::string path;
        std::string window;
        int depth = -1;
        double of = SCREENSHOT_DEFAULT_WIDTH;
        bool gone = false;
        int count = -1;
        bool hasValue = false;
        std::string value;
        bool hasLabel = false;
        std::string label;
        double deadline = 0.0;
    };
    auto query = std::make_shared<Query>();
    query->path = Mcp::stringArgument(arguments, "path");
    query->window = Mcp::stringArgument(arguments, "window");
    query->depth = static_cast<int>(Mcp::numberArgument(arguments, "depth", -1.0));
    query->of = Mcp::numberArgument(arguments, "of", SCREENSHOT_DEFAULT_WIDTH);
    query->gone = Mcp::boolArgument(arguments, "gone", false);
    query->count = static_cast<int>(Mcp::numberArgument(arguments, "count", -1.0));
    if (arguments.isObject() && arguments.find("value") != nullptr && arguments.find("value")->isString()) {
        query->hasValue = true;
        query->value = arguments.find("value")->getString();
    }
    if (arguments.isObject() && arguments.find("label") != nullptr && arguments.find("label")->isString()) {
        query->hasLabel = true;
        query->label = arguments.find("label")->getString();
    }
    if (query->window != "" && query->window != "main" && query->window != "settings" && query->window != "export") {
        return Mcp::ready(Mcp::ToolResult::error("window is \"main\", \"settings\" or \"export\""));
    }
    if (query->path.empty() && (query->gone || query->count >= 0 || query->hasValue || query->hasLabel)) {
        return Mcp::ready(Mcp::ToolResult::error("`gone`, `count`, `value` and `label` are about what `path` names: pass `path`"));
    }
    query->deadline = nowSeconds() + std::clamp(Mcp::numberArgument(arguments, "wait", TARGET_WAIT_SECONDS), 0.0, 30.0);

    return [this, query]() -> std::optional<Mcp::ToolResult> {
        // The window the user has in front, unless the call names one.
        PandaUI::Context *context = &inputContext();
        Fern::Window *window = inputWindow();
        if (query->window == "main") {
            context = &m_editor.ui().context();
            window = m_editor.window();
        } else if (query->window == "settings" || query->window == "export") {
            context = m_editor.ui().auxiliaryContext();
            window = m_editor.ui().auxiliaryWindow();
            if (context == nullptr || window == nullptr) { return Mcp::ToolResult::error("That window is not open"); }
        }
        if (window == nullptr) { return Mcp::ToolResult::error("The editor has no window"); }

        const std::vector<AccessibilityElement> tree = PandaUI::accessibilityTree(*context);
        std::vector<const AccessibilityElement *> answer;
        std::string why;
        bool done = false;
        if (query->path.empty()) {
            for (const AccessibilityElement &element : tree) { answer.push_back(&element); }
            done = true;
        } else {
            const PandaUI::AccessibilityMatch match = PandaUI::findAccessibilityElements(tree, query->path);
            if (match.segments.empty()) { return Mcp::ToolResult::error("The path is empty"); }
            const size_t found = match.elements.size();
            if (query->gone) {
                done = found == 0;
                why = "`" + query->path + "` is still there";
            } else if (query->count >= 0 && found != static_cast<size_t>(query->count)) {
                why = "`" + query->path + "` names " + std::to_string(found) + " element(s), not " + std::to_string(query->count);
                // A row a tree or a collection has not made may be one of the wanted ones.
                if (found == 0) { why += ". " + missingText(tree, match); }
            } else if (found == 0 && query->count < 0) {
                why = missingText(tree, match);
            } else if (query->hasValue || query->hasLabel) {
                // What an element holds or shows is asked about one element.
                if (found != 1) {
                    why = found == 0 ? missingText(tree, match) : ambiguousText(query->path, match);
                } else {
                    const AccessibilityElement &element = *match.elements.front();
                    done = true;
                    if (query->hasValue && element.value != query->value) {
                        done = false;
                        why = "The value of `" + query->path + "` is \"" + element.value + "\", not \"" + query->value + "\"";
                    } else if (query->hasLabel && element.label != query->label) {
                        done = false;
                        why = "The label of `" + query->path + "` is \"" + element.label + "\", not \"" + query->label + "\"";
                    }
                }
            } else {
                done = true;
            }
            if (found == 0 && !query->gone && query->count != 0) {
                // A row a tree or a collection has not made: asked for, it is there after the next update.
                const std::string &name = match.segments[match.resolved];
                if (match.resolved == 0) {
                    for (PandaUI::View *layer : context->layers()) { revealIn(*layer, name); }
                } else {
                    for (const AccessibilityElement *element : match.scope) { revealIn(*element->view, name); }
                }
            }
            if (!query->gone) { answer = match.elements; }
        }
        if (!done) {
            if (nowSeconds() < query->deadline) { return std::nullopt; }
            return Mcp::ToolResult::error(why);
        }
        const double scale = pointsPerScreenshotPixel(*window, query->of);
        Rain::Value elements = Rain::Value::array();
        for (const AccessibilityElement *element : answer) { elements.push(elementValue(*element, scale, query->depth)); }
        Rain::Value result = Rain::Value::object();
        result.add("elements", std::move(elements));
        return textResult(Rain::writeJson(result));
    };
}

Mcp::PendingResult EditorMcp::build(const Rain::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    const std::string target = Mcp::stringArgument(arguments, "target", "scripts");
    const std::string action = Mcp::stringArgument(arguments, "action", "build");
    if (target != "scripts" && target != "export") {
        return Mcp::ready(Mcp::ToolResult::error("target is \"scripts\" or \"export\""));
    }
    if (action != "build" && action != "configure" && action != "clean") {
        return Mcp::ready(Mcp::ToolResult::error("action is \"build\", \"configure\" or \"clean\""));
    }
    ProjectBuilds &builds = m_editor.builds();
    if (builds.isRunning()) { return Mcp::ready(Mcp::ToolResult::error("A build is running: wait for it, or stop it (the command build.stop)")); }
    const size_t limit = static_cast<size_t>(std::clamp(Mcp::numberArgument(arguments, "lines", 80.0), 1.0, 2000.0));
    builds.start(
        target == "export" ? BuildTargetKind::Export : BuildTargetKind::Scripts,
        action == "configure" ? BuildAction::Configure : action == "clean" ? BuildAction::Clean : BuildAction::Build
    );
    if (builds.lastResult() == ProjectBuilds::Result::None) {
        return Mcp::ready(Mcp::ToolResult::error("The build did not start"));
    }
    return [this, limit]() -> std::optional<Mcp::ToolResult> {
        ProjectBuilds &builds = m_editor.builds();
        if (builds.lastResult() == ProjectBuilds::Result::Running) { return std::nullopt; }
        const std::vector<std::string> &output = builds.lastOutput();
        std::string text = builds.lastResult() == ProjectBuilds::Result::Succeeded  ? "Finished."
                           : builds.lastResult() == ProjectBuilds::Result::Canceled ? "Canceled."
                                                                                    : "Failed.";
        const size_t first = output.size() > limit ? output.size() - limit : 0;
        if (first > 0) { text += " The last " + std::to_string(limit) + " of " + std::to_string(output.size()) + " lines:"; }
        for (size_t i = first; i < output.size(); ++i) { text += "\n" + output[i]; }
        if (builds.lastResult() == ProjectBuilds::Result::Succeeded) { return textResult(std::move(text)); }
        return Mcp::ToolResult::error(std::move(text));
    };
}

Mcp::PendingResult EditorMcp::command(const Rain::Value &arguments) {
    Editor::Commands &commands = m_editor.commands();
    const std::string id = Mcp::stringArgument(arguments, "id");
    if (id.empty()) {
        Rain::Value list = Rain::Value::array();
        for (const Editor::Commands::Command &entry : commands.all()) {
            Rain::Value keys = Rain::Value::array();
            for (const Editor::KeyChord &chord : entry.chords) { keys.push(chord.name()); }
            Rain::Value item = Rain::Value::object();
            item.add("id", entry.id);
            item.add("name", entry.group + ": " + entry.label);
            item.add("keys", std::move(keys));
            if (entry.scope == Editor::CommandScope::World) { item.add("where", "world view, hierarchy"); }
            if (entry.customized) { item.add("user", true); }
            list.push(std::move(item));
        }
        return Mcp::ready(textResult(Rain::writeJson(list)));
    }
    if (commands.find(id) == nullptr) { return Mcp::ready(Mcp::ToolResult::error("No such command: " + id)); }
    if (Mcp::boolArgument(arguments, "reset", false)) {
        commands.resetBinding(id);
        return Mcp::ready(textResult("The default keys: " + commands.keys(id)));
    }
    if (arguments.isObject() && arguments.find("keys") != nullptr && arguments.find("keys")->isArray()) {
        std::vector<Editor::KeyChord> chords;
        for (const auto &item : arguments.find("keys")->items()) {
            const Editor::KeyChord chord = item.isString() ? Editor::KeyChord::parse(item.getString()) : Editor::KeyChord();
            if (!chord.isSet()) { return Mcp::ready(Mcp::ToolResult::error("Not a key chord: write it as \"cmd-shift-p\", \"f5\"")); }
            chords.push_back(chord);
        }
        commands.bind(id, std::move(chords));
        return Mcp::ready(textResult("Bound and written to " + commands.keymapPath().string()));
    }
    SystemTools::Unattended unattended;
    if (!commands.run(id)) { return Mcp::ready(Mcp::ToolResult::error("The command does not apply now: nothing to act on")); }
    // What the command set off (a tab, a dialog) is laid out and drawn before the answer.
    return playInput({InputStep{}, InputStep{}});
}

Mcp::PendingResult EditorMcp::show(const Rain::Value &arguments) {
    if (!hasProject() || !m_editor.controller()) {
        return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor"));
    }
    Editor::EditorController &controller = *m_editor.controller();
    SystemTools::Unattended unattended;
    const std::string panel = Mcp::stringArgument(arguments, "panel");
    const std::string settings = Mcp::stringArgument(arguments, "settings");
    const std::string asset = Mcp::stringArgument(arguments, "asset");
    const std::string entity = Mcp::stringArgument(arguments, "entity");
    if (!panel.empty()) {
        if (!controller.showPanel(panel)) { return Mcp::ready(Mcp::ToolResult::error("No such panel: " + panel)); }
        return Mcp::ready(textResult("The panel is in front."));
    }
    if (!settings.empty()) {
        if (settings == "project") {
            controller.openSettings(SettingsScope::Project);
        } else if (settings == "editor") {
            controller.openSettings(SettingsScope::Editor);
        } else if (settings == "keymap") {
            controller.openKeymap();
        } else if (settings == "close") {
            controller.closeAuxiliaryWindow();
            return Mcp::ready(textResult("The second window is closed."));
        } else {
            return Mcp::ready(Mcp::ToolResult::error("settings is \"project\", \"editor\", \"keymap\" or \"close\""));
        }
        return Mcp::ready(textResult("The Settings window is open: shoot it with the `settings` view."));
    }
    if (!asset.empty()) {
        auto *handler = AssetHandlerEditor::current();
        path_t file(asset);
        if (file.is_relative()) { file = m_editor.project().getOpenedProjectPath() / file; }
        const AssetId id = handler != nullptr ? handler->getAssetId(file.lexically_normal()) : AssetId(0);
        if (!id) {
            return Mcp::ready(Mcp::ToolResult::error(
                "Not an asset of the project: " + file.string() + " (a new file becomes one after `reload`)"
            ));
        }
        controller.openAsset(id);
        return Mcp::ready(textResult("The asset is open in its tab."));
    }
    if (!entity.empty()) {
        Entity found = m_editor.shownWorld()->findByTag(entity.c_str());
        if (!found.isValid()) { return Mcp::ready(Mcp::ToolResult::error("No entity is named " + entity)); }
        controller.selectEntity(found.getId());
        return Mcp::ready(textResult("The entity is selected: the inspector of the world tab shows it."));
    }
    return Mcp::ready(Mcp::ToolResult::error("Say what to show: panel, settings, asset or entity"));
}

namespace {

    // The account of a cook as text: what the pack holds, what was left out.
    std::string cookReportText(const CookReport &report, size_t limit) {
        std::string text = "The game starts in " + report.startWorld + ".\nThe pack: " + report.pack + ", " +
                           CookReport::sizeText(report.packBytes) + " in " + std::to_string(report.packFiles) +
                           " files.\n\nBy kind (assets of the project the game uses, what they take):\n";
        char line[512];
        for (const CookReportKind &kind : report.kinds) {
            std::snprintf(
                line, sizeof(line), "  %-12s %5u  %10s\n", kind.kind.c_str(), kind.assets,
                CookReport::sizeText(kind.bytes).c_str()
            );
            text += line;
        }
        text += "\nThe largest files:\n";
        for (size_t i = 0; i < report.files.size() && i < limit; i++) {
            const CookReportFile &file = report.files[i];
            std::snprintf(
                line, sizeof(line), "  %10s  %s%s%s\n", CookReport::sizeText(file.bytes).c_str(), file.path.c_str(),
                file.detail.empty() ? "" : "  — ", file.detail.c_str()
            );
            text += line;
        }
        if (report.unused.empty()) {
            text += "\nEvery asset of the project is in the game.\n";
        } else {
            text += "\nNot reached from the start world and left out of the game: " + std::to_string(report.unused.size()) +
                    " asset(s), " + CookReport::sizeText(report.unusedBytes) + " of source files:\n";
            for (size_t i = 0; i < report.unused.size() && i < limit; i++) {
                std::snprintf(
                    line, sizeof(line), "  %10s  %-10s %s\n", CookReport::sizeText(report.unused[i].bytes).c_str(),
                    report.unused[i].kind.c_str(), report.unused[i].path.c_str()
                );
                text += line;
            }
            if (report.unused.size() > limit) {
                text += "  … and " + std::to_string(report.unused.size() - limit) + " more (report.json has them all)\n";
            }
        }
        if (!report.warnings.empty()) {
            text += "\nWarnings:\n";
            for (const std::string &warning : report.warnings) { text += "  " + warning + "\n"; }
        }
        return text;
    }

} // namespace

Mcp::PendingResult EditorMcp::exportGame(const Rain::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    const std::string action = Mcp::stringArgument(arguments, "action");
    if (action == "run") { return runBuiltGame(arguments); }
    const size_t limit = static_cast<size_t>(std::clamp(Mcp::numberArgument(arguments, "limit", 15.0), 1.0, 5000.0));
    ProjectBuilds &builds = m_editor.builds();
    CookReport report;
    if (action.empty() || action == "cook") {
        const uint64_t from = LogHistory::nextSequence();
        SystemTools::Unattended unattended;
        if (!builds.cook() || !builds.readCookReport(report)) {
            return Mcp::ready(Mcp::ToolResult::error("The game was not cooked.\n" + logSince(from)));
        }
        return Mcp::ready(textResult(cookReportText(report, limit)));
    }
    if (!builds.readCookReport(report)) {
        return Mcp::ready(Mcp::ToolResult::error("The project has no cook yet: call `export` with action `cook`."));
    }
    if (action == "report") { return Mcp::ready(textResult(cookReportText(report, limit))); }
    if (action == "files") {
        const std::string filter = Mcp::stringArgument(arguments, "filter");
        std::string text;
        size_t count = 0;
        uint64_t bytes = 0;
        char line[512];
        for (const CookReportFile &file : report.files) {
            if (!filter.empty() && file.path.find(filter) == std::string::npos) { continue; }
            std::snprintf(
                line, sizeof(line), "%10s  %-10s %s%s%s\n", CookReport::sizeText(file.bytes).c_str(), file.kind.c_str(),
                file.path.c_str(), file.detail.empty() ? "" : "  — ", file.detail.c_str()
            );
            text += line;
            count++;
            bytes += file.bytes;
        }
        text += std::to_string(count) + " file(s), " + CookReport::sizeText(bytes) + "\n";
        return Mcp::ready(textResult(std::move(text)));
    }
    return Mcp::ready(Mcp::ToolResult::error("action is cook, report, files or run"));
}

namespace {

    // A check run in flight: the game runs on a thread of its own, the tool asks every frame.
    struct GameRun final {
        std::atomic<bool> done{false};
        bool succeeded = false; // the game left by itself with exit code 0
        std::mutex mutex;
        std::vector<std::string> lines;
        path_t shotFile;
    };

    // RGBA8 rows → a PNG of at most maxWidth (area averaging), as `screenshot` packs a viewport.
    std::vector<uint8_t> packPng(const CheckShot &shot, uint64_t maxWidth) {
        const uint64_t width = shot.width;
        const uint64_t height = shot.height;
        const uint64_t outWidth = std::min<uint64_t>(width, maxWidth);
        const uint64_t outHeight = std::max<uint64_t>(1, (height * outWidth + width / 2) / width);
        std::vector<uint8_t> rgb(outWidth * outHeight * 3);
        for (uint64_t y = 0; y < outHeight; ++y) {
            const uint64_t y0 = y * height / outHeight;
            const uint64_t y1 = std::max(y0 + 1, (y + 1) * height / outHeight);
            for (uint64_t x = 0; x < outWidth; ++x) {
                const uint64_t x0 = x * width / outWidth;
                const uint64_t x1 = std::max(x0 + 1, (x + 1) * width / outWidth);
                uint64_t sum[3] = {0, 0, 0};
                for (uint64_t sy = y0; sy < y1; ++sy) {
                    const uint8_t *row = shot.pixels.data() + (shot.bottomUp ? height - 1 - sy : sy) * width * 4;
                    for (uint64_t sx = x0; sx < x1; ++sx) {
                        sum[0] += row[sx * 4];
                        sum[1] += row[sx * 4 + 1];
                        sum[2] += row[sx * 4 + 2];
                    }
                }
                const uint64_t count = (y1 - y0) * (x1 - x0);
                uint8_t *out = rgb.data() + (y * outWidth + x) * 3;
                out[0] = static_cast<uint8_t>((sum[0] + count / 2) / count);
                out[1] = static_cast<uint8_t>((sum[1] + count / 2) / count);
                out[2] = static_cast<uint8_t>((sum[2] + count / 2) / count);
            }
        }
        size_t pngSize = 0;
        void *png = tdefl_write_image_to_png_file_in_memory_ex(
            rgb.data(), static_cast<int>(outWidth), static_cast<int>(outHeight), 3, &pngSize, 6, MZ_FALSE
        );
        std::vector<uint8_t> packed;
        if (png != nullptr) {
            packed.assign(static_cast<const uint8_t *>(png), static_cast<const uint8_t *>(png) + pngSize);
            mz_free(png);
        }
        return packed;
    }

} // namespace

namespace {

    bool fernKeyByName(const std::string &name, Fern::Key &key) {
        std::string n;
        for (char c : name) { n += static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
        if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') {
            key = static_cast<Fern::Key>(static_cast<int>(Fern::Key::KEY_A) + (n[0] - 'A'));
            return true;
        }
        if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') {
            key = static_cast<Fern::Key>(static_cast<int>(Fern::Key::KEY_0) + (n[0] - '0'));
            return true;
        }
        static const std::pair<const char *, Fern::Key> named[] = {
            {"SPACE", Fern::Key::KEY_SPACE},   {"SHIFT", Fern::Key::KEY_LEFT_SHIFT}, {"ESCAPE", Fern::Key::KEY_ESCAPE},
            {"TAB", Fern::Key::KEY_TAB},       {"ENTER", Fern::Key::KEY_ENTER},      {"UP", Fern::Key::KEY_UP},
            {"DOWN", Fern::Key::KEY_DOWN},     {"LEFT", Fern::Key::KEY_LEFT},        {"RIGHT", Fern::Key::KEY_RIGHT},
            {"BACKSPACE", Fern::Key::KEY_BACKSPACE}, {"CONTROL", Fern::Key::KEY_LEFT_CONTROL}, {"ALT", Fern::Key::KEY_LEFT_ALT},
        };
        for (const auto &[text, value] : named) {
            if (n == text) {
                key = value;
                return true;
            }
        }
        return false;
    }

    bool pointArgument(const Rain::Value &value, glm::vec2 &out) {
        if (!value.isArray() || value.size() != 2 || !value.items()[0].isNumber() || !value.items()[1].isNumber()) { return false; }
        out = glm::vec2(value.items()[0].getFloat(), value.items()[1].getFloat());
        return true;
    }

} // namespace

Mcp::PendingResult EditorMcp::gameInput(const Rain::Value &arguments) {
    if (!m_editor.isPlaying()) { return Mcp::ready(Mcp::ToolResult::error("The game is not playing: `play` first")); }
    struct Finger {
        glm::vec2 at{0.f}, to{0.f};
        int id = 0;
    };
    struct Gesture {
        std::vector<Fern::Key> keys;
        bool hasMouse = false;
        glm::vec2 mouse{0.f}, mouseTo{0.f}, last{0.f};
        bool hasButton = false;
        Fern::MouseButton button = Fern::MouseButton::LEFT;
        std::vector<Finger> fingers;
        double scroll = 0.0;
        double seconds = 0.15;
        double started = 0.0;
        int frame = 0;
        float scale = 1.f;
    };
    auto g = std::make_shared<Gesture>();
    g->seconds = std::clamp(Mcp::numberArgument(arguments, "seconds", 0.15), 0.02, 30.0);
    if (arguments.isObject() && arguments.find("keys") != nullptr && arguments.find("keys")->isArray()) {
        for (const auto &k : arguments.find("keys")->items()) {
            Fern::Key key;
            if (!k.isString() || !fernKeyByName(k.getString(), key)) {
                return Mcp::ready(Mcp::ToolResult::error(std::string("Unknown key: ") + (k.isString() ? k.getString() : std::string("?"))));
            }
            g->keys.push_back(key);
        }
    }
    const Rect frame = Input::getViewportFrame();
    const double of = Mcp::numberArgument(arguments, "of", 0.0);
    g->scale = of > 0.0 && frame.size.width > 0.f ? frame.size.width / static_cast<float>(of) : 1.f;
    if (arguments.isObject() && arguments.find("mouse") != nullptr) {
        if (!pointArgument((*arguments.find("mouse")), g->mouse)) { return Mcp::ready(Mcp::ToolResult::error("mouse is [x, y]")); }
        g->hasMouse = true;
        g->mouseTo = g->mouse;
        if (arguments.find("to") != nullptr && !pointArgument((*arguments.find("to")), g->mouseTo)) {
            return Mcp::ready(Mcp::ToolResult::error("to is [x, y]"));
        }
    }
    g->scroll = Mcp::numberArgument(arguments, "scroll", 0.0);
    const std::string button = Mcp::stringArgument(arguments, "button");
    if (!button.empty()) {
        g->hasButton = true;
        g->button = button == "right" ? Fern::MouseButton::RIGHT : button == "middle" ? Fern::MouseButton::MIDDLE : Fern::MouseButton::LEFT;
    }
    if (arguments.isObject() && arguments.find("touches") != nullptr && arguments.find("touches")->isArray()) {
        int id = 7000;
        for (const auto &t : arguments.find("touches")->items()) {
            Finger f;
            if (!t.isObject() || t.find("at") == nullptr || !pointArgument((*t.find("at")), f.at)) {
                return Mcp::ready(Mcp::ToolResult::error("a touch is {\"at\": [x, y], \"to\": [x, y]}"));
            }
            f.to = f.at;
            if (t.find("to") != nullptr && !pointArgument((*t.find("to")), f.to)) { return Mcp::ready(Mcp::ToolResult::error("to of a touch is [x, y]")); }
            f.id = id++;
            g->fingers.push_back(f);
        }
    }
    m_editor.setGameDriven(true);
    return [this, g]() -> std::optional<Mcp::ToolResult> {
        // The input goes the way the events of the window go (Input::onEvent), before the frame of
        // the game: its update sees it this frame.
        Fern::Window *window = m_editor.window();
        const auto post = [this](Fern::Event &&event) { m_editor.postGameEvent(event); };
        const Rect frame = Input::getViewportFrame();
        const glm::vec2 origin(frame.origin.x, frame.origin.y);
        const double now = nowSeconds();
        if (g->frame == 0) { g->started = now; }
        const float t = static_cast<float>(std::clamp((now - g->started) / g->seconds, 0.0, 1.0));
        const bool last = g->frame > 1 && t >= 1.f;
        if (g->hasMouse) {
            const glm::vec2 p = (g->mouse + (g->mouseTo - g->mouse) * t) * g->scale;
            const glm::vec2 delta = g->frame == 0 ? glm::vec2(0.f) : p - g->last;
            post(Fern::MouseMovedEvent(origin.x + p.x, origin.y + p.y, delta.x, delta.y, window));
            g->last = p;
        }
        if (g->frame == 0) {
            for (Fern::Key key : g->keys) { post(Fern::KeyPressedEvent(key, window)); }
            if (g->scroll != 0.0) { post(Fern::MouseScrolledEvent(0.0, g->scroll, false, false, true, window)); }
            if (g->hasButton) {
                Fern::MouseKeyEvent press(g->button, true, window);
                press.hasPosition = g->hasMouse;
                press.x = origin.x + g->last.x;
                press.y = origin.y + g->last.y;
                post(std::move(press));
            }
            for (const Finger &f : g->fingers) {
                post(Fern::TouchBeganEvent(f.id, origin.x + f.at.x * g->scale, origin.y + f.at.y * g->scale, window));
            }
        } else {
            for (const Finger &f : g->fingers) {
                const glm::vec2 p = (f.at + (f.to - f.at) * t) * g->scale;
                post(Fern::TouchMovedEvent(f.id, origin.x + p.x, origin.y + p.y, window));
            }
        }
        if (last) {
            for (Fern::Key key : g->keys) { post(Fern::KeyReleasedEvent(key, window)); }
            if (g->hasButton) {
                Fern::MouseKeyEvent release(g->button, false, window);
                release.hasPosition = g->hasMouse;
                release.x = origin.x + g->last.x;
                release.y = origin.y + g->last.y;
                post(std::move(release));
            }
            for (const Finger &f : g->fingers) { post(Fern::TouchEndedEvent(f.id, window)); }
        }
        g->frame++;
        if (!last) { return std::nullopt; }
        // The releases are seen by the next frame; the game keeps the input for it.
        return textResult("Done in " + std::to_string(g->frame) + " frames.");
    };
}

Mcp::PendingResult EditorMcp::runBuiltGame(const Rain::Value &arguments) {
    const path_t game = m_editor.builds().builtGame().executable;
    if (game.empty()) {
        return Mcp::ready(Mcp::ToolResult::error(
            "The game is not built for this machine: `export` (cook), then `build` with target `export`. An export "
            "target of another platform (iOS) does not run here."
        ));
    }
    const int frames = static_cast<int>(std::clamp(Mcp::numberArgument(arguments, "frames", 120.0), 1.0, 100000.0));
    const size_t limit = static_cast<size_t>(std::clamp(Mcp::numberArgument(arguments, "limit", 40.0), 1.0, 5000.0));
    const uint64_t maxWidth = static_cast<uint64_t>(std::clamp(Mcp::numberArgument(arguments, "width", 1280.0), 16.0, 8192.0));
    auto run = std::make_shared<GameRun>();
    std::error_code error;
    run->shotFile = std::filesystem::temp_directory_path(error) / ("panda-check-" + std::to_string(SystemTools::currentProcessId()) + ".shot");
    std::filesystem::remove(run->shotFile, error);

    // The check run is asked for by the environment of the game (Panda/Runtime/CheckRun.hpp).
    const std::string framesText = std::to_string(frames);
    const std::string shotText = run->shotFile.string();
    std::thread([run, game, framesText, shotText] {
        const auto collect = [&run](std::string_view line) {
            std::lock_guard<std::mutex> lock(run->mutex);
            run->lines.emplace_back(line);
        };
#if defined(PLATFORM_WINDOWS)
        // The child inherits the environment of the editor: set for the time of the run.
        _putenv_s(CheckRun::FRAMES_VARIABLE, framesText.c_str());
        _putenv_s(CheckRun::SHOT_VARIABLE, shotText.c_str());
        run->succeeded = SystemTools::runCommand(game, {}, game.parent_path(), collect);
        _putenv_s(CheckRun::FRAMES_VARIABLE, "");
        _putenv_s(CheckRun::SHOT_VARIABLE, "");
#else
        run->succeeded = SystemTools::runCommand(
            "/usr/bin/env",
            {std::string(CheckRun::FRAMES_VARIABLE) + "=" + framesText, std::string(CheckRun::SHOT_VARIABLE) + "=" + shotText,
             game.string()},
            game.parent_path(), collect
        );
#endif
        run->done = true;
    }).detach();

    return [run, limit, maxWidth, frames]() -> std::optional<Mcp::ToolResult> {
        if (!run->done) { return std::nullopt; }
        Mcp::ToolResult result;
        result.isError = !run->succeeded;
        result.text = run->succeeded ? "The game played " + std::to_string(frames) + " frames and left by itself."
                                     : "The game did not end well (a crash, its data did not load, or it did not leave in time).";
        CheckShot shot;
        std::error_code error;
        if (shot.read(run->shotFile)) {
            result.png = packPng(shot, maxWidth);
            result.text += " Its last frame is the picture (" + std::to_string(shot.width) + "×" + std::to_string(shot.height) + ").";
        } else {
            result.text += " It left no picture of its last frame.";
        }
        std::filesystem::remove(run->shotFile, error);
        std::lock_guard<std::mutex> lock(run->mutex);
        const size_t first = run->lines.size() > limit ? run->lines.size() - limit : 0;
        result.text += first > 0 ? "\nThe last " + std::to_string(limit) + " of " + std::to_string(run->lines.size()) + " lines of its log:"
                                 : "\nIts log:";
        for (size_t i = first; i < run->lines.size(); ++i) { result.text += "\n" + run->lines[i]; }
        return result;
    };
}

Mcp::PendingResult EditorMcp::openProject(const Rain::Value &arguments) {
    const path_t folder = path_t(Mcp::stringArgument(arguments, "path")).lexically_normal();
    std::error_code error;
    if (folder.empty() || !folder.is_absolute() || !std::filesystem::is_directory(folder, error)) {
        return Mcp::ready(Mcp::ToolResult::error("path is the folder of a project, absolute: " + folder.string()));
    }
    std::string problem;
    const std::string name = ProjectLoader::readName(folder, &problem);
    if (name.empty()) { return Mcp::ready(Mcp::ToolResult::error(problem)); }
    if (hasProject() && folder == m_editor.project().getOpenedProjectPath()) {
        return Mcp::ready(textResult("This project is open already."));
    }
    const std::vector<std::string> dirty = dirtyDocuments();
    if (!dirty.empty() && !Mcp::boolArgument(arguments, "discard", false)) {
        std::string text = "Unsaved documents: ";
        for (size_t i = 0; i < dirty.size(); ++i) { text += (i > 0 ? ", " : "") + dirty[i]; }
        text += ". Call `save` first, or pass discard=true to lose their changes.";
        return Mcp::ready(Mcp::ToolResult::error(std::move(text)));
    }
    // After the answer has left (performExit): the server may move to the port of the other project.
    m_projectToOpen = folder;
    m_exit = Exit::OpenProject;
    m_exitFrames = 0;
    return Mcp::ready(textResult("The editor opens " + name + ": poll `status`."));
}

Mcp::PendingResult EditorMcp::exit(Exit kind, const Rain::Value &arguments) {
    const std::vector<std::string> dirty = dirtyDocuments();
    if (!dirty.empty() && !Mcp::boolArgument(arguments, "discard", false)) {
        std::string text = "Unsaved documents: ";
        for (size_t i = 0; i < dirty.size(); ++i) { text += (i > 0 ? ", " : "") + dirty[i]; }
        text += ". Call `save` first, or pass discard=true to lose their changes.";
        return Mcp::ready(Mcp::ToolResult::error(std::move(text)));
    }
    // After the answer has left (performExit): the process that answers is the one that exits.
    m_exit = kind;
    m_exitFrames = 0;
    return Mcp::ready(textResult(
        kind == Exit::Restart ? "The editor restarts with the same project: it exits now and answers again in a few seconds."
                              : "The editor closes."
    ));
}

} // namespace Panda
