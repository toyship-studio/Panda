#include "App/EditorMcp.hpp"

#include "App/EditorLayer.hpp"
#include "App/SystemStats.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/EditorController.hpp"

#include <Fern/Window/Window.hpp>
#include <Foundation/GraphicsAPI.hpp>
#include <Foundation/Logger.hpp>
#include <Miren/Miren.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Base/Version.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>
#include <Panda/Renderer/ShaderFeatures.hpp>
#include <miniz.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <typeinfo>

#if defined(__GNUC__) || defined(__clang__)
#    include <cxxabi.h>
#endif

namespace Panda {

namespace {

    using Foundation::LogHistory;
    using Foundation::LogRecord;
    using LogLevel = Foundation::Logger::MessageType;
    using JsonWriter = rapidjson::Writer<rapidjson::StringBuffer>;
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

    void writeText(JsonWriter &writer, const std::string &text) {
        writer.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
    }

    void writeVector(JsonWriter &writer, glm::vec3 value) {
        writer.StartArray();
        writer.Double(value.x);
        writer.Double(value.y);
        writer.Double(value.z);
        writer.EndArray();
    }

    std::string takeText(rapidjson::StringBuffer &buffer) {
        return std::string(buffer.GetString(), buffer.GetSize());
    }

    Mcp::ToolResult textResult(std::string text) {
        Mcp::ToolResult result;
        result.text = std::move(text);
        return result;
    }

