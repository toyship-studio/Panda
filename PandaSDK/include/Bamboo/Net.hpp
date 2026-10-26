#pragma once

// Networking of a script: HTTP, TCP and UDP (context/networking.md).
//
// Which one:
//   HTTP — ask a server and get an answer: a login, a leaderboard, a JSON API, a file to download.
//          One request, one Response. https:// only (http:// to localhost for development).
//   TCP  — a connection between two sides: an ordered stream of bytes, nothing lost, nothing
//          duplicated. A chat, a lobby, a turn-based game, a connection to your own server.
//   UDP  — single datagrams without a connection: fast, may arrive out of order or not at all.
//          Positions many times a second, finding games in the local network (broadcast).
//
// Everything runs on the main thread: a call never blocks, and every callback — an answer, a
// connection, received bytes, a close — comes once per frame, before the update() of the scripts.
// Stop and the unload of the scripts close everything; nothing is called after that. A connection,
// a server or a socket lives while a handle to it exists (keep it as a field of the script): the
// last handle gone closes it. Bamboo::Net is a thin layer over Fern::Net and uses its types as they
// are.
//
//     // HTTP
//     Bamboo::Net::Http::get("https://example.com/api/scores", [](Bamboo::Net::Response &&r) {
//         if (r.ok()) { LOG_INFO("%s", r.bodyText().c_str()); }
//         else { LOG_INFO("failed: %s, status %d", Bamboo::Net::errorName(r.error), r.status); }
//     });
//
//     // TCP client
//     m_connection = Bamboo::Net::TcpConnection::connect("game.example.com", 7777, {
//         .connected = [this] { m_connection.send("hello"); },
//         .received = [](std::vector<uint8_t> &&bytes) { /* parse */ },
//         .closed = [](Bamboo::Net::Error error) { LOG_INFO("closed: %s", Bamboo::Net::errorName(error)); },
//     });
//
//     // TCP server
//     m_server = Bamboo::Net::TcpServer::listen(7777, {
//         .connected = [](Bamboo::Net::TcpConnection client) { client.send("welcome"); },
//         .received = [](Bamboo::Net::TcpConnection client, std::vector<uint8_t> &&bytes) { client.send(bytes); },
//         .disconnected = [](Bamboo::Net::TcpConnection, Bamboo::Net::Error) {},
//     });
//
//     // UDP
//     m_socket = Bamboo::Net::UdpSocket::open(7777, [](Bamboo::Net::Datagram &&d) { /* d.from, d.bytes */ });
//     m_socket.sendTo(*Bamboo::Net::Address::parse("192.168.1.20:7777"), "ping");

#include <Fern/Net/Error.hpp>
#include <Fern/Net/Http.hpp>
#include <Fern/Net/Socket.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Bamboo::Net {

using Error = Fern::Net::Error;
using Fern::Net::errorName;           // a word for a log line or a message
using Address = Fern::Net::Address;   // an IP address with a port: Address::parse("192.168.1.5:7777"), toString()
using Datagram = Fern::Net::Datagram; // from + bytes
using Bytes = std::span<const uint8_t>;

