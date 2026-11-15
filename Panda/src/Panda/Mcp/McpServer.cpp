#include "Panda/Mcp/McpServer.hpp"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace Panda::Mcp {

namespace {

    constexpr const char *ENDPOINT = "/mcp";
    // The revisions of the protocol this server speaks; the first one is offered to a client that
    // asks for a revision the server does not know.
    constexpr const char *PROTOCOLS[] = {"2025-06-18", "2025-03-26"};
    constexpr size_t MAX_HEADER_BYTES = 16 * 1024;
    constexpr size_t MAX_BODY_BYTES = 4 * 1024 * 1024;
    constexpr size_t MAX_CONNECTIONS = 16;
    // An answer leaves in slices: the socket gives up on a peer with megabytes waiting.
    constexpr size_t SEND_SLICE = 256 * 1024;
    constexpr double PENDING_TIMEOUT_SECONDS = 120.0;
    constexpr double IDLE_TIMEOUT_SECONDS = 300.0;

    using JsonWriter = rapidjson::Writer<rapidjson::StringBuffer>;

    double nowSeconds() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    std::string lower(std::string_view text) {
        std::string result(text);
        for (char &c : result) {
            if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
        }
        return result;
    }

    std::string_view trim(std::string_view text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.remove_prefix(1); }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) { text.remove_suffix(1); }
        return text;
    }

    std::string base64(const std::vector<uint8_t> &bytes) {
        static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string text;
        text.reserve((bytes.size() + 2) / 3 * 4);
        for (size_t i = 0; i < bytes.size(); i += 3) {
            const uint32_t a = bytes[i];
            const uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
            const uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
            const uint32_t triple = (a << 16) | (b << 8) | c;
            text.push_back(ALPHABET[(triple >> 18) & 63]);
            text.push_back(ALPHABET[(triple >> 12) & 63]);
            text.push_back(i + 1 < bytes.size() ? ALPHABET[(triple >> 6) & 63] : '=');
            text.push_back(i + 2 < bytes.size() ? ALPHABET[triple & 63] : '=');
        }
        return text;
    }

    // "127.0.0.1:7717", "localhost", "[::1]:7717": the host of a local request, with or without a port.
    bool isLocalHost(std::string_view host) {
        host = trim(host);
        if (!host.empty() && host.front() == '[') { // an IPv6 literal
            const size_t close = host.find(']');
            return close != std::string_view::npos && host.substr(0, close + 1) == "[::1]";
        }
        const size_t colon = host.rfind(':');
        if (colon != std::string_view::npos) { host = host.substr(0, colon); }
        const std::string name = lower(host);
        return name == "127.0.0.1" || name == "localhost";
    }

    // The Origin header of a request made by a page: only a local page is let in. A program that
    // is not a browser sends none.
    bool isLocalOrigin(std::string_view origin) {
        origin = trim(origin);
        for (std::string_view scheme : {std::string_view("http://"), std::string_view("https://")}) {
            if (origin.substr(0, scheme.size()) == scheme) { return isLocalHost(origin.substr(scheme.size())); }
        }
        return false;
    }

    std::string toJson(const rapidjson::Value &value) {
        rapidjson::StringBuffer buffer;
        JsonWriter writer(buffer);
        value.Accept(writer);
        return std::string(buffer.GetString(), buffer.GetSize());
    }

    void writeString(JsonWriter &writer, const std::string &text) {
        writer.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
    }

    const char *reasonPhrase(int status) {
        switch (status) {
            case 100: return "Continue";
            case 200: return "OK";
            case 202: return "Accepted";
            case 400: return "Bad Request";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 411: return "Length Required";
            case 413: return "Content Too Large";
            case 431: return "Request Header Fields Too Large";
            default: return "Error";
        }
    }

    const rapidjson::Value &emptyObject() {
        static const rapidjson::Value EMPTY(rapidjson::kObjectType);
        return EMPTY;
    }

} // namespace

PendingResult ready(ToolResult result) {
    return [result = std::move(result)]() -> std::optional<ToolResult> { return result; };
}

