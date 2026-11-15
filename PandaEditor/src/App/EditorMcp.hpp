#pragma once

#include "Viewport/EditorViewports.hpp" // ViewportKind

#include <Panda/Mcp/McpServer.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Fern {
class Window;
}

namespace PandaUI {
class Context;
}

namespace Panda {

class EditorLayer;

// The MCP server of the editor (Panda/Mcp/McpServer.hpp; PandaSDK/docs/zed-and-agents.md): how an
// AI agent that works in the project folder sees the result of its work and drives the editor.
//
// The server is thin on purpose. The files of the project are the contract (context/vision.md):
// the agent edits worlds, materials, shaders, settings and scripts as text, with its own tools.
// What it cannot do with files is the editor's part, and only that is here — to look (a
// screenshot of a viewport), to read what the editor said (the log), to make the editor take the
// files (Reload, Save), to run the game (Play, Stop), to place the editor camera, to restart. The
// screenshot of the whole window shows the agent the editor itself, when the editor is its work,
// and `pointer` and `key` let it use what it sees as the user would: press a button, drag a
// label, type into a field — the input goes into the UI of the editor where the mouse and the
// keyboard of the user go.
// There are no tools that edit a world or an asset: that would be a second way to do what a text
// file already does.
//
// A tool acts as the user does, through the same functions of EditorLayer, at the start of a
// frame. Nobody is at the screen while it runs: a native dialog is not shown
// (SystemTools::Unattended) — the tool fails and says what the editor wanted to ask.
//
// The server follows the "mcp" section of the open project (ProjectMcpSettings): it listens while
// a project with the section on is open, on the port of the section.
class EditorMcp final {
public:
    explicit EditorMcp(EditorLayer &editor);
    ~EditorMcp();
    EditorMcp(const EditorMcp &) = delete;
    EditorMcp &operator=(const EditorMcp &) = delete;

    // Once a frame, before the frame of the editor is built.
    void update();

private:
    struct Screenshot;
    struct InputStep;
    enum class Exit { None, Restart, Quit };

    void syncServer(); // the server follows the project and its "mcp" section
    void registerTools();
    void performExit();

    Mcp::PendingResult status();
    Mcp::PendingResult logs(const rapidjson::Value &arguments);
    Mcp::PendingResult screenshot(const rapidjson::Value &arguments);
    Mcp::PendingResult reload(const rapidjson::Value &arguments);
    Mcp::PendingResult save(const rapidjson::Value &arguments);
    Mcp::PendingResult play();
    Mcp::PendingResult stop();
    Mcp::PendingResult camera(const rapidjson::Value &arguments);
    Mcp::PendingResult openWorld(const rapidjson::Value &arguments);
    Mcp::PendingResult show(const rapidjson::Value &arguments);
    Mcp::PendingResult pointer(const rapidjson::Value &arguments);
    Mcp::PendingResult key(const rapidjson::Value &arguments);
    Mcp::PendingResult command(const rapidjson::Value &arguments);
    Mcp::PendingResult exit(Exit kind, const rapidjson::Value &arguments);

    // A frame of a screenshot in flight; a result — the shot is taken or has failed.
    std::optional<Mcp::ToolResult> stepScreenshot(Screenshot &shot);
    Mcp::ToolResult finishScreenshot(Screenshot &shot);
    void showViewport(ViewportKind view);
    // Draws the window into the capture target and asks for its pixels; false — the window has no size.
    bool captureWindow(Screenshot &shot);

    // Plays the steps of a gesture, one a frame, and answers with what is under the pointer.
    Mcp::PendingResult playInput(std::vector<InputStep> steps);
    std::string pointerJson() const;
    PandaUI::Context &inputContext() const;
    Fern::Window *inputWindow() const;

    bool hasProject() const;
    std::string cameraJson() const;
    std::vector<std::string> dirtyDocuments() const;

    EditorLayer &m_editor;
    Mcp::Server m_server;
    // The target of a screenshot of the whole window: the chrome is drawn into it once more.
    Miren::TextureHandle m_windowCapture;
    Miren::FrameBufferHandle m_windowCaptureBuffer;
    uint32_t m_windowCaptureWidth = 0;
    uint32_t m_windowCaptureHeight = 0;
    int m_port = 0; // the port the server was last asked to listen on; 0 — off
    Exit m_exit = Exit::None;
    int m_exitFrames = 0; // frames since the answer to "restart" / "quit" has left
};

} // namespace Panda
