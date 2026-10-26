#include <Fern/Net/Socket.hpp>

#include <algorithm>
#include <cstring>
#include <string>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <cerrno>
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <poll.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace Fern::Net {

namespace {

#if defined(_WIN32)
    using SocketType = SOCKET;
    using SockLen = int;
    constexpr SocketType kInvalidSocket = INVALID_SOCKET;
    constexpr int kSendFlags = 0;
    int lastError() {
        return WSAGetLastError();
    }
    bool isWouldBlock(int error) {
        return error == WSAEWOULDBLOCK;
    }
    bool isInProgress(int error) {
        return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
    }
    bool isInterrupted(int error) {
        return error == WSAEINTR;
    }
    void closeSocket(SocketType socket) {
        closesocket(socket);
    }
    bool setNonBlocking(SocketType socket) {
        u_long one = 1;
        return ioctlsocket(socket, FIONBIO, &one) == 0;
    }
#else
    using SocketType = int;
    using SockLen = socklen_t;
    constexpr SocketType kInvalidSocket = -1;
#    if defined(MSG_NOSIGNAL)
    constexpr int kSendFlags = MSG_NOSIGNAL; // a send to a closed peer is an error, not a signal
#    else
    constexpr int kSendFlags = 0;
#    endif
    int lastError() {
        return errno;
    }
    bool isWouldBlock(int error) {
        return error == EWOULDBLOCK || error == EAGAIN;
    }
    bool isInProgress(int error) {
        return error == EINPROGRESS;
    }
    bool isInterrupted(int error) {
        return error == EINTR;
    }
    void closeSocket(SocketType socket) {
        ::close(socket);
    }
    bool setNonBlocking(SocketType socket) {
        const int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
    }
#endif

    SocketType toSocket(intptr_t handle) {
        return static_cast<SocketType>(handle);
    }
    intptr_t fromSocket(SocketType socket) {
        return static_cast<intptr_t>(socket);
    }

    bool ensureSockets() {
#if defined(_WIN32)
        static const bool started = [] {
            WSADATA data;
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        return started;
#else
        return true;
#endif
    }

    bool setOption(SocketType socket, int level, int name, int value) {
        return setsockopt(socket, level, name, reinterpret_cast<const char *>(&value), sizeof(value)) == 0;
    }

    void setStreamOptions(SocketType socket) {
#if defined(SO_NOSIGPIPE)
        setOption(socket, SOL_SOCKET, SO_NOSIGPIPE, 1); // a send to a closed peer is an error, not a signal
#endif
        setOption(socket, IPPROTO_TCP, TCP_NODELAY, 1); // small messages leave at once (setNoDelay(false) turns Nagle on)
    }

    Error mapError(int error) {
#if defined(_WIN32)
        switch (error) {
            case WSAEADDRINUSE: return Error::AddressInUse;
            case WSAECONNREFUSED: return Error::ConnectionRefused;
            case WSAETIMEDOUT: return Error::Timeout;
            case WSAEADDRNOTAVAIL:
            case WSAEINVAL:
            case WSAEFAULT: return Error::InvalidAddress;
            case WSAEAFNOSUPPORT:
            case WSAEPROTONOSUPPORT:
            case WSAESOCKTNOSUPPORT:
            case WSAEMFILE:
            case WSAENOBUFS:
            case WSAEACCES:
            case WSANOTINITIALISED: return Error::Unavailable;
            case WSAENETDOWN: return Error::Offline;
            default: return Error::ConnectionFailed;
        }
#else
        switch (error) {
            case EADDRINUSE: return Error::AddressInUse;
            case ECONNREFUSED: return Error::ConnectionRefused;
            case ETIMEDOUT: return Error::Timeout;
            case EADDRNOTAVAIL:
            case EINVAL:
            case EFAULT: return Error::InvalidAddress;
            case EAFNOSUPPORT:
            case EPROTONOSUPPORT:
            case EMFILE:
            case ENFILE:
            case ENOBUFS:
            case ENOMEM:
            case EACCES:
            case EPERM: return Error::Unavailable;
            case ENETDOWN: return Error::Offline;
            default: return Error::ConnectionFailed;
        }
#endif
    }

    int addressFamily(Address::Family family) {
        return family == Address::Family::IPv4 ? AF_INET : AF_INET6;
    }

    SockLen toSockaddr(const Address &address, sockaddr_storage &storage) {
        std::memset(&storage, 0, sizeof(storage));
        if (address.family == Address::Family::IPv4) {
            auto *in4 = reinterpret_cast<sockaddr_in *>(&storage);
            in4->sin_family = AF_INET;
            in4->sin_port = htons(address.port);
            std::memcpy(&in4->sin_addr, address.ip.data(), 4);
            return sizeof(sockaddr_in);
        }
        auto *in6 = reinterpret_cast<sockaddr_in6 *>(&storage);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(address.port);
        std::memcpy(&in6->sin6_addr, address.ip.data(), 16);
        in6->sin6_scope_id = address.scope;
        return sizeof(sockaddr_in6);
    }

    // An IPv4 peer of a dual-stack IPv6 socket comes mapped (::ffff:a.b.c.d): reported as IPv4.
    Address fromSockaddr(const sockaddr *generic) {
        static const uint8_t kMappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        Address address;
        if (generic->sa_family == AF_INET) {
            const auto *in4 = reinterpret_cast<const sockaddr_in *>(generic);
            address.family = Address::Family::IPv4;
            std::memcpy(address.ip.data(), &in4->sin_addr, 4);
            address.port = ntohs(in4->sin_port);
        } else if (generic->sa_family == AF_INET6) {
            const auto *in6 = reinterpret_cast<const sockaddr_in6 *>(generic);
            const auto *bytes = reinterpret_cast<const uint8_t *>(&in6->sin6_addr);
            if (std::memcmp(bytes, kMappedPrefix, sizeof(kMappedPrefix)) == 0) {
                address.family = Address::Family::IPv4;
                std::memcpy(address.ip.data(), bytes + 12, 4);
            } else {
                address.family = Address::Family::IPv6;
                std::memcpy(address.ip.data(), bytes, 16);
                address.scope = in6->sin6_scope_id;
            }
            address.port = ntohs(in6->sin6_port);
        }
        return address;
    }

    // The destination as the socket's family sees it: IPv4 through an IPv6 socket goes mapped,
    // IPv6 through an IPv4 socket cannot go.
    std::optional<Address> forSocketFamily(const Address &to, Address::Family socketFamily) {
        if (to.family == socketFamily) { return to; }
        if (socketFamily != Address::Family::IPv6) { return std::nullopt; }
        Address mapped;
        mapped.family = Address::Family::IPv6;
        mapped.ip[10] = 0xff;
        mapped.ip[11] = 0xff;
        std::memcpy(mapped.ip.data() + 12, to.ip.data(), 4);
        mapped.port = to.port;
        return mapped;
    }

    Address localAddressOf(SocketType socket) {
        sockaddr_storage storage{};
        SockLen length = sizeof(storage);
        if (getsockname(socket, reinterpret_cast<sockaddr *>(&storage), &length) != 0) { return {}; }
        return fromSockaddr(reinterpret_cast<const sockaddr *>(&storage));
    }

    // The outcome of a connect in progress: 1 connected, 0 still connecting, -1 failed (the
    // system's error in soError, 0 when it gave none).
    int pollConnect(SocketType socket, int &soError) {
        soError = 0;
#if defined(_WIN32)
        fd_set writable;
        fd_set failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(socket, &writable);
        FD_SET(socket, &failed);
        timeval zero{0, 0};
        const int count = select(0, nullptr, &writable, &failed, &zero);
        if (count == 0) { return 0; }
        if (count < 0) {
            soError = lastError();
            return -1;
        }
        int length = sizeof(soError);
        getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soError), &length);
        if (FD_ISSET(socket, &failed)) { return -1; }
        if (FD_ISSET(socket, &writable)) { return soError == 0 ? 1 : -1; }
        return 0;
#else
        pollfd descriptor{};
        descriptor.fd = socket;
        descriptor.events = POLLOUT;
        const int count = poll(&descriptor, 1, 0);
        if (count == 0) { return 0; }
        if (count < 0) {
            if (isInterrupted(errno)) { return 0; }
            soError = errno;
            return -1;
        }
        socklen_t length = sizeof(soError);
        getsockopt(socket, SOL_SOCKET, SO_ERROR, &soError, &length);
        if (soError != 0) { return -1; }
        if (descriptor.revents & (POLLERR | POLLHUP)) { return -1; }
        return (descriptor.revents & POLLOUT) ? 1 : 0;
#endif
    }

    bool parseNumber(std::string_view text, uint32_t maximum, uint32_t &value) {
        if (text.empty() || text.size() > 10) { return false; }
        uint64_t result = 0;
        for (char c : text) {
            if (c < '0' || c > '9') { return false; }
            result = result * 10 + uint64_t(c - '0');
            if (result > maximum) { return false; }
        }
        value = static_cast<uint32_t>(result);
        return true;
    }

    constexpr size_t kMaxQueuedBytes = 4u << 20;
    constexpr size_t kMaxDatagramBytes = 65507;

} // namespace