std::string stringArgument(const rapidjson::Value &arguments, const char *name, const std::string &fallback) {
    if (!arguments.IsObject() || !arguments.HasMember(name) || !arguments[name].IsString()) { return fallback; }
    return std::string(arguments[name].GetString(), arguments[name].GetStringLength());
}

bool boolArgument(const rapidjson::Value &arguments, const char *name, bool fallback) {
    if (!arguments.IsObject() || !arguments.HasMember(name) || !arguments[name].IsBool()) { return fallback; }
    return arguments[name].GetBool();
}

double numberArgument(const rapidjson::Value &arguments, const char *name, double fallback) {
    if (!arguments.IsObject() || !arguments.HasMember(name) || !arguments[name].IsNumber()) { return fallback; }
    return arguments[name].GetDouble();
}

bool numbersArgument(const rapidjson::Value &arguments, const char *name, double *values, size_t count) {
    if (!arguments.IsObject() || !arguments.HasMember(name) || !arguments[name].IsArray()) { return false; }
    const rapidjson::Value &array = arguments[name];
    if (array.Size() != count) { return false; }
    for (rapidjson::SizeType i = 0; i < array.Size(); ++i) {
        if (!array[i].IsNumber()) { return false; }
        values[i] = array[i].GetDouble();
    }
    return true;
}

struct Server::Request {
    std::string method;                                       // "POST"
    std::string path;                                         // "/mcp", without the query
    std::vector<std::pair<std::string, std::string>> headers; // the names in lower case
    std::string body;

    const std::string *header(const char *name) const {
        for (const auto &[key, value] : headers) {
            if (key == name) { return &value; }
        }
        return nullptr;
    }
};

struct Server::Connection {
    Fern::Net::TcpStream stream;
    std::string input;  // received and not taken as a request yet
    std::string output; // answers not handed to the socket yet
    size_t outputOffset = 0;
    bool continueSent = false;  // "100 Continue" went out for the request being received
    bool closeWhenSent = false; // the client asked to close, or the request was not HTTP
    double lastActivity = 0.0;
    // A tool call that needs more frames: the connection answers one request at a time.
    PendingResult pending;
    std::string pendingId;
    double pendingSince = 0.0;
};

Server::Server(std::string name, std::string title, std::string version, std::string instructions)
    : m_name(std::move(name))
    , m_title(std::move(title))
    , m_version(std::move(version))
    , m_instructions(std::move(instructions)) {}

Server::~Server() {
    stop();
}

void Server::addTool(Tool tool) {
    m_tools.push_back(std::move(tool));
}

bool Server::start(uint16_t port) {
    stop();
    Fern::Net::Error error = Fern::Net::Error::None;
    m_listener = Fern::Net::TcpListener::bind(Fern::Net::Address::loopback(port), &error);
    if (!m_listener.isOpen()) { return false; } // the application says so: it knows whose port it is
    m_port = m_listener.localAddress().port;
    return true;
}

void Server::stop() {
    m_connections.clear();
    m_listener.close();
    m_port = 0;
}

bool Server::isRunning() const {
    return m_listener.isOpen();
}

bool Server::hasUnsentData() const {
    for (const std::unique_ptr<Connection> &connection : m_connections) {
        if (!connection->output.empty() || connection->stream.pendingSendBytes() > 0 || connection->pending) { return true; }
    }
    return false;
}

void Server::update() {
    if (!m_listener.isOpen()) { return; }
    acceptConnections();
    // By index: a tool may not add connections, but the list is taken as it is each step.
    for (size_t i = 0; i < m_connections.size(); ++i) {
        updateConnection(*m_connections[i]);
    }
    m_connections.erase(
        std::remove_if(
            m_connections.begin(), m_connections.end(),
            [](const std::unique_ptr<Connection> &connection) { return !connection->stream.isOpen(); }
        ),
        m_connections.end()
    );
}

void Server::acceptConnections() {
    while (std::optional<Fern::Net::TcpStream> stream = m_listener.accept()) {
        if (m_connections.size() >= MAX_CONNECTIONS) {
            stream->close();
            continue;
        }
        auto connection = std::make_unique<Connection>();
        connection->stream = std::move(*stream);
        connection->lastActivity = nowSeconds();
        m_connections.push_back(std::move(connection));
    }
}