    const char *viewName(ViewportKind view) {
        return view == ViewportKind::Scene ? "scene" : "game";
    }

} // namespace

// A screenshot in flight: the tab of the viewport comes forward, the viewport renders a few
// frames, its output texture is read back from the GPU (two more frames), then the picture is
// scaled and packed.
struct EditorMcp::Screenshot {
    bool window = false; // the whole window of the editor, not a viewport
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
    enum class Kind { Move, Down, Up, Scroll, KeyDown, KeyUp, Text, Idle };
    Kind kind = Kind::Idle;
    PandaUI::Point position;                                    // Move, Down, Up, Scroll (window points)
    PandaUI::Point delta;                                       // Scroll
    PandaUI::PointerButton button = PandaUI::PointerButton::Left; // Down, Up
    PandaUI::Key key = PandaUI::Key::Unknown;                   // KeyDown, KeyUp
    uint32_t codepoint = 0;                                     // Text
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;
    bool newClick = false; // Down: not the second click of a double click
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
    if (m_editor.m_launch.mcpPort > 0 && m_editor.m_launch.mcpPort < 65536) {
        // Asked for on the command line: the server is there whatever is open (the start screen
        // too) — work on the editor itself.
        port = m_editor.m_launch.mcpPort;
    } else if (hasProject()) {
        const ProjectMcpSettings &settings = m_editor.m_documents.projectSettings().get<ProjectMcpSettings>();
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
    } else {
        m_editor.closeAppRequest();
    }
}

bool EditorMcp::hasProject() const {
    return m_editor.m_loader.hasOpenedProject();
}

std::vector<std::string> EditorMcp::dirtyDocuments() const {
    std::vector<std::string> titles;
    const FileDocumentList files = m_editor.m_documents.list();
    for (FileDocument *file : files.all) {
        if (file->isDirty()) { titles.push_back(file->title()); }
    }
    return titles;
}

std::string EditorMcp::cameraJson() const {
    CameraController &camera = m_editor.m_viewports.cameraController();
    rapidjson::StringBuffer buffer;
    JsonWriter writer(buffer);
    writer.SetMaxDecimalPlaces(3);
    writer.StartObject();
    writer.Key("position");
    writeVector(writer, camera.getPosition());
    writer.Key("forward");
    writeVector(writer, camera.getForward());
    writer.EndObject();
    return takeText(buffer);
}

void EditorMcp::registerTools() {
    m_server.addTool({
        "status",
        "The state of the editor: the project, Edit or Play, the open worlds, the unsaved documents, the viewports, "
        "the editor camera, the frame statistics, the shader features of the project, the number of errors in the "
        "log. Call it first.",
        {},
        [this](const rapidjson::Value &) { return status(); },
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
        [this](const rapidjson::Value &arguments) { return logs(arguments); },
    });
    m_server.addTool({
        "screenshot",
        "A picture of a viewport as the editor shows it, or of the whole editor window. `scene`: the world "
        "through the editor camera, with the grid, the selection outline and the helper icons (place the camera "
        "with `camera`). `game`: the frame of the game through the main camera of the world, with the game UI — "
        "the running game in Play, a still frame in Edit. The tab of the viewport comes forward for the shot. "
        "`window`: the editor itself — its panels, tabs and dialogs with the viewport inside (the menu bar and the "
        "title of the system are not part of it). Takes a few frames.",
        R"({"type":"object","properties":{)"
        R"("view":{"type":"string","enum":["scene","game","window"],"description":"Default: game while playing, scene while editing."},)"
        R"("width":{"type":"integer","description":"The picture is scaled down to this width in pixels when the viewport is wider. Default 1280."},)"
        R"("wait":{"type":"number","description":"Seconds to wait before the shot: assets still loading, a moment of the game to reach. Default 0, at most 30."},)"
        R"("file":{"type":"string","description":"Also write the PNG to this file: an absolute path, or one relative to the project folder."},)"
        R"("image":{"type":"boolean","description":"Return the picture in the answer. Default true; false when the file is enough."}}})",
        [this](const rapidjson::Value &arguments) { return screenshot(arguments); },
    });
    m_server.addTool({
        "reload",
        "Makes the editor take what changed on disk — worlds, assets, shaders, the settings, the script build — "
        "as the Reload button does. Call it after editing files of the project. Stops the game if it plays. A "
        "document with unsaved edits in the editor that was also changed on disk is a conflict: the tool fails "
        "and names it. Answers with what the editor logged.",
        R"({"type":"object","properties":{)"
        R"("discard":{"type":"boolean","description":"The disk wins: every unsaved edit made in the editor is dropped and its document is read again from its file, changed on disk or not. Default false."}}})",
        [this](const rapidjson::Value &arguments) { return reload(arguments); },
    });
    m_server.addTool({
        "save",
        "Saves every unsaved document of the editor (Save All): what the user changed in the editor reaches the "
        "files. A file changed on disk since the editor read it is a conflict: the tool fails and names it.",
        R"({"type":"object","properties":{)"
        R"("overwrite":{"type":"boolean","description":"In a conflict the version of the editor wins and the file on disk is overwritten. Default false."}}})",
        [this](const rapidjson::Value &arguments) { return save(arguments); },
    });
    m_server.addTool({
        "play",
        "Starts the game inside the editor (Play): the scripts and the physics run in a copy of the active world, "
        "the Game tab shows the frame. Answers with what the start logged.",
        {},
        [this](const rapidjson::Value &) { return play(); },
    });
    m_server.addTool({
        "stop",
        "Stops the game (Stop): the editor returns to the world being edited; what the game changed is gone.",
        {},
        [this](const rapidjson::Value &) { return stop(); },
    });
    m_server.addTool({
        "camera",
        "The editor camera of the Scene viewport. Without arguments answers where it is; `position` moves it, "
        "`target` turns it to look at a point. World space, meters, Y up. The camera of the game is an entity "
        "of the world: it is edited in the world file.",
        R"({"type":"object","properties":{)"
        R"("position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"[x, y, z] of the camera."},)"
        R"("target":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"[x, y, z] of the point to look at."}}})",
        [this](const rapidjson::Value &arguments) { return camera(arguments); },
    });
    m_server.addTool({
        "open_world",
        "Opens a world file of the project in a tab of the editor and makes it the active world (the one the "
        "viewports show and Play runs). Stops the game if it plays.",
        R"({"type":"object","properties":{)"
        R"("path":{"type":"string","description":"The .world file: relative to the project folder (Assets/world.world) or absolute."}},)"
        R"("required":["path"]})",
        [this](const rapidjson::Value &arguments) { return openWorld(arguments); },
    });
    m_server.addTool({
        "show",
        "Brings a part of the editor in front, as a click of the user would, so that a `screenshot` of the "
        "`window` shows it. One of: `panel` — a panel by its id; `settings` — a settings tab; `asset` — selects "
        "an asset: its tab opens (a material with its preview, a texture, a mesh); `entity` — selects an entity "
        "of the active world by its name: its inspector tab opens.",
        R"({"type":"object","properties":{)"
        R"("panel":{"type":"string","enum":["hierarchy","contentBrowser","output","cmake","statistics","profiler"]},)"
        R"("settings":{"type":"string","enum":["world","project","editor"]},)"
        R"("asset":{"type":"string","description":"The file of the asset, relative to the project folder (Assets/Materials/gold.mat)."},)"
        R"("entity":{"type":"string","description":"The name of the entity."}}})",
        [this](const rapidjson::Value &arguments) { return show(arguments); },
    });
    m_server.addTool({
        "pointer",
        "A gesture of the mouse in the editor window, made where the user would make it: for the editor itself as "
        "the thing to test — a button, a menu, a field, a drag. The coordinates are those of a `window` screenshot "
        "(of the default width; pass `of` when the screenshot was asked with another `width`). `click` presses and "
        "releases; `drag` presses at x, y, moves to toX, toY over a few frames and releases; `down`, `move`, `up` "
        "make a gesture in parts, with a screenshot in between; `scroll` turns the wheel by dx, dy. Answers with "
        "what is under the pointer afterwards: the views from the innermost out, the cursor, the view in focus. "
        "The viewport's own tools (the gizmo, the camera) read the real mouse and do not follow.",
        R"({"type":"object","properties":{)"
        R"("action":{"type":"string","enum":["click","doubleClick","rightClick","move","down","up","drag","scroll"],"description":"Default: click."},)"
        R"("x":{"type":"number"},"y":{"type":"number"},)"
        R"("toX":{"type":"number","description":"drag: where it ends."},"toY":{"type":"number"},)"
        R"("dx":{"type":"number","description":"scroll: the wheel, in lines; dy > 0 scrolls the content down."},"dy":{"type":"number"},)"
        R"("steps":{"type":"integer","description":"drag: the frames of the movement. Default 8."},)"
        R"("of":{"type":"number","description":"The width of the screenshot the coordinates were read from. Default 1280."},)"
        R"("shift":{"type":"boolean"},"alt":{"type":"boolean"},"ctrl":{"type":"boolean"},"super":{"type":"boolean","description":"Command on macOS."}},)"
        R"("required":["x","y"]})",
        [this](const rapidjson::Value &arguments) { return pointer(arguments); },
    });
    m_server.addTool({
        "key",
        "Keys and text into the editor window, to the view in focus (click a field with `pointer` first): `text` "
        "is typed character by character, `key` is pressed and released — a letter or a digit, or Enter, Escape, "
        "Tab, Backspace, Delete, Space, Up, Down, Left, Right, Home, End, PageUp, PageDown, F1..F12. With both, the "
        "text goes first. Answers like `pointer`.",
        R"({"type":"object","properties":{)"
        R"("key":{"type":"string"},"text":{"type":"string"},)"
        R"("shift":{"type":"boolean"},"alt":{"type":"boolean"},"ctrl":{"type":"boolean"},"super":{"type":"boolean","description":"Command on macOS."}}})",
        [this](const rapidjson::Value &arguments) { return key(arguments); },
    });
    m_server.addTool({
        "restart",
        "Restarts the editor with the same project: after the editor itself was rebuilt, or when the module set "
        "of project.json has changed. The answer comes first, then the editor exits and starts again; the server "
        "answers again in a few seconds (poll `status`). Fails when documents are unsaved.",
        R"({"type":"object","properties":{)"
        R"("discard":{"type":"boolean","description":"Restart although documents are unsaved: their changes are lost. Default false."}}})",
        [this](const rapidjson::Value &arguments) { return exit(Exit::Restart, arguments); },
    });
    m_server.addTool({
        "quit",
        "Closes the editor. Fails when documents are unsaved.",
        R"({"type":"object","properties":{)"
        R"("discard":{"type":"boolean","description":"Close although documents are unsaved: their changes are lost. Default false."}}})",
        [this](const rapidjson::Value &arguments) { return exit(Exit::Quit, arguments); },
    });
}