/* ------------------------------ Address ------------------------------ */

Address Address::ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
    Address address;
    address.ip[0] = a;
    address.ip[1] = b;
    address.ip[2] = c;
    address.ip[3] = d;
    address.port = port;
    return address;
}

Address Address::any(uint16_t port, Family family) {
    Address address;
    address.family = family;
    address.port = port;
    return address;
}

Address Address::loopback(uint16_t port, Family family) {
    if (family == Family::IPv4) { return ipv4(127, 0, 0, 1, port); }
    Address address;
    address.family = Family::IPv6;
    address.ip[15] = 1;
    address.port = port;
    return address;
}

Address Address::broadcast(uint16_t port) {
    return ipv4(255, 255, 255, 255, port);
}

std::optional<Address> Address::parse(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.remove_prefix(1); }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    if (text.empty()) { return std::nullopt; }
    Address address;
    std::string_view host = text;
    std::string_view portText;
    if (text.front() == '[') { // "[::1]:7777"
        const size_t close = text.find(']');
        if (close == std::string_view::npos) { return std::nullopt; }
        host = text.substr(1, close - 1);
        const std::string_view rest = text.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') { return std::nullopt; }
            portText = rest.substr(1);
            if (portText.empty()) { return std::nullopt; }
        }
        address.family = Family::IPv6;
    } else {
        const auto colons = std::count(text.begin(), text.end(), ':');
        if (colons >= 2) { // a bare IPv6 address, no port
            address.family = Family::IPv6;
        } else if (colons == 1) { // "192.168.1.5:7777"
            const size_t at = text.find(':');
            host = text.substr(0, at);
            portText = text.substr(at + 1);
            if (portText.empty()) { return std::nullopt; }
        }
    }
    if (!portText.empty()) {
        uint32_t port = 0;
        if (!parseNumber(portText, 65535, port)) { return std::nullopt; }
        address.port = static_cast<uint16_t>(port);
    }
    std::string hostText(host);
    if (address.family == Family::IPv6) { // "fe80::1%4": a numeric scope
        const size_t percent = hostText.find('%');
        if (percent != std::string::npos) {
            uint32_t scope = 0;
            if (!parseNumber(std::string_view(hostText).substr(percent + 1), 0xffffffffu, scope)) { return std::nullopt; }
            address.scope = scope;
            hostText.erase(percent);
        }
    }
    ensureSockets();
    if (address.family == Family::IPv4) {
        in_addr in4{};
        if (inet_pton(AF_INET, hostText.c_str(), &in4) != 1) { return std::nullopt; }
        std::memcpy(address.ip.data(), &in4, 4);
    } else {
        in6_addr in6{};
        if (inet_pton(AF_INET6, hostText.c_str(), &in6) != 1) { return std::nullopt; }
        std::memcpy(address.ip.data(), &in6, 16);
    }
    return address;
}

