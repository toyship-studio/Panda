#include "Bamboo/Net.hpp"

#include "Bamboo/Async.hpp"
#include "Bamboo/Internal/MainThread.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

namespace Bamboo::Net {

namespace Detail {

    using Clock = std::chrono::steady_clock;
    constexpr std::chrono::seconds CONNECT_TIMEOUT{15}; // per address, as the connect timeout of HTTP
    constexpr size_t MAX_PENDING_BYTES = 4u << 20;     // sent before the connection opened

    uint64_t nextId() {
        static uint64_t next = 1;
        return next++;
    }

    void append(std::vector<uint8_t> &to, Bytes bytes) {
        to.insert(to.end(), bytes.begin(), bytes.end());
    }

    // The client side of a connection, or a client of a server (born connected, pumped by the
    // server). The callbacks never run inside the call that caused them: everything is reported
    // from pump(), once per frame.
    struct TcpConnectionState {
        uint64_t id = 0;
        Fern::Net::TcpStream stream;
        TcpCallbacks callbacks;
        std::vector<Address> candidates; // the addresses of the host, tried in order
        size_t nextCandidate = 0;
        std::vector<uint8_t> pending; // sent before the connection opened; kept for a retry on the next address
        Clock::time_point started;
        Error failure = Error::None;
        bool resolving = false;
        bool failed = false;    // every address failed: closed(failure) comes with the next pump
        bool announced = false; // connected was reported
        bool closed = false;

        bool send(Bytes bytes) {
            if (closed || failed) { return false; }
            if (stream.isOpen()) {
                if (!stream.send(bytes)) { return false; }
                if (!announced) { append(pending, bytes); } // a copy for a retry on the next address
                return true;
            }
            if (announced) { return false; } // the connection ended
            append(pending, bytes);
            if (pending.size() > MAX_PENDING_BYTES) {
                fail(Error::TooLarge);
                return false;
            }
            return true;
        }

        void tryNext() {
            while (nextCandidate < candidates.size()) {
                Error error = Error::None;
                stream = Fern::Net::TcpStream::connect(candidates[nextCandidate++], &error);
                if (stream.isOpen()) {
                    started = Clock::now();
                    if (!pending.empty()) { stream.send(pending); }
                    return;
                }
                failure = error != Error::None ? error : stream.error();
            }
            fail(failure == Error::None ? Error::ConnectionFailed : failure);
        }

        void fail(Error error) {
            failed = true;
            failure = error;
            stream.close();
        }

        // The end from the system or the peer: the callback, once.
        void finish(Error error) {
            if (closed) { return; }
            closed = true;
            stream.close();
            pending.clear();
            std::function<void(Error)> onClosed = std::move(callbacks.closed);
            callbacks = {};
            if (onClosed) { onClosed(error); }
        }

        // By the game: silent.
        void close() {
            closed = true;
            stream.close();
            pending.clear();
            callbacks = {};
        }

        void pump(Clock::time_point now) {
            if (closed || resolving) { return; }
            if (failed) {
                finish(failure);
                return;
            }
            using State = Fern::Net::TcpStream::State;
            State state = stream.state();
            if (state == State::Connecting) {
                if (now - started > CONNECT_TIMEOUT) {
                    stream.close();
                    failure = Error::Timeout;
                    tryNext();
                }
                return;
            }
            if (state == State::Connected) {
                if (!announced) {
                    announced = true;
                    pending.clear();
                    pending.shrink_to_fit();
                    if (callbacks.connected) { callbacks.connected(); }
                    if (closed) { return; }
                }
                std::vector<uint8_t> bytes = stream.receive();
                if (!bytes.empty() && callbacks.received) {
                    callbacks.received(std::move(bytes));
                    if (closed) { return; }
                }
                state = stream.currentState();
            }
            if (state == State::Closed) {
                if (announced) {
                    finish(stream.error());
                } else { // the connect failed: the next address of the host
                    failure = stream.error();
                    tryNext();
                }
            }
        }
    };

    struct TcpServerState {
        uint64_t id = 0;
        Fern::Net::TcpListener listener;
        TcpServerCallbacks callbacks;
        std::vector<std::shared_ptr<TcpConnectionState>> clients;
        bool closed = false;