/* ------------------------------ status, logs ------------------------------ */

Mcp::PendingResult EditorMcp::status() {
    rapidjson::StringBuffer buffer;
    JsonWriter writer(buffer);
    writer.SetMaxDecimalPlaces(3);
    writer.StartObject();

    writer.Key("editor");
    writer.StartObject();
    writer.Key("version");
    writer.String(ENGINE_VERSION);
    if (ENGINE_BUILD_COMMIT[0] != '\0') {
        writer.Key("commit");
        writer.String(ENGINE_BUILD_COMMIT);
    }
    writer.Key("pid");
    writer.Uint(SystemTools::currentProcessId());
    writer.EndObject();

    if (!hasProject()) {
        writer.Key("project");
        writer.Null();
        writer.EndObject();
        return Mcp::ready(textResult(takeText(buffer)));
    }
    const path_t &projectPath = m_editor.m_loader.getOpenedProjectPath();
    writer.Key("project");
    writer.StartObject();
    writer.Key("name");
    writeText(writer, projectPath.filename().string());
    writer.Key("path");
    writeText(writer, projectPath.string());
    writer.EndObject();

    writer.Key("state");
    writer.String(m_editor.m_sceneState == SceneState::PLAY ? "play" : "edit");

    writer.Key("worlds");
    writer.StartArray();
    for (const std::unique_ptr<WorldFile> &world : m_editor.m_documents.worlds()) {
        writer.StartObject();
        writer.Key("file"); // relative to the project; empty — a new world that has no file yet
        std::error_code error;
        const path_t relative = world->path().empty() ? path_t() : std::filesystem::relative(world->path(), projectPath, error);
        writeText(writer, error ? world->path().string() : relative.generic_string());
        writer.Key("active");
        writer.Bool(world.get() == &m_editor.m_documents.activeWorld());
        writer.Key("unsaved");
        writer.Bool(world->isDirty());
        writer.Key("changedOnDisk");
        writer.Bool(world->isChangedOnDisk());
        writer.EndObject();
    }
    writer.EndArray();

    writer.Key("unsaved"); // every document with edits the files do not have: `save` writes them
    writer.StartArray();
    for (const std::string &title : dirtyDocuments()) { writeText(writer, title); }
    writer.EndArray();

    const EditorViewports &viewports = m_editor.m_viewports;
    writer.Key("viewports");
    writer.StartObject();
    writer.Key("rendering"); // one viewport renders per frame: the one whose tab is in front
    writer.String(viewName(viewports.live()));
    for (ViewportKind view : {ViewportKind::Scene, ViewportKind::Game}) {
        const ViewportSurface &surface = viewports.surface(view);
        writer.Key(viewName(view));
        writer.StartObject();
        writer.Key("width");
        writer.Uint(surface.pixelWidth());
        writer.Key("height");
        writer.Uint(surface.pixelHeight());
        writer.EndObject();
    }
    writer.EndObject();

    writer.Key("camera");
    const std::string camera = cameraJson();
    writer.RawValue(camera.data(), camera.size(), rapidjson::kObjectType);

    const Renderer3D::Statistics stats3d = m_editor.m_currentWorld->getRenderer3D().getStats();
    const Renderer2D::Statistics stats2d = m_editor.m_currentWorld->getRenderer2D().getStats();
    const Miren::GpuStats gpu = Miren::getGpuStats();
    const Editor::SystemStats system = Editor::sampleSystemStats();
    const double megabyte = 1024.0 * 1024.0;
    writer.Key("frame");
    writer.StartObject();
    writer.Key("fps");
    writer.Int(Application::get()->fps);
    writer.Key("gpuMs");
    writer.Double(gpu.frameMs);
    writer.Key("drawCalls");
    writer.Uint(stats3d.drawCalls);
    writer.Key("shadowDrawCalls");
    writer.Uint(stats3d.shadowDrawCalls);
    writer.Key("drawCalls2d");
    writer.Uint(stats2d.drawCalls);
    writer.Key("memoryMb");
    writer.Double(std::round(static_cast<double>(system.residentBytes) / megabyte));
    writer.Key("videoMemoryMb");
    writer.Double(std::round(static_cast<double>(gpu.allocatedBytes) / megabyte));
    writer.EndObject();

    writer.Key("shaderFeatures");
    writer.StartArray();
    for (const std::string &feature : m_editor.m_documents.projectSettings().get<ProjectRenderingSettings>().features) {
        writeText(writer, feature);
    }
    writer.EndArray();

    size_t errors = 0;
    LogHistory::read(0, LogLevel::_ERROR, 0, &errors);
    writer.Key("log");
    writer.StartObject();
    writer.Key("next");
    writer.Uint64(LogHistory::nextSequence());
    writer.Key("errors");
    writer.Uint64(errors);
    writer.EndObject();

    writer.EndObject();
    return Mcp::ready(textResult(takeText(buffer)));
}