std::string Address::ipString() const {
    ensureSockets();
    char buffer[INET6_ADDRSTRLEN + 1] = {};
    if (family == Family::IPv4) {
        in_addr in4{};
        std::memcpy(&in4, ip.data(), 4);
        inet_ntop(AF_INET, &in4, buffer, sizeof(buffer));
        return buffer;
    }
    in6_addr in6{};
    std::memcpy(&in6, ip.data(), 16);
    inet_ntop(AF_INET6, &in6, buffer, sizeof(buffer));
    std::string text = buffer;
    if (scope != 0) {
        text += '%';
        text += std::to_string(scope);
    }
    return text;
}

std::string Address::toString() const {
    if (family == Family::IPv4) { return ipString() + ":" + std::to_string(port); }
    return "[" + ipString() + "]:" + std::to_string(port);
}

bool Address::isAny() const {
    const size_t length = family == Family::IPv4 ? 4 : 16;
    return std::all_of(ip.begin(), ip.begin() + length, [](uint8_t byte) { return byte == 0; });
}

bool Address::isLoopback() const {
    if (family == Family::IPv4) { return ip[0] == 127; }
    static const std::array<uint8_t, 16> kLoopback6 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    return ip == kLoopback6;
}

/* ------------------------------ resolve ------------------------------ */