        void close() {
            closed = true;
            listener.close();
            for (const std::shared_ptr<TcpConnectionState> &client : clients) { client->close(); }
            clients.clear();
            callbacks = {};
        }

        void pump() {
            if (closed) { return; }
            while (std::optional<Fern::Net::TcpStream> accepted = listener.accept()) {
                auto client = std::make_shared<TcpConnectionState>();
                client->id = nextId();
                client->stream = std::move(*accepted);
                client->announced = true;
                clients.push_back(client);
                if (callbacks.connected) { callbacks.connected(TcpConnection(client)); }
                if (closed) { return; }
            }
            for (size_t i = 0; i < clients.size(); ++i) { // by index: a callback may close clients
                std::shared_ptr<TcpConnectionState> client = clients[i];
                if (client->closed) { continue; }
                std::vector<uint8_t> bytes = client->stream.receive();
                if (!bytes.empty() && callbacks.received) {
                    callbacks.received(TcpConnection(client), std::move(bytes));
                    if (closed) { return; }
                    if (client->closed) { continue; }
                }
                if (client->stream.currentState() == Fern::Net::TcpStream::State::Closed) {
                    client->closed = true;
                    if (callbacks.disconnected) { callbacks.disconnected(TcpConnection(client), client->stream.error()); }
                    if (closed) { return; }
                }
            }
            std::erase_if(clients, [](const std::shared_ptr<TcpConnectionState> &client) { return client->closed; });
        }
    };

    struct UdpSocketState {
        uint64_t id = 0;
        Fern::Net::UdpSocket socket;
        UdpSocket::Received received;
        bool closed = false;

        void close() {
            closed = true;
            socket.close();
            received = nullptr;
        }

        void pump() {
            if (closed) { return; }
            for (int count = 0; count < 512; ++count) { // a bound per frame: a flood does not stall the game
                std::optional<Datagram> datagram = socket.receive();
                if (!datagram) { break; }
                if (received) { received(std::move(*datagram)); }
                if (closed) { return; }
            }
        }
    };

} // namespace Detail

namespace {

    struct Runtime {
        std::unique_ptr<Fern::Net::Client> http; // created with the first request, on the scheduler of the bundle
        std::string userAgent;
        std::vector<std::weak_ptr<Detail::TcpConnectionState>> connections;
        std::vector<std::weak_ptr<Detail::TcpServerState>> servers;
        std::vector<std::weak_ptr<Detail::UdpSocketState>> sockets;
    };

    Runtime &runtime() {
        static Runtime instance;
        return instance;
    }

    Fern::Net::Client &httpClient() {
        Runtime &state = runtime();
        if (!state.http) {
            Fern::Net::ClientDesc desc;
            desc.userAgent = state.userAgent;
            state.http = std::make_unique<Fern::Net::Client>(Async::Internal::scheduler(), desc);
        }
        return *state.http;
    }

    bool mainThread(const char *api) {
        return Bamboo::Internal::requireMainThreadApi(api);
    }

    // The handles own the objects; the runtime only drives the live ones. A callback may open or
    // close objects, so the pass runs over a snapshot.
    template<typename TState, typename TPump>
    void pumpAll(std::vector<std::weak_ptr<TState>> &list, TPump &&pump) {
        const std::vector<std::weak_ptr<TState>> snapshot = list;
        for (const std::weak_ptr<TState> &weak : snapshot) {
            if (std::shared_ptr<TState> state = weak.lock()) {
                if (!state->closed) { pump(*state); }
            }
        }
        std::erase_if(list, [](const std::weak_ptr<TState> &weak) {
            const std::shared_ptr<TState> state = weak.lock();
            return !state || state->closed;
        });
    }

    template<typename TState>
    void closeAll(std::vector<std::weak_ptr<TState>> &list) {
        for (const std::weak_ptr<TState> &weak : list) {
            if (std::shared_ptr<TState> state = weak.lock()) { state->close(); }
        }
        list.clear();
    }

} // namespace

/* ------------------------------ HTTP ------------------------------ */

namespace Http {