Mcp::PendingResult EditorMcp::logs(const rapidjson::Value &arguments) {
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
    if (!m_editor.m_editorController) { return; }
    if (view == ViewportKind::Scene) {
        m_editor.m_editorController->showSceneView();
    } else {
        m_editor.m_editorController->openGameView();
    }
}

Mcp::PendingResult EditorMcp::screenshot(const rapidjson::Value &arguments) {
    auto shot = std::make_shared<Screenshot>();
    const std::string view = Mcp::stringArgument(arguments, "view");
    // The window is there without a project (the start screen); a viewport is not.
    if (!hasProject() && view != "window") {
        return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor: only the `window` view can be shot"));
    }
    if (view.empty()) {
        shot->view = m_editor.m_sceneState == SceneState::PLAY ? ViewportKind::Game : ViewportKind::Scene;
    } else if (view == "scene") {
        shot->view = ViewportKind::Scene;
    } else if (view == "game") {
        shot->view = ViewportKind::Game;
    } else if (view == "window") {
        shot->window = true;
    } else {
        return Mcp::ready(Mcp::ToolResult::error("view is \"scene\", \"game\" or \"window\""));
    }
    shot->maxWidth = static_cast<int>(std::clamp(Mcp::numberArgument(arguments, "width", SCREENSHOT_DEFAULT_WIDTH), 64.0, 8192.0));
    shot->returnImage = Mcp::boolArgument(arguments, "image", true);
    const std::string file = Mcp::stringArgument(arguments, "file");
    if (!file.empty()) {
        shot->file = path_t(file);
        if (shot->file.is_relative()) {
            if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("file is an absolute path when no project is open")); }
            shot->file = m_editor.m_loader.getOpenedProjectPath() / shot->file;
        }
        shot->file = shot->file.lexically_normal();
        if (shot->file.extension() != ".png") { return Mcp::ready(Mcp::ToolResult::error("file is a .png file")); }
    } else if (!shot->returnImage) {
        return Mcp::ready(Mcp::ToolResult::error("Nothing to answer with: image is false and there is no file"));
    }
    shot->notBefore = nowSeconds() + std::clamp(Mcp::numberArgument(arguments, "wait", 0.0), 0.0, 30.0);
    if (hasProject()) { shot->shownBefore = m_editor.m_viewports.live(); }
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
    const EditorViewports &viewports = m_editor.m_viewports;
    const ViewportSurface &surface = viewports.surface(shot.view);
    const bool live = viewports.live() == shot.view && surface.isReady() && surface.pixelWidth() > 0 && surface.pixelHeight() > 0;
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
                " viewport does not render. One viewport renders per frame: in Edit the Scene while its tab is "
                "visible, in Play the Game while its tab is visible — the other one cannot be shot while both "
                "tabs are on screen."
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
    const Fern::Size size = m_editor.m_window->getSize();
    const Fern::Size dpi = m_editor.m_window->getDpi();
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
    Miren::setViewport(Views::CAPTURE_VIEW, Miren::Rect(0, 0, width, height));
    Miren::setViewClear(Views::CAPTURE_VIEW, 0x000000ff);
    Miren::setViewFrameBuffer(Views::CAPTURE_VIEW, m_windowCaptureBuffer);
    m_editor.m_editorUI.renderTo(Views::CAPTURE_VIEW);

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

    rapidjson::StringBuffer buffer;
    JsonWriter writer(buffer);
    writer.StartObject();
    writer.Key("view");
    writer.String(shot.window ? "window" : viewName(shot.view));
    writer.Key("width");
    writer.Uint64(outWidth);
    writer.Key("height");
    writer.Uint64(outHeight);
    if (!shot.file.empty()) {
        writer.Key("file");
        writeText(writer, shot.file.string());
    }
    writer.EndObject();
    result.text = takeText(buffer);
    if (!shot.returnImage) { result.png.clear(); }
    return result;
}