void Server::updateConnection(Connection &connection) {
    if (connection.stream.state() == Fern::Net::TcpStream::State::Closed) { return; }
    const double now = nowSeconds();
    if (connection.pending) {
        std::optional<ToolResult> result = connection.pending();
        if (!result && now - connection.pendingSince > PENDING_TIMEOUT_SECONDS) {
            result = ToolResult::error("The tool did not answer in time");
        }
        if (result) {
            connection.pending = nullptr;
            respondToolResult(connection, connection.pendingId, *result);
            connection.lastActivity = now;
        }
    }
    if (!connection.pending) {
        const std::vector<uint8_t> bytes = connection.stream.receive();
        if (!bytes.empty()) {
            connection.input.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            connection.lastActivity = now;
        }
        while (!connection.pending && !connection.closeWhenSent && takeRequest(connection)) {}
    }
    while (connection.outputOffset < connection.output.size() && connection.stream.pendingSendBytes() < SEND_SLICE) {
        const size_t count = std::min(SEND_SLICE, connection.output.size() - connection.outputOffset);
        const auto *data = reinterpret_cast<const uint8_t *>(connection.output.data()) + connection.outputOffset;
        if (!connection.stream.send({data, count})) { break; }
        connection.outputOffset += count;
    }
    if (connection.outputOffset >= connection.output.size()) {
        connection.output.clear();
        connection.outputOffset = 0;
    }
    connection.stream.update();
    const bool sent = connection.output.empty() && connection.stream.pendingSendBytes() == 0;
    if (connection.closeWhenSent && sent) {
        connection.stream.close();
    } else if (!connection.pending && now - connection.lastActivity > IDLE_TIMEOUT_SECONDS) {
        connection.stream.close();
    }
}

bool Server::takeRequest(Connection &connection) {
    const size_t headerEnd = connection.input.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        if (connection.input.size() > MAX_HEADER_BYTES) {
            connection.closeWhenSent = true;
            connection.input.clear();
            respond(connection, 431, R"({"error":"The request headers are too long"})");
        }
        return false;
    }
    const auto refuse = [&](int status, const char *message) {
        connection.closeWhenSent = true;
        connection.input.clear();
        respond(connection, status, std::string(R"({"error":")") + message + "\"}");
        return false;
    };

    Request request;
    size_t contentLength = 0;
    bool expectsContinue = false;
    bool closes = false;
    const std::string_view head(connection.input.data(), headerEnd);
    size_t lineStart = 0;
    bool firstLine = true;
    while (lineStart <= head.size()) {
        size_t lineEnd = head.find("\r\n", lineStart);
        if (lineEnd == std::string_view::npos) { lineEnd = head.size(); }
        const std::string_view line = head.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 2;
        if (firstLine) { // "POST /mcp HTTP/1.1"
            firstLine = false;
            const size_t methodEnd = line.find(' ');
            const size_t targetEnd = methodEnd == std::string_view::npos ? methodEnd : line.find(' ', methodEnd + 1);
            if (targetEnd == std::string_view::npos || line.substr(targetEnd + 1, 5) != "HTTP/") {
                return refuse(400, "Not an HTTP request");
            }
            request.method = std::string(line.substr(0, methodEnd));
            std::string_view target = line.substr(methodEnd + 1, targetEnd - methodEnd - 1);
            request.path = std::string(target.substr(0, target.find('?')));
            continue;
        }
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) { continue; }
        std::string name = lower(trim(line.substr(0, colon)));
        std::string value(trim(line.substr(colon + 1)));
        if (name == "content-length") {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos || value.size() > 9) {
                return refuse(400, "Content-Length is not a number");
            }
            contentLength = static_cast<size_t>(std::stoul(value));
        } else if (name == "transfer-encoding") {
            return refuse(411, "A request body needs Content-Length");
        } else if (name == "expect") {
            expectsContinue = lower(value).find("100-continue") != std::string::npos;
        } else if (name == "connection") {
            closes = lower(value).find("close") != std::string::npos;
        }
        request.headers.emplace_back(std::move(name), std::move(value));
    }
    if (contentLength > MAX_BODY_BYTES) { return refuse(413, "The request body is too long"); }

    const size_t total = headerEnd + 4 + contentLength;
    if (connection.input.size() < total) {
        // A client that sends a large body waits for this line first (curl does above 1 KB).
        if (expectsContinue && !connection.continueSent) {
            connection.output += "HTTP/1.1 100 Continue\r\n\r\n";
            connection.continueSent = true;
        }
        return false;
    }
    request.body = connection.input.substr(headerEnd + 4, contentLength);
    connection.input.erase(0, total);
    connection.continueSent = false;
    if (closes) { connection.closeWhenSent = true; }
    handleRequest(connection, request);
    return true;
}