PandaAsync::JobHandle resolve(
    PandaAsync::Scheduler &scheduler,
    std::string host,
    uint16_t port,
    std::function<void(std::vector<Address> &&, Error)> complete,
    PandaAsync::GroupId group
) {
    struct Result {
        std::vector<Address> addresses;
        Error error = Error::None;
    };
    PandaAsync::JobDesc desc;
    desc.name = "net.resolve";
    desc.group = group;
    return scheduler.submit<Result>(
        desc,
        [host = std::move(host), port](PandaAsync::Context &) -> Result {
            Result result;
            if (std::optional<Address> numeric = Address::parse(host)) {
                numeric->port = port;
                result.addresses.push_back(*numeric);
                return result;
            }
            ensureSockets();
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM; // one entry per address, not one per protocol
            addrinfo *list = nullptr;
            if (getaddrinfo(host.c_str(), nullptr, &hints, &list) != 0 || list == nullptr) {
                result.error = Error::HostNotFound;
                return result;
            }
            for (const addrinfo *entry = list; entry != nullptr; entry = entry->ai_next) {
                if (entry->ai_family != AF_INET && entry->ai_family != AF_INET6) { continue; }
                Address address = fromSockaddr(entry->ai_addr);
                address.port = port;
                if (std::find(result.addresses.begin(), result.addresses.end(), address) == result.addresses.end()) {
                    result.addresses.push_back(address);
                }
            }
            freeaddrinfo(list);
            // IPv4 first — the usual route to a game server; IPv6 follows as the fallback.
            std::stable_partition(result.addresses.begin(), result.addresses.end(), [](const Address &address) {
                return address.family == Address::Family::IPv4;
            });
            if (result.addresses.empty()) { result.error = Error::HostNotFound; }
            return result;
        },
        [complete = std::move(complete)](Result &&result) {
            if (complete) { complete(std::move(result.addresses), result.error); }
        }
    );
}

/* ------------------------------ UdpSocket ------------------------------ */

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket &&other) noexcept
    : m_socket(other.m_socket)
    , m_local(other.m_local) {
    other.m_socket = -1;
}

UdpSocket &UdpSocket::operator=(UdpSocket &&other) noexcept {
    if (this != &other) {
        close();
        m_socket = other.m_socket;
        m_local = other.m_local;
        other.m_socket = -1;
    }
    return *this;
}

