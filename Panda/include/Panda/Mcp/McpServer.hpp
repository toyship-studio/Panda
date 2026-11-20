#pragma once

// An MCP server (Model Context Protocol, modelcontextprotocol.io) inside a running application:
// the way an AI agent looks at the editor or a game and acts on it. The transport is Streamable
// HTTP of the protocol in its smallest form — one endpoint, POST with a JSON-RPC message, one JSON
// answer; no sessions and no server-sent events, so a restart of the application is invisible to
// the client: the next request simply reaches the new process.
//
// The server owns no thread. It is polled once a frame on the main thread (update), where the
// sockets of Fern never block: a tool runs inside the frame and touches the engine like any other
// code of the frame. Latency is one frame. A tool that needs more than one frame (a screenshot
// waits for the GPU) answers later through PendingResult.
//
// The server listens on the loopback address only and refuses requests whose Host or Origin is
// not local (a web page cannot reach it through DNS rebinding). It is off unless the application
// starts it.

#include <Fern/Net/Socket.hpp>
#include <Rain/Value.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Panda::Mcp {

// What a tool answers: text the model reads (plain text or JSON), and optionally a picture.
struct ToolResult final {
    std::string text;
    bool isError = false;     // the tool ran and failed: the text says why
    std::vector<uint8_t> png; // a picture next to the text (the bytes of a PNG file); empty — none

    static ToolResult error(std::string message) {
        ToolResult result;
        result.text = std::move(message);
        result.isError = true;
        return result;
    }
};

// The answer of a call: asked every frame until it has a result. A tool that answers at once
// returns ready(result).
using PendingResult = std::function<std::optional<ToolResult>()>;
PendingResult ready(ToolResult result);

struct Tool final {
    std::string name;        // "screenshot"
    std::string description; // what it does and when to call it: the model chooses by this text
    // The JSON Schema of the arguments, as text: {"type":"object","properties":{...}}. Empty — no
    // arguments. Read once by addTool: a schema that is not JSON is an error in the log, and the
    // tool is listed without arguments.
    std::string inputSchema;
    // Runs on the main thread inside the frame. `arguments` is the "arguments" object of the call
    // (an object, maybe without members).
    std::function<PendingResult(const Rain::Value &arguments)> call;
};

// The arguments of a call by name, with a default for what the caller left out.
std::string stringArgument(const Rain::Value &arguments, const char *name, const std::string &fallback = {});
bool boolArgument(const Rain::Value &arguments, const char *name, bool fallback);
double numberArgument(const Rain::Value &arguments, const char *name, double fallback);
// An array of exactly `count` numbers; false — absent or of another shape.
bool numbersArgument(const Rain::Value &arguments, const char *name, double *values, size_t count);

class Server final {
public:
    // name — the identifier of the server ("panda-editor"); instructions — how to use the tools,
    // shown to the model once when it connects.
    Server(std::string name, std::string title, std::string version, std::string instructions);
    ~Server();
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    void addTool(Tool tool);
    // Listens on 127.0.0.1:port; the endpoint is http://127.0.0.1:<port>/mcp. false — the port
    // is taken or the system refused.
    bool start(uint16_t port);
    void stop();
    bool isRunning() const;
    uint16_t port() const {
        return m_port;
    }
    // Once a frame on the main thread: accepts connections, reads requests, runs the tools, writes
    // the answers. Never blocks.
    void update();
    // Bytes of an answer are still on their way to a client (an application that is about to exit
    // waits for false, so the answer to "restart" leaves first).
    bool hasUnsentData() const;

private:
    struct Connection;
    struct Request;

    void acceptConnections();
    void updateConnection(Connection &connection);
    // true — a whole request was taken off the input of the connection and answered (or is being
    // answered by a pending tool call).
    bool takeRequest(Connection &connection);
    void handleRequest(Connection &connection, const Request &request);
    void handleMessage(Connection &connection, const std::string &body);
    void respond(Connection &connection, int status, const std::string &body, const char *contentType = "application/json");
    // An answer outside JSON-RPC: the HTTP error of a request the server does not take.
    void respondFailure(Connection &connection, int status, const std::string &message);
    void respondResult(Connection &connection, const Rain::Value &id, Rain::Value result);
    void respondError(Connection &connection, int status, const Rain::Value &id, int code, const std::string &message);
    void respondToolResult(Connection &connection, const Rain::Value &id, const ToolResult &result);
    Rain::Value toolList() const;

    std::string m_name;
    std::string m_title;
    std::string m_version;
    std::string m_instructions;
    std::vector<Tool> m_tools;
    std::vector<Rain::Value> m_schemas; // the input schema of each tool, read once (addTool)
    Fern::Net::TcpListener m_listener;
    uint16_t m_port = 0;
    std::vector<std::unique_ptr<Connection>> m_connections;
};

} // namespace Panda::Mcp