void Server::handleRequest(Connection &connection, const Request &request) {
    // Local callers only: a page of a remote site that resolves its name to 127.0.0.1 (DNS
    // rebinding) still carries its own Host and Origin.
    const std::string *host = request.header("host");
    const std::string *origin = request.header("origin");
    if (host == nullptr || !isLocalHost(*host) || (origin != nullptr && !isLocalOrigin(*origin))) {
        respond(connection, 403, R"({"error":"The MCP server answers local requests only"})");
        return;
    }
    if (request.path != ENDPOINT) {
        respond(connection, 404, R"({"error":"The MCP endpoint is /mcp"})");
        return;
    }
    if (request.method != "POST") {
        // GET would open a stream of server messages and DELETE would end a session: the server
        // has neither.
        connection.output += "HTTP/1.1 405 Method Not Allowed\r\nAllow: POST\r\nContent-Length: 0\r\n\r\n";
        return;
    }
    if (const std::string *version = request.header("mcp-protocol-version")) {
        const bool known = std::any_of(std::begin(PROTOCOLS), std::end(PROTOCOLS), [&](const char *protocol) {
            return *version == protocol;
        });
        if (!known) {
            respondError(connection, 400, "null", -32600, "Unsupported MCP-Protocol-Version: " + *version);
            return;
        }
    }
    handleMessage(connection, request.body);
}

void Server::handleMessage(Connection &connection, const std::string &body) {
    rapidjson::Document message;
    message.Parse(body.data(), body.size());
    if (message.HasParseError()) {
        respondError(connection, 400, "null", -32700, "Parse error");
        return;
    }
    if (!message.IsObject()) { // a batch is not a part of the protocol any more
        respondError(connection, 400, "null", -32600, "A single JSON-RPC message is expected");
        return;
    }
    const bool hasMethod = message.HasMember("method") && message["method"].IsString();
    const bool hasId = message.HasMember("id") && !message["id"].IsNull();
    if (!hasMethod || !hasId) {
        // A notification, or an answer of the client: taken, nothing to say back.
        connection.output += "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n";
        return;
    }
    const std::string id = toJson(message["id"]);
    const std::string method(message["method"].GetString(), message["method"].GetStringLength());
    const rapidjson::Value &params =
        message.HasMember("params") && message["params"].IsObject() ? message["params"] : emptyObject();

    if (method == "initialize") {
        // The revision the client asked for when the server speaks it, else the server's own.
        const std::string requested = stringArgument(params, "protocolVersion");
        const char *protocol = PROTOCOLS[0];
        for (const char *known : PROTOCOLS) {
            if (requested == known) { protocol = known; }
        }
        rapidjson::StringBuffer buffer;
        JsonWriter writer(buffer);
        writer.StartObject();
        writer.Key("protocolVersion");
        writer.String(protocol);
        writer.Key("capabilities");
        writer.StartObject();
        writer.Key("tools");
        writer.StartObject();
        writer.EndObject();
        writer.EndObject();
        writer.Key("serverInfo");
        writer.StartObject();
        writer.Key("name");
        writeString(writer, m_name);
        writer.Key("title");
        writeString(writer, m_title);
        writer.Key("version");
        writeString(writer, m_version);
        writer.EndObject();
        if (!m_instructions.empty()) {
            writer.Key("instructions");
            writeString(writer, m_instructions);
        }
        writer.EndObject();
        respondResult(connection, id, std::string(buffer.GetString(), buffer.GetSize()));
        return;
    }
    if (method == "ping") {
        respondResult(connection, id, "{}");
        return;
    }
    if (method == "tools/list") {
        respondResult(connection, id, toolListJson());
        return;
    }
    if (method == "tools/call") {
        const std::string name = stringArgument(params, "name");
        const auto tool = std::find_if(m_tools.begin(), m_tools.end(), [&](const Tool &candidate) {
            return candidate.name == name;
        });
        if (tool == m_tools.end()) {
            respondError(connection, 200, id, -32602, "Unknown tool: " + name);
            return;
        }
        const rapidjson::Value &arguments =
            params.HasMember("arguments") && params["arguments"].IsObject() ? params["arguments"] : emptyObject();
        PendingResult pending = tool->call(arguments);
        std::optional<ToolResult> result =
            pending ? pending() : std::optional<ToolResult>(ToolResult::error("The tool returned nothing"));
        if (result) {
            respondToolResult(connection, id, *result);
            return;
        }
        connection.pending = std::move(pending);
        connection.pendingId = id;
        connection.pendingSince = nowSeconds();
        return;
    }
    respondError(connection, 200, id, -32601, "Method not found: " + method);
}

