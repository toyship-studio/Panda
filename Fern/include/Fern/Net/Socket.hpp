#pragma once

// Sockets of the network layer of Fern (context/networking.md, section 5): UDP datagrams, TCP
// streams and listeners, IPv4 and IPv6, over the sockets of the system (BSD sockets, Winsock 2).
//
// A socket never blocks and owns no thread: the owner polls it in its own tick — receive(),
// accept(), state() — where latency matters more than convenience. Only resolve() runs in the
// background (getaddrinfo blocks) as a job of a scheduler, and reports on the main thread. No TLS
// here: HTTPS covers it, and a netcode module protects its own packets. Bamboo::Net turns these
// into callbacks for scripts.

#include <Fern/Net/Error.hpp>
#include <PandaAsync/PandaAsync.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fern::Net {

// An IP address with a port.
struct Address {
    enum class Family : uint8_t { IPv4, IPv6 };

    Family family = Family::IPv4;
    std::array<uint8_t, 16> ip{}; // the first 4 bytes for IPv4; network order
    uint16_t port = 0;
    uint32_t scope = 0; // the scope id of a link-local IPv6 address

    static Address ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);
    static Address any(uint16_t port, Family family = Family::IPv4);      // 0.0.0.0 / :: — every interface
    static Address loopback(uint16_t port, Family family = Family::IPv4); // 127.0.0.1 / ::1
    static Address broadcast(uint16_t port);                              // 255.255.255.255 — the local network (IPv4)
    // "192.168.1.5:7777", "192.168.1.5" (port 0), "[::1]:7777", "::1", "fe80::1%4". A host name is
    // not an address: resolve() turns it into addresses.
    static std::optional<Address> parse(std::string_view text);
    std::string toString() const; // "192.168.1.5:7777", "[::1]:7777"
    std::string ipString() const; // without the port
    bool isAny() const;
    bool isLoopback() const;

    bool operator==(const Address &other) const = default;
};

// The addresses of a host name, IPv4 first, on the main thread of the scheduler; a numeric address
// comes back as it is, without a lookup. Empty with HostNotFound when the name is unknown. The group
// is the scheduler's: cancelGroup drops the answer.
PandaAsync::JobHandle resolve(
    PandaAsync::Scheduler &scheduler,
    std::string host,
    uint16_t port,
    std::function<void(std::vector<Address> &&addresses, Error error)> complete,
    PandaAsync::GroupId group = 0
);

struct Datagram {
    Address from;
    std::vector<uint8_t> bytes;
};

// A UDP socket: single datagrams without a connection, no order and no delivery guarantee.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(UdpSocket &&other) noexcept;
    UdpSocket &operator=(UdpSocket &&other) noexcept;
    UdpSocket(const UdpSocket &) = delete;
    UdpSocket &operator=(const UdpSocket &) = delete;

    // Binds to the address; port 0 takes a free one (localAddress tells which). An IPv6 address
    // receives IPv4 too where the system allows it (dual stack). broadcast allows sending to
    // Address::broadcast. A failure returns a closed socket and the error.
    static UdpSocket bind(const Address &local, bool broadcast = false, Error *error = nullptr);
    bool isOpen() const;
    // One datagram of at most 65507 bytes. false — closed, an address this socket cannot reach, or
    // the system refused it (a full buffer drops the datagram: that is UDP).
    bool sendTo(const Address &to, std::span<const uint8_t> bytes);
    // The next datagram waiting, or nothing. Never blocks.
    std::optional<Datagram> receive();
    Address localAddress() const;
    void close();

private:
    intptr_t m_socket = -1;
    Address m_local;
};

// A TCP connection: an ordered stream of bytes both ways, nothing lost, nothing duplicated.
class TcpStream {
public:
    enum class State : uint8_t { Closed, Connecting, Connected };

    TcpStream() = default;
    ~TcpStream();
    TcpStream(TcpStream &&other) noexcept;
    TcpStream &operator=(TcpStream &&other) noexcept;
    TcpStream(const TcpStream &) = delete;
    TcpStream &operator=(const TcpStream &) = delete;

    // Starts connecting and returns at once; state() reports Connecting until the system answers.
    // A failure the system reports at once returns a Closed stream with the error.
    static TcpStream connect(const Address &remote, Error *error = nullptr);
    // Polls the connect in progress and reports the state. After Closed, error() says why: None —
    // the peer ended the stream, or close() was called here.
    State state();
    State currentState() const { // the last known state, without polling
        return m_state;
    }
    bool isOpen() const {
        return m_state != State::Closed;
    }
    Error error() const;
    // Queues the bytes and sends what the system takes now; the rest leaves with the next send,
    // receive or update. Bytes queued while Connecting leave when the connection opens. false —
    // Closed, or more than 4 MB waiting: the stream is then closed with TooLarge (a peer that does
    // not read is gone).
    bool send(std::span<const uint8_t> bytes);
    // What has arrived, up to maxBytes, in order; empty when nothing waits or the stream is Closed.
    // Never blocks.
    std::vector<uint8_t> receive(size_t maxBytes = 1u << 20);
    // Flushes the send queue and polls the connect, for an owner that only sends; receive() and
    // state() do the same.
    void update();
    size_t pendingSendBytes() const;
    // Nagle's algorithm is off by default (small messages leave at once); on for bulk transfers.
    void setNoDelay(bool noDelay);
    Address remoteAddress() const;
    Address localAddress() const; // known once Connected
    void close();                 // the peer sees the end of the stream

private:
    friend class TcpListener;
    void flush();
    void fail(Error error);

    intptr_t m_socket = -1;
    State m_state = State::Closed;
    Error m_error = Error::None;
    std::vector<uint8_t> m_queue;
    size_t m_queueOffset = 0;
    Address m_remote;
    Address m_local;
};

// A listening TCP socket: accepts connections without blocking.
class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(TcpListener &&other) noexcept;
    TcpListener &operator=(TcpListener &&other) noexcept;
    TcpListener(const TcpListener &) = delete;
    TcpListener &operator=(const TcpListener &) = delete;

    // Binds and listens; port 0 takes a free one. An IPv6 address accepts IPv4 too where the
    // system allows it (dual stack). A failure returns a closed listener and the error.
    static TcpListener bind(const Address &local, Error *error = nullptr, int backlog = 16);
    bool isOpen() const;
    // The next connection waiting, already Connected; nothing when none waits. Never blocks.
    std::optional<TcpStream> accept();
    Address localAddress() const;
    void close();

private:
    intptr_t m_socket = -1;
    Address m_local;
};

} // namespace Fern::Net