    RequestHandle send(Request request, Complete complete, OnProgress progress) {
        if (!mainThread("Net::Http::send")) { return {}; }
        return httpClient().send(std::move(request), std::move(complete), std::move(progress));
    }

    RequestHandle get(std::string url, Complete complete) {
        Request request;
        request.url = std::move(url);
        return send(std::move(request), std::move(complete));
    }

    RequestHandle post(std::string url, std::string body, std::string contentType, Complete complete) {
        Request request;
        request.method = Method::Post;
        request.url = std::move(url);
        request.body.assign(body.begin(), body.end());
        request.headers.push_back({"Content-Type", std::move(contentType)});
        return send(std::move(request), std::move(complete));
    }

    RequestHandle download(std::string url, std::filesystem::path to, Complete complete, OnProgress progress) {
        Request request;
        request.url = std::move(url);
        request.downloadTo = std::move(to);
        return send(std::move(request), std::move(complete), std::move(progress));
    }

    void cancelAll() {
        if (!mainThread("Net::Http::cancelAll")) { return; }
        if (runtime().http) { runtime().http->cancelAll(); }
    }

    void setUserAgent(std::string userAgent) {
        runtime().userAgent = std::move(userAgent);
    }

} // namespace Http

/* ------------------------------ Names ------------------------------ */

void resolve(std::string host, uint16_t port, std::function<void(std::vector<Address> &&, Error)> complete) {
    if (!mainThread("Net::resolve")) { return; }
    Fern::Net::resolve(Async::Internal::scheduler(), std::move(host), port, std::move(complete));
}

/* ------------------------------ TcpConnection ------------------------------ */

TcpConnection::TcpConnection(std::shared_ptr<Detail::TcpConnectionState> state)
    : m_state(std::move(state)) {}

TcpConnection TcpConnection::connect(std::string host, uint16_t port, TcpCallbacks callbacks) {
    if (!mainThread("Net::TcpConnection::connect")) { return {}; }
    auto state = std::make_shared<Detail::TcpConnectionState>();
    state->id = Detail::nextId();
    state->callbacks = std::move(callbacks);
    runtime().connections.push_back(state);
    if (std::optional<Address> address = Address::parse(host)) {
        address->port = port;
        state->candidates.push_back(*address);
        state->tryNext();
    } else {
        state->resolving = true;
        std::weak_ptr<Detail::TcpConnectionState> weak = state;
        Fern::Net::resolve(
            Async::Internal::scheduler(), std::move(host), port,
            [weak](std::vector<Address> &&addresses, Error error) {
                std::shared_ptr<Detail::TcpConnectionState> resolved = weak.lock();
                if (!resolved || resolved->closed) { return; }
                resolved->resolving = false;
                if (addresses.empty()) {
                    resolved->fail(error == Error::None ? Error::HostNotFound : error);
                    return;
                }
                resolved->candidates = std::move(addresses);
                resolved->tryNext();
            }
        );
    }
    return TcpConnection(std::move(state));
}

bool TcpConnection::send(Bytes bytes) {
    if (!m_state || !mainThread("Net::TcpConnection::send")) { return false; }
    return m_state->send(bytes);
}

void TcpConnection::close() {
    if (m_state && mainThread("Net::TcpConnection::close")) { m_state->close(); }
}

bool TcpConnection::isConnecting() const {
    return m_state && !m_state->closed && !m_state->announced;
}

bool TcpConnection::isConnected() const {
    return m_state && !m_state->closed && m_state->announced &&
           m_state->stream.currentState() == Fern::Net::TcpStream::State::Connected;
}

bool TcpConnection::isOpen() const {
    return m_state && !m_state->closed;
}

Address TcpConnection::remote() const {
    return m_state ? m_state->stream.remoteAddress() : Address{};
}

uint64_t TcpConnection::id() const {
    return m_state ? m_state->id : 0;
}

/* ------------------------------ TcpServer ------------------------------ */

TcpServer TcpServer::listen(uint16_t port, TcpServerCallbacks callbacks, Error *error) {
    if (error) { *error = Error::None; }
    if (!mainThread("Net::TcpServer::listen")) {
        if (error) { *error = Error::Unavailable; }
        return {};
    }
    // IPv6 with IPv4 mapped in (dual stack) where the system allows it, IPv4 alone otherwise.
    Error bindError = Error::None;
    Fern::Net::TcpListener listener = Fern::Net::TcpListener::bind(Address::any(port, Address::Family::IPv6), &bindError);
    if (!listener.isOpen() && bindError != Error::AddressInUse) {
        listener = Fern::Net::TcpListener::bind(Address::any(port), &bindError);
    }
    if (error) { *error = bindError; }
    if (!listener.isOpen()) { return {}; }
    auto state = std::make_shared<Detail::TcpServerState>();
    state->id = Detail::nextId();
    state->listener = std::move(listener);
    state->callbacks = std::move(callbacks);
    runtime().servers.push_back(state);
    TcpServer server;
    server.m_state = std::move(state);
    return server;
}

void TcpServer::close() {
    if (m_state && mainThread("Net::TcpServer::close")) { m_state->close(); }
}

bool TcpServer::isListening() const {
    return m_state && !m_state->closed;
}

uint16_t TcpServer::port() const {
    return m_state ? m_state->listener.localAddress().port : 0;
}

std::vector<TcpConnection> TcpServer::clients() const {
    std::vector<TcpConnection> result;
    if (!m_state) { return result; }
    for (const std::shared_ptr<Detail::TcpConnectionState> &client : m_state->clients) {
        if (!client->closed) { result.push_back(TcpConnection(client)); }
    }
    return result;
}

/* ------------------------------ UdpSocket ------------------------------ */

UdpSocket UdpSocket::open(uint16_t port, Received received, bool broadcast, Error *error) {
    return open(Address::any(port), std::move(received), broadcast, error);
}

UdpSocket UdpSocket::open(const Address &local, Received received, bool broadcast, Error *error) {
    if (error) { *error = Error::None; }
    if (!mainThread("Net::UdpSocket::open")) {
        if (error) { *error = Error::Unavailable; }
        return {};
    }
    Error bindError = Error::None;
    Fern::Net::UdpSocket socket = Fern::Net::UdpSocket::bind(local, broadcast, &bindError);
    if (error) { *error = bindError; }
    if (!socket.isOpen()) { return {}; }
    auto state = std::make_shared<Detail::UdpSocketState>();
    state->id = Detail::nextId();
    state->socket = std::move(socket);
    state->received = std::move(received);
    runtime().sockets.push_back(state);
    UdpSocket result;
    result.m_state = std::move(state);
    return result;
}

bool UdpSocket::sendTo(const Address &to, Bytes bytes) {
    if (!m_state || m_state->closed || !mainThread("Net::UdpSocket::sendTo")) { return false; }
    return m_state->socket.sendTo(to, bytes);
}

bool UdpSocket::broadcast(uint16_t port, Bytes bytes) {
    return sendTo(Address::broadcast(port), bytes);
}

void UdpSocket::close() {
    if (m_state && mainThread("Net::UdpSocket::close")) { m_state->close(); }
}

bool UdpSocket::isOpen() const {
    return m_state && !m_state->closed;
}

Address UdpSocket::local() const {
    return m_state ? m_state->socket.localAddress() : Address{};
}

uint16_t UdpSocket::port() const {
    return local().port;
}

/* ------------------------------ Internal ------------------------------ */

namespace Internal {

    void update() {
        Runtime &state = runtime();
        const Detail::Clock::time_point now = Detail::Clock::now();
        pumpAll(state.connections, [now](Detail::TcpConnectionState &connection) { connection.pump(now); });
        pumpAll(state.servers, [](Detail::TcpServerState &server) { server.pump(); });
        pumpAll(state.sockets, [](Detail::UdpSocketState &socket) { socket.pump(); });
    }

    void cancelAll() {
        Runtime &state = runtime();
        if (state.http) { state.http->cancelAll(); }
        closeAll(state.connections);
        closeAll(state.servers);
        closeAll(state.sockets);
    }

    void shutdown() {
        cancelAll();
        runtime().http.reset(); // waits for the stack to let go; the scheduler it posted to is still alive
    }

} // namespace Internal

} // namespace Bamboo::Net