std::string Server::toolListJson() const {
    rapidjson::StringBuffer buffer;
    JsonWriter writer(buffer);
    writer.StartObject();
    writer.Key("tools");
    writer.StartArray();
    for (const Tool &tool : m_tools) {
        static const std::string NO_ARGUMENTS = R"({"type":"object","properties":{}})";
        const std::string &schema = tool.inputSchema.empty() ? NO_ARGUMENTS : tool.inputSchema;
        writer.StartObject();
        writer.Key("name");
        writeString(writer, tool.name);
        writer.Key("description");
        writeString(writer, tool.description);
        writer.Key("inputSchema");
        writer.RawValue(schema.data(), schema.size(), rapidjson::kObjectType);
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
}

void Server::respond(Connection &connection, int status, const std::string &body, const char *contentType) {
    std::string &out = connection.output;
    out += "HTTP/1.1 " + std::to_string(status) + " " + reasonPhrase(status) + "\r\n";
    out += std::string("Content-Type: ") + contentType + "\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += connection.closeWhenSent ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
    out += "\r\n";
    out += body;
}

void Server::respondResult(Connection &connection, const std::string &id, const std::string &resultJson) {
    respond(connection, 200, R"({"jsonrpc":"2.0","id":)" + id + R"(,"result":)" + resultJson + "}");
}

void Server::respondError(Connection &connection, int status, const std::string &id, int code, const std::string &message) {
    rapidjson::StringBuffer buffer;
    JsonWriter writer(buffer);
    writer.StartObject();
    writer.Key("code");
    writer.Int(code);
    writer.Key("message");
    writeString(writer, message);
    writer.EndObject();
    const std::string error(buffer.GetString(), buffer.GetSize());
    respond(connection, status, R"({"jsonrpc":"2.0","id":)" + id + R"(,"error":)" + error + "}");
}

void Server::respondToolResult(Connection &connection, const std::string &id, const ToolResult &result) {
    rapidjson::StringBuffer buffer;
    JsonWriter writer(buffer);
    writer.StartObject();
    writer.Key("content");
    writer.StartArray();
    writer.StartObject();
    writer.Key("type");
    writer.String("text");
    writer.Key("text");
    writeString(writer, result.text);
    writer.EndObject();
    if (!result.png.empty()) {
        writer.StartObject();
        writer.Key("type");
        writer.String("image");
        writer.Key("data");
        writeString(writer, base64(result.png));
        writer.Key("mimeType");
        writer.String("image/png");
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("isError");
    writer.Bool(result.isError);
    writer.EndObject();
    respondResult(connection, id, std::string(buffer.GetString(), buffer.GetSize()));
}

} // namespace Panda::Mcp