UdpSocket UdpSocket::bind(const Address &local, bool broadcast, Error *error) {
    if (error) { *error = Error::None; }
    UdpSocket result;
    if (!ensureSockets()) {
        if (error) { *error = Error::Unavailable; }
        return result;
    }
    const SocketType socket = ::socket(addressFamily(local.family), SOCK_DGRAM, IPPROTO_UDP);
    if (socket == kInvalidSocket) {
        if (error) { *error = mapError(lastError()); }
        return result;
    }
    setNonBlocking(socket);
    if (local.family == Address::Family::IPv6) { setOption(socket, IPPROTO_IPV6, IPV6_V6ONLY, 0); }
    if (broadcast) { setOption(socket, SOL_SOCKET, SO_BROADCAST, 1); }
    sockaddr_storage storage{};
    const SockLen length = toSockaddr(local, storage);
    if (::bind(socket, reinterpret_cast<const sockaddr *>(&storage), length) != 0) {
        const Error bindError = mapError(lastError());
        closeSocket(socket);
        if (error) { *error = bindError; }
        return result;
    }
    result.m_socket = fromSocket(socket);
    result.m_local = localAddressOf(socket);
    return result;
}

bool UdpSocket::isOpen() const {
    return m_socket != -1;
}

bool UdpSocket::sendTo(const Address &to, std::span<const uint8_t> bytes) {
    if (!isOpen() || bytes.size() > kMaxDatagramBytes) { return false; }
    const std::optional<Address> destination = forSocketFamily(to, m_local.family);
    if (!destination) { return false; }
    sockaddr_storage storage{};
    const SockLen length = toSockaddr(*destination, storage);
    const auto sent = ::sendto(
        toSocket(m_socket), reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()), 0,
        reinterpret_cast<const sockaddr *>(&storage), length
    );
    return sent >= 0 && static_cast<size_t>(sent) == bytes.size();
}

std::optional<Datagram> UdpSocket::receive() {
    if (!isOpen()) { return std::nullopt; }
    static thread_local std::array<uint8_t, 65536> buffer;
    sockaddr_storage storage{};
    SockLen length = sizeof(storage);
    const auto received = ::recvfrom(
        toSocket(m_socket), reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0,
        reinterpret_cast<sockaddr *>(&storage), &length
    );
    // Nothing waits, or the system reports an ICMP error of an earlier send: dropped, that is UDP.
    if (received < 0) { return std::nullopt; }
    Datagram datagram;
    datagram.from = fromSockaddr(reinterpret_cast<const sockaddr *>(&storage));
    datagram.bytes.assign(buffer.begin(), buffer.begin() + received);
    return datagram;
}

Address UdpSocket::localAddress() const {
    return m_local;
}

void UdpSocket::close() {
    if (m_socket != -1) {
        closeSocket(toSocket(m_socket));
        m_socket = -1;
    }
}

/* ------------------------------ TcpStream ------------------------------ */

TcpStream::~TcpStream() {
    close();
}

TcpStream::TcpStream(TcpStream &&other) noexcept
    : m_socket(other.m_socket)
    , m_state(other.m_state)
    , m_error(other.m_error)
    , m_queue(std::move(other.m_queue))
    , m_queueOffset(other.m_queueOffset)
    , m_remote(other.m_remote)
    , m_local(other.m_local) {
    other.m_socket = -1;
    other.m_state = State::Closed;
    other.m_queue.clear();
    other.m_queueOffset = 0;
}

TcpStream &TcpStream::operator=(TcpStream &&other) noexcept {
    if (this != &other) {
        close();
        m_socket = other.m_socket;
        m_state = other.m_state;
        m_error = other.m_error;
        m_queue = std::move(other.m_queue);
        m_queueOffset = other.m_queueOffset;
        m_remote = other.m_remote;
        m_local = other.m_local;
        other.m_socket = -1;
        other.m_state = State::Closed;
        other.m_queue.clear();
        other.m_queueOffset = 0;
    }
    return *this;
}