/* ------------------------------ acting on the editor ------------------------------ */

Mcp::PendingResult EditorMcp::reload(const rapidjson::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    const bool discard = Mcp::boolArgument(arguments, "discard", false);
    const uint64_t from = LogHistory::nextSequence();
    // Don't Save of the question about a conflict is "take the version on disk".
    SystemTools::Unattended unattended(discard ? SaveChangesChoice::DontSave : SaveChangesChoice::Cancel);
    // With `discard` the disk wins everywhere: a document with unsaved edits is read again even
    // when its file has not changed.
    if (discard) { m_editor.revertUnsaved(); }
    m_editor.reloadFromDisk();
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

Mcp::PendingResult EditorMcp::save(const rapidjson::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    const bool overwrite = Mcp::boolArgument(arguments, "overwrite", false);
    const uint64_t from = LogHistory::nextSequence();
    SystemTools::Unattended unattended(overwrite ? SaveChangesChoice::Save : SaveChangesChoice::Cancel);
    const bool saved = m_editor.saveAll();
    Mcp::ToolResult result;
    if (!saved) {
        result.isError = true;
        result.text = "Not everything was saved";
        if (!unattended.questions().empty()) { result.text += ": " + unattended.questions().front(); }
        result.text += ". A file changed on disk needs `save` with overwrite=true (the version of the editor wins) "
                       "or `reload` with discard=true (the disk wins); a new world that has no file yet is named "
                       "by the user in the editor.\n";
    } else if (dirtyDocuments().empty()) {
        result.text = "Saved.\n";
    }
    result.text += logSince(from);
    return Mcp::ready(std::move(result));
}

Mcp::PendingResult EditorMcp::play() {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    if (m_editor.m_sceneState == SceneState::PLAY) { return Mcp::ready(textResult("The game is playing already.")); }
    const uint64_t from = LogHistory::nextSequence();
    SystemTools::Unattended unattended;
    m_editor.play();
    return Mcp::ready(textResult("Playing.\n" + logSince(from)));
}

Mcp::PendingResult EditorMcp::stop() {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    if (m_editor.m_sceneState == SceneState::EDIT) { return Mcp::ready(textResult("The game is not playing.")); }
    const uint64_t from = LogHistory::nextSequence();
    SystemTools::Unattended unattended;
    m_editor.stop();
    return Mcp::ready(textResult("Stopped.\n" + logSince(from)));
}

Mcp::PendingResult EditorMcp::camera(const rapidjson::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    CameraController &controller = m_editor.m_viewports.cameraController();
    double values[3];
    if (arguments.HasMember("position")) {
        if (!Mcp::numbersArgument(arguments, "position", values, 3)) {
            return Mcp::ready(Mcp::ToolResult::error("position is an array of three numbers: [x, y, z]"));
        }
        controller.setPosition(glm::vec3(values[0], values[1], values[2]));
    }
    if (arguments.HasMember("target")) {
        if (!Mcp::numbersArgument(arguments, "target", values, 3)) {
            return Mcp::ready(Mcp::ToolResult::error("target is an array of three numbers: [x, y, z]"));
        }
        controller.lookAt(glm::vec3(values[0], values[1], values[2]));
    }
    return Mcp::ready(textResult(cameraJson()));
}

Mcp::PendingResult EditorMcp::openWorld(const rapidjson::Value &arguments) {
    if (!hasProject()) { return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor")); }
    const std::string name = Mcp::stringArgument(arguments, "path");
    if (name.empty()) { return Mcp::ready(Mcp::ToolResult::error("path is the .world file to open")); }
    path_t file(name);
    if (file.is_relative()) { file = m_editor.m_loader.getOpenedProjectPath() / file; }
    file = file.lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        return Mcp::ready(Mcp::ToolResult::error("No such file: " + file.string()));
    }
    const uint64_t from = LogHistory::nextSequence();
    SystemTools::Unattended unattended;
    // The game plays a copy of the active world: it ends before another world takes that place.
    if (m_editor.m_sceneState != SceneState::EDIT) { m_editor.stop(); }
    if (m_editor.m_documents.openWorld(file) == nullptr) {
        return Mcp::ready(Mcp::ToolResult::error("The world could not be opened: " + file.string() + "\n" + logSince(from)));
    }
    if (m_editor.m_editorController) { m_editor.m_editorController->showSceneView(); }
    return Mcp::ready(textResult("Opened " + file.filename().string() + ".\n" + logSince(from)));
}

namespace {

    // The class of a view, for the answer of an input tool: "PandaUI::Toggle".
    std::string viewClassName(const PandaUI::View &view) {
        const char *raw = typeid(view).name();
#if defined(__GNUC__) || defined(__clang__)
        int status = 0;
        char *demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
        std::string name = status == 0 && demangled != nullptr ? demangled : raw;
        std::free(demangled);
        return name;
#else
        return raw;
#endif
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

std::string EditorMcp::pointerJson() const {
    PandaUI::Context &context = m_editor.m_editorUI.context();
    rapidjson::StringBuffer buffer;
    JsonWriter writer(buffer);
    writer.StartObject();
    // The views under the pointer, from the innermost out: what a click there reaches.
    writer.Key("under");
    writer.StartArray();
    int depth = 0;
    for (PandaUI::View *view = context.getHoveredView(); view != nullptr && depth < 6; view = view->getSuperview(), ++depth) {
        writeText(writer, viewClassName(*view));
    }
    writer.EndArray();
    writer.Key("cursor");
    writer.String(cursorName(context.getCurrentCursor()));
    writer.Key("focus");
    if (PandaUI::View *focused = context.getFocusedView()) {
        writeText(writer, viewClassName(*focused));
    } else {
        writer.Null();
    }
    writer.EndObject();
    return takeText(buffer);
}

Mcp::PendingResult EditorMcp::playInput(std::vector<InputStep> steps) {
    auto queue = std::make_shared<std::vector<InputStep>>(std::move(steps));
    auto next = std::make_shared<size_t>(0);
    // The real mouse steps aside for the gesture and comes back when the call is over — however
    // it ends: a client that went away mid-gesture drops the pending answer, and the guard with it.
    m_editor.m_editorUI.setPointerOverridden(true);
    EditorUI *ui = &m_editor.m_editorUI;
    std::shared_ptr<void> release(nullptr, [ui](void *) { ui->setPointerOverridden(false); });
    return [this, queue, next, release]() -> std::optional<Mcp::ToolResult> {
        PandaUI::Context &context = m_editor.m_editorUI.context();
        if (*next >= queue->size()) {
            m_editor.m_editorUI.setPointerOverridden(false);
            Mcp::ToolResult result;
            result.text = pointerJson();
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
            case InputStep::Kind::Scroll: {
                PandaUI::ScrollEvent event;
                event.position = step.position;
                event.delta = step.delta;
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

Mcp::PendingResult EditorMcp::pointer(const rapidjson::Value &arguments) {
    if (m_editor.m_window == nullptr) { return Mcp::ready(Mcp::ToolResult::error("The editor has no window")); }
    const Fern::Size size = m_editor.m_window->getSize();
    const Fern::Size dpi = m_editor.m_window->getDpi();
    if (size.width <= 0.f || size.height <= 0.f) { return Mcp::ready(Mcp::ToolResult::error("The window of the editor has no size")); }
    // The coordinates are those of a screenshot of the window: its pixels, scaled down to `of`
    // when the window is wider.
    const double of = std::max(Mcp::numberArgument(arguments, "of", SCREENSHOT_DEFAULT_WIDTH), 1.0);
    const double imageWidth = std::min(of, static_cast<double>(size.width) * dpi.width);
    const double scale = static_cast<double>(size.width) / imageWidth;
    auto toWindow = [scale](double x, double y) {
        return PandaUI::Point(static_cast<float>(x * scale), static_cast<float>(y * scale));
    };
    const std::string action = Mcp::stringArgument(arguments, "action", "click");
    const PandaUI::Point at = toWindow(Mcp::numberArgument(arguments, "x", 0.0), Mcp::numberArgument(arguments, "y", 0.0));

    InputStep base;
    base.position = at;
    base.shift = Mcp::boolArgument(arguments, "shift", false);
    base.alt = Mcp::boolArgument(arguments, "alt", false);
    base.control = Mcp::boolArgument(arguments, "ctrl", false);
    base.super = Mcp::boolArgument(arguments, "super", false);
    auto step = [&base](InputStep::Kind kind) {
        InputStep result = base;
        result.kind = kind;
        return result;
    };
    std::vector<InputStep> steps;
    auto press = [&](PandaUI::PointerButton button, bool newClick) {
        InputStep down = step(InputStep::Kind::Down);
        down.button = button;
        down.newClick = newClick;
        steps.push_back(down);
        InputStep up = step(InputStep::Kind::Up);
        up.button = button;
        steps.push_back(up);
    };
    // The pointer arrives first: the hover of a frame is what the press of the next one meets.
    steps.push_back(step(InputStep::Kind::Move));
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
        down.newClick = true;
        steps.push_back(down);
    } else if (action == "up") {
        steps.push_back(step(InputStep::Kind::Up));
    } else if (action == "drag") {
        const PandaUI::Point to =
            toWindow(Mcp::numberArgument(arguments, "toX", 0.0), Mcp::numberArgument(arguments, "toY", 0.0));
        const int count = static_cast<int>(std::clamp(Mcp::numberArgument(arguments, "steps", 8.0), 1.0, 240.0));
        InputStep down = step(InputStep::Kind::Down);
        down.newClick = true;
        steps.push_back(down);
        for (int i = 1; i <= count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count);
            InputStep move = step(InputStep::Kind::Move);
            move.position = PandaUI::Point(at.x + (to.x - at.x) * t, at.y + (to.y - at.y) * t);
            steps.push_back(move);
        }
        InputStep up = step(InputStep::Kind::Up);
        up.position = to;
        steps.push_back(up);
    } else if (action == "scroll") {
        InputStep scroll = step(InputStep::Kind::Scroll);
        scroll.delta = PandaUI::Point(
            static_cast<float>(Mcp::numberArgument(arguments, "dx", 0.0)),
            static_cast<float>(-Mcp::numberArgument(arguments, "dy", 0.0))
        );
        steps.push_back(scroll);
    } else {
        return Mcp::ready(Mcp::ToolResult::error("action is click, doubleClick, rightClick, move, down, up, drag or scroll"));
    }
    // What the gesture set off (a popover, a rebuilt form) is laid out and drawn before the answer.
    steps.push_back(step(InputStep::Kind::Idle));
    steps.push_back(step(InputStep::Kind::Idle));
    return playInput(std::move(steps));
}

Mcp::PendingResult EditorMcp::key(const rapidjson::Value &arguments) {
    const std::string text = Mcp::stringArgument(arguments, "text");
    const std::string keyName = Mcp::stringArgument(arguments, "key");
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
    return playInput(std::move(steps));
}

Mcp::PendingResult EditorMcp::show(const rapidjson::Value &arguments) {
    if (!hasProject() || !m_editor.m_editorController) {
        return Mcp::ready(Mcp::ToolResult::error("No project is open in the editor"));
    }
    Editor::EditorController &controller = *m_editor.m_editorController;
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
        if (settings == "world") {
            controller.openSettings(SettingsScope::World);
        } else if (settings == "project") {
            controller.openSettings(SettingsScope::Project);
        } else if (settings == "editor") {
            controller.openSettings(SettingsScope::Editor);
        } else {
            return Mcp::ready(Mcp::ToolResult::error("settings is \"world\", \"project\" or \"editor\""));
        }
        return Mcp::ready(textResult("The settings are in front."));
    }
    if (!asset.empty()) {
        auto *handler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
        path_t file(asset);
        if (file.is_relative()) { file = m_editor.m_loader.getOpenedProjectPath() / file; }
        const AssetId id = handler != nullptr ? handler->getAssetId(file.lexically_normal()) : AssetId(0);
        if (!id) {
            return Mcp::ready(Mcp::ToolResult::error(
                "Not an asset of the project: " + file.string() + " (a new file becomes one after `reload`)"
            ));
        }
        controller.selectAsset(id);
        return Mcp::ready(textResult("The asset is selected: its tab is open."));
    }
    if (!entity.empty()) {
        Entity found = m_editor.m_currentWorld->findByTag(entity.c_str());
        if (!found.isValid()) { return Mcp::ready(Mcp::ToolResult::error("No entity is named " + entity)); }
        controller.selectEntity(found.getId());
        return Mcp::ready(textResult("The entity is selected: its tab is open."));
    }
    return Mcp::ready(Mcp::ToolResult::error("Say what to show: panel, settings, asset or entity"));
}

Mcp::PendingResult EditorMcp::exit(Exit kind, const rapidjson::Value &arguments) {
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