// Text as bytes and back, for text protocols (JSON lines, commands).
inline Bytes bytesOf(std::string_view text) {
    return Bytes(reinterpret_cast<const uint8_t *>(text.data()), text.size());
}
inline std::string textOf(Bytes bytes) {
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

/* ------------------------------ HTTP ------------------------------ */

using Method = Fern::Net::Method;
using Header = Fern::Net::Header;
using Request = Fern::Net::Request;   // method, url, headers, body, downloadTo, timeouts, …
using Response = Fern::Net::Response; // ok(), status, bodyText(), header(name), error
using Progress = Fern::Net::Progress;
using RequestHandle = Fern::Net::RequestHandle; // cancel(), isActive()
using Multipart = Fern::Net::Multipart;         // a form with files for a POST

namespace Http {
    using Complete = std::function<void(Response &&response)>;
    using OnProgress = std::function<void(const Progress &progress)>;

    // A canceled request never completes; a failed one completes with the error. An HTTP status is
    // not an error: a 404 comes with error None and status 404, Response::ok() is the usual check.
    // A body in memory is at most 16 MB (Request::maxBodyBytes); larger answers go to a file.
    RequestHandle get(std::string url, Complete complete);
    RequestHandle post(std::string url, std::string body, std::string contentType, Complete complete);
    RequestHandle send(Request request, Complete complete, OnProgress progress = {});
    // Into a file, atomically (<path>.part until it is complete); progress at most once per frame.
    RequestHandle download(std::string url, std::filesystem::path to, Complete complete, OnProgress progress = {});
    void cancelAll();
    // The User-Agent of the game; "Fern (<os>; <arch>)" otherwise. Before the first request.
    void setUserAgent(std::string userAgent);
} // namespace Http

/* ------------------------------ Names ------------------------------ */

// The addresses of a host name, IPv4 first; empty with HostNotFound when the name is unknown.
// TcpConnection::connect resolves on its own; this is for UDP, which has no connection.
void resolve(std::string host, uint16_t port, std::function<void(std::vector<Address> &&addresses, Error error)> complete);

/* ------------------------------ TCP ------------------------------ */

namespace Detail {
    struct TcpConnectionState;
    struct TcpServerState;
    struct UdpSocketState;
} // namespace Detail

struct TcpCallbacks {
    std::function<void()> connected;
    std::function<void(std::vector<uint8_t> &&bytes)> received; // what arrived since the last frame, in order
    // Once, when the connection ends from the other side or fails: None — the peer closed it;
    // HostNotFound, ConnectionRefused, Timeout, ConnectionFailed — why it did not work. Not
    // called after close().
    std::function<void(Error error)> closed;
};

// A TCP connection: the client side of connect(), or a client of a TcpServer. A copyable handle.
class TcpConnection {
public:
    TcpConnection() = default;

    // Connects to a host name or an address; every address of the name is tried, 15 s each.
    // Bytes sent before `connected` are queued and leave when the connection opens.
    static TcpConnection connect(std::string host, uint16_t port, TcpCallbacks callbacks);

    bool send(Bytes bytes); // false — closed, or more than 4 MB waiting (the connection then closes with TooLarge)
    bool send(std::string_view text) {
        return send(bytesOf(text));
    }
    void close(); // the peer sees the end of the stream; closed is not called
    bool isConnecting() const;
    bool isConnected() const;
    bool isOpen() const; // connecting or connected
    Address remote() const;
    uint64_t id() const; // 0 for an empty handle; tells connections apart
    explicit operator bool() const {
        return m_state != nullptr;
    }
    bool operator==(const TcpConnection &other) const = default;

private:
    friend class TcpServer;
    friend struct Detail::TcpServerState;
    explicit TcpConnection(std::shared_ptr<Detail::TcpConnectionState> state);

    std::shared_ptr<Detail::TcpConnectionState> m_state;
};

struct TcpServerCallbacks {
    std::function<void(TcpConnection client)> connected; // keep the handle to answer the client later
    std::function<void(TcpConnection client, std::vector<uint8_t> &&bytes)> received;
    std::function<void(TcpConnection client, Error error)> disconnected; // None — the client closed; not after close()
};

// A TCP server: listens on a port of every interface, IPv4 and IPv6. A copyable handle.
class TcpServer {
public:
    TcpServer() = default;

    static TcpServer listen(uint16_t port, TcpServerCallbacks callbacks, Error *error = nullptr);

    void close(); // every client too, silently
    bool isListening() const;
    uint16_t port() const;
    std::vector<TcpConnection> clients() const;
    explicit operator bool() const {
        return m_state != nullptr;
    }

private:
    std::shared_ptr<Detail::TcpServerState> m_state;
};

/* ------------------------------ UDP ------------------------------ */

// A UDP socket: datagrams in and out, no connection. A copyable handle.
class UdpSocket {
public:
    using Received = std::function<void(Datagram &&datagram)>;

    UdpSocket() = default;

    // On a port of every interface (IPv4); port 0 takes a free one (a client). broadcast allows
    // broadcast(). A datagram is at most 65507 bytes; the useful limit is far lower (an MTU).
    static UdpSocket open(uint16_t port, Received received, bool broadcast = false, Error *error = nullptr);
    // On one address: IPv6 (which receives IPv4 too where the system allows it), or one interface.
    static UdpSocket open(const Address &local, Received received, bool broadcast = false, Error *error = nullptr);

    bool sendTo(const Address &to, Bytes bytes);
    bool sendTo(const Address &to, std::string_view text) {
        return sendTo(to, bytesOf(text));
    }
    bool broadcast(uint16_t port, Bytes bytes); // to the local network; needs open(…, broadcast = true)
    void close();
    bool isOpen() const;
    Address local() const;
    uint16_t port() const;
    explicit operator bool() const {
        return m_state != nullptr;
    }

private:
    std::shared_ptr<Detail::UdpSocketState> m_state;
};

namespace Internal {
    // Called by the engine through the hooks of Bamboo::Async (OuterScriptHook), not from user code.
    void update();    // the callbacks of this frame
    void cancelAll(); // Stop: requests aborted, sockets closed, no callbacks
    void shutdown();  // before the scheduler of the bundle goes
} // namespace Internal

} // namespace Bamboo::Net