TcpStream TcpStream::connect(const Address &remote, Error *error) {
    if (error) { *error = Error::None; }
    TcpStream stream;
    stream.m_remote = remote;
    if (!ensureSockets()) {
        stream.m_error = Error::Unavailable;
        if (error) { *error = stream.m_error; }
        return stream;
    }
    const SocketType socket = ::socket(addressFamily(remote.family), SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalidSocket) {
        stream.m_error = mapError(lastError());
        if (error) { *error = stream.m_error; }
        return stream;
    }
    setNonBlocking(socket);
    setStreamOptions(socket);
    sockaddr_storage storage{};
    const SockLen length = toSockaddr(remote, storage);
    if (::connect(socket, reinterpret_cast<const sockaddr *>(&storage), length) == 0) { // loopback can answer at once
        stream.m_socket = fromSocket(socket);
        stream.m_state = State::Connected;
        stream.m_local = localAddressOf(socket);
        return stream;
    }
    const int connectError = lastError();
    if (isInProgress(connectError) || isInterrupted(connectError)) {
        stream.m_socket = fromSocket(socket);
        stream.m_state = State::Connecting;
        return stream;
    }
    closeSocket(socket);
    stream.m_error = mapError(connectError);
    if (error) { *error = stream.m_error; }
    return stream;
}

TcpStream::State TcpStream::state() {
    if (m_state == State::Connecting) {
        int soError = 0;
        const int outcome = pollConnect(toSocket(m_socket), soError);
        if (outcome > 0) {
            m_state = State::Connected;
            m_local = localAddressOf(toSocket(m_socket));
            flush(); // what was queued while connecting
        } else if (outcome < 0) {
            fail(soError != 0 ? mapError(soError) : Error::ConnectionFailed);
        }
    }
    return m_state;
}

Error TcpStream::error() const {
    return m_error;
}

bool TcpStream::send(std::span<const uint8_t> bytes) {
    if (m_state == State::Closed) { return false; }
    if (bytes.empty()) { return true; }
    m_queue.insert(m_queue.end(), bytes.begin(), bytes.end());
    if (m_queue.size() - m_queueOffset > kMaxQueuedBytes) {
        fail(Error::TooLarge);
        return false;
    }
    flush();
    return m_state != State::Closed;
}

std::vector<uint8_t> TcpStream::receive(size_t maxBytes) {
    std::vector<uint8_t> result;
    if (state() != State::Connected) { return result; }
    flush();
    if (m_state != State::Connected) { return result; }
    static thread_local std::array<uint8_t, 65536> buffer;
    while (result.size() < maxBytes) {
        const size_t want = std::min(buffer.size(), maxBytes - result.size());
        const auto received = ::recv(toSocket(m_socket), reinterpret_cast<char *>(buffer.data()), static_cast<int>(want), 0);
        if (received > 0) {
            result.insert(result.end(), buffer.begin(), buffer.begin() + received);
            continue;
        }
        if (received == 0) { // the peer ended the stream: what arrived before is returned, the state is Closed
            closeSocket(toSocket(m_socket));
            m_socket = -1;
            m_state = State::Closed;
            m_error = Error::None;
            m_queue.clear();
            m_queueOffset = 0;
            break;
        }
        const int receiveError = lastError();
        if (isWouldBlock(receiveError)) { break; }
        if (isInterrupted(receiveError)) { continue; }
        fail(mapError(receiveError));
        break;
    }
    return result;
}

void TcpStream::update() {
    state();
    flush();
}

size_t TcpStream::pendingSendBytes() const {
    return m_queue.size() - m_queueOffset;
}

void TcpStream::setNoDelay(bool noDelay) {
    if (m_socket != -1) { setOption(toSocket(m_socket), IPPROTO_TCP, TCP_NODELAY, noDelay ? 1 : 0); }
}

Address TcpStream::remoteAddress() const {
    return m_remote;
}

Address TcpStream::localAddress() const {
    return m_local;
}

void TcpStream::close() {
    if (m_socket != -1) {
        closeSocket(toSocket(m_socket));
        m_socket = -1;
    }
    m_state = State::Closed;
    m_queue.clear();
    m_queueOffset = 0;
}

void TcpStream::flush() {
    if (m_state != State::Connected) { return; }
    while (m_queueOffset < m_queue.size()) {
        const size_t remaining = m_queue.size() - m_queueOffset;
        const auto sent = ::send(
            toSocket(m_socket), reinterpret_cast<const char *>(m_queue.data() + m_queueOffset),
            static_cast<int>(std::min<size_t>(remaining, 1u << 16)), kSendFlags
        );
        if (sent > 0) {
            m_queueOffset += static_cast<size_t>(sent);
            continue;
        }
        if (sent == 0) { break; }
        const int sendError = lastError();
        if (isWouldBlock(sendError)) { break; }
        if (isInterrupted(sendError)) { continue; }
        fail(mapError(sendError));
        return;
    }
    if (m_queueOffset == m_queue.size()) {
        m_queue.clear();
        m_queueOffset = 0;
    } else if (m_queueOffset > (1u << 20)) {
        m_queue.erase(m_queue.begin(), m_queue.begin() + static_cast<std::ptrdiff_t>(m_queueOffset));
        m_queueOffset = 0;
    }
}

void TcpStream::fail(Error error) {
    if (m_socket != -1) {
        closeSocket(toSocket(m_socket));
        m_socket = -1;
    }
    m_state = State::Closed;
    m_error = error;
    m_queue.clear();
    m_queueOffset = 0;
}

/* ------------------------------ TcpListener ------------------------------ */

TcpListener::~TcpListener() {
    close();
}

TcpListener::TcpListener(TcpListener &&other) noexcept
    : m_socket(other.m_socket)
    , m_local(other.m_local) {
    other.m_socket = -1;
}

TcpListener &TcpListener::operator=(TcpListener &&other) noexcept {
    if (this != &other) {
        close();
        m_socket = other.m_socket;
        m_local = other.m_local;
        other.m_socket = -1;
    }
    return *this;
}

TcpListener TcpListener::bind(const Address &local, Error *error, int backlog) {
    if (error) { *error = Error::None; }
    TcpListener listener;
    if (!ensureSockets()) {
        if (error) { *error = Error::Unavailable; }
        return listener;
    }
    const SocketType socket = ::socket(addressFamily(local.family), SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalidSocket) {
        if (error) { *error = mapError(lastError()); }
        return listener;
    }
    setNonBlocking(socket);
#if !defined(_WIN32)
    setOption(socket, SOL_SOCKET, SO_REUSEADDR, 1); // a server restarts on its port while old connections linger in TIME_WAIT
#endif
    if (local.family == Address::Family::IPv6) { setOption(socket, IPPROTO_IPV6, IPV6_V6ONLY, 0); }
    sockaddr_storage storage{};
    const SockLen length = toSockaddr(local, storage);
    if (::bind(socket, reinterpret_cast<const sockaddr *>(&storage), length) != 0 || ::listen(socket, backlog) != 0) {
        const Error bindError = mapError(lastError());
        closeSocket(socket);
        if (error) { *error = bindError; }
        return listener;
    }
    listener.m_socket = fromSocket(socket);
    listener.m_local = localAddressOf(socket);
    return listener;
}

bool TcpListener::isOpen() const {
    return m_socket != -1;
}

std::optional<TcpStream> TcpListener::accept() {
    if (!isOpen()) { return std::nullopt; }
    sockaddr_storage storage{};
    SockLen length = sizeof(storage);
    const SocketType socket = ::accept(toSocket(m_socket), reinterpret_cast<sockaddr *>(&storage), &length);
    if (socket == kInvalidSocket) { return std::nullopt; } // nothing waits, or the peer left before accept
    setNonBlocking(socket);
    setStreamOptions(socket);
    TcpStream stream;
    stream.m_socket = fromSocket(socket);
    stream.m_state = TcpStream::State::Connected;
    stream.m_remote = fromSockaddr(reinterpret_cast<const sockaddr *>(&storage));
    stream.m_local = localAddressOf(socket);
    return stream;
}

Address TcpListener::localAddress() const {
    return m_local;
}

void TcpListener::close() {
    if (m_socket != -1) {
        closeSocket(toSocket(m_socket));
        m_socket = -1;
    }
}

} // namespace Fern::Net
