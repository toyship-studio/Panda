// Smoke test of Fern::Net (context/networking.md, decision 11): a loopback HTTP server inside the
// test, no internet. Checks a request into memory, the HTTP status, a POST body, a download into a
// file with a broken connection and its resume with a SHA-256 check, an integrity failure,
// cancellation, the refused insecure address and the client destructor with a request in flight;
// then the sockets: addresses, resolve, UDP and TCP over loopback. Returns non-zero on failure.

#include <Fern/Net/Http.hpp>
#include <Fern/Net/Socket.hpp>
#include <Foundation/Hash/Sha256.hpp>
#include <PandaAsync/PandaAsync.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <span>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX // windows.h has min/max macros; std::min below
#    include <winsock2.h>
#    include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
static void closeSocket(SocketHandle socket) {
    closesocket(socket);
}
#else
#    include <arpa/inet.h>
#    include <csignal>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
static void closeSocket(SocketHandle socket) {
    close(socket);
}
#endif

using namespace std::chrono_literals;

namespace {

int g_failures = 0;

void check(bool condition, const char *what) {
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

constexpr size_t kPayloadSize = 1u << 20;

std::vector<uint8_t> payload() {
    std::vector<uint8_t> bytes(kPayloadSize);
    for (size_t i = 0; i < bytes.size(); ++i) { bytes[i] = uint8_t((i * 2654435761u) >> 13); }
    return bytes;
}

bool sendAll(SocketHandle socket, const void *data, size_t size) {
    const char *bytes = static_cast<const char *>(data);
    while (size > 0) {
        int sent = int(send(socket, bytes, int(std::min<size_t>(size, 1u << 16)), 0));
        if (sent <= 0) { return false; }
        bytes += sent;
        size -= size_t(sent);
    }
    return true;
}

// A loopback HTTP/1.1 server: one connection per thread, `Connection: close` after every answer.
class TestServer {
public:
    bool start() {
#if defined(_WIN32)
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
        m_payload = payload();
        m_listen = socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen == kInvalidSocket) { return false; }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(m_listen, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) { return false; }
        socklen_t length = sizeof(address);
        getsockname(m_listen, reinterpret_cast<sockaddr *>(&address), &length);
        m_port = ntohs(address.sin_port);
        if (listen(m_listen, 16) != 0) { return false; }
        m_thread = std::thread([this] { acceptLoop(); });
        return true;
    }

    void stop() {
        m_stopping.store(true);
        // Wakes accept(): a connection to ourselves.
        SocketHandle wake = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(m_port);
        connect(wake, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        closeSocket(wake);
        if (m_thread.joinable()) { m_thread.join(); }
        closeSocket(m_listen);
        std::vector<std::thread> connections;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            connections.swap(m_connections);
        }
        for (std::thread &connection : connections) { connection.join(); }
#if defined(_WIN32)
        WSACleanup();
#endif
    }

    std::string url(const std::string &path) const {
        return "http://127.0.0.1:" + std::to_string(m_port) + path;
    }
    const std::vector<uint8_t> &bytes() const {
        return m_payload;
    }
    std::string lastRange() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastRange;
    }
    std::string lastIfRange() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastIfRange;
    }

private:
    void acceptLoop() {
        for (;;) {
            SocketHandle connection = accept(m_listen, nullptr, nullptr);
            if (m_stopping.load()) {
                if (connection != kInvalidSocket) { closeSocket(connection); }
                return;
            }
            if (connection == kInvalidSocket) { continue; }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connections.emplace_back([this, connection] { serve(connection); });
        }
    }

    void serve(SocketHandle connection) {
        std::string head;
        char buffer[4096];
        while (head.find("\r\n\r\n") == std::string::npos && head.size() < 65536) {
            int received = int(recv(connection, buffer, sizeof(buffer), 0));
            if (received <= 0) {
                closeSocket(connection);
                return;
            }
            head.append(buffer, size_t(received));
        }
        size_t headEnd = head.find("\r\n\r\n");
        std::string body = head.substr(headEnd + 4);
        head.resize(headEnd);

        size_t firstSpace = head.find(' ');
        size_t secondSpace = head.find(' ', firstSpace + 1);
        std::string method = head.substr(0, firstSpace);
        std::string target = head.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        std::string path = target.substr(0, target.find('?'));
        std::string query = target.find('?') == std::string::npos ? "" : target.substr(target.find('?') + 1);
        auto header = [&head](const char *name) -> std::string {
            std::string lower = head;
            for (char &c : lower) { c = char(std::tolower(static_cast<unsigned char>(c))); }
            std::string key = std::string("\r\n") + name + ":";
            for (char &c : key) { c = char(std::tolower(static_cast<unsigned char>(c))); }
            size_t position = lower.find(key);
            if (position == std::string::npos) { return ""; }
            position += key.size();
            size_t end = lower.find("\r\n", position);
            std::string value = head.substr(position, end == std::string::npos ? std::string::npos : end - position);
            while (!value.empty() && value.front() == ' ') { value.erase(value.begin()); }
            return value;
        };
        std::string contentLength = header("Content-Length");
        size_t expectedBody = contentLength.empty() ? 0 : size_t(std::stoul(contentLength));
        while (body.size() < expectedBody) {
            int received = int(recv(connection, buffer, sizeof(buffer), 0));
            if (received <= 0) { break; }
            body.append(buffer, size_t(received));
        }

        auto respond = [connection](int status, const std::string &extraHeaders, const std::string &body) {
            std::string response = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK" : status == 206 ? " Partial Content" : " Error") +
                                   "\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" +
                                   extraHeaders + "\r\n" + body;
            sendAll(connection, response.data(), response.size());
        };

        if (path == "/hello") {
            respond(200, "X-Test: 1\r\nContent-Type: text/plain\r\n", "hello from fern");
        } else if (path == "/echo") {
            respond(200, "Content-Type: text/plain\r\n", body);
        } else if (path == "/file") {
            std::string range = header("Range");
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_lastRange = range;
                m_lastIfRange = header("If-Range");
            }
            size_t offset = 0;
            if (range.rfind("bytes=", 0) == 0) { offset = size_t(std::stoul(range.substr(6))); }
            size_t cut = m_payload.size();
            if (query.rfind("cut=", 0) == 0) { cut = size_t(std::stoul(query.substr(4))); }
            std::string response = offset > 0
                                       ? "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + std::to_string(offset) + "-" +
                                             std::to_string(m_payload.size() - 1) + "/" + std::to_string(m_payload.size()) + "\r\n"
                                       : "HTTP/1.1 200 OK\r\n";
            response += "Connection: close\r\nContent-Type: application/octet-stream\r\nETag: \"v1\"\r\nAccept-Ranges: bytes\r\nContent-Length: " +
                        std::to_string(m_payload.size() - offset) + "\r\n\r\n";
            sendAll(connection, response.data(), response.size());
            size_t end = std::min(m_payload.size(), offset + cut);
            sendAll(connection, m_payload.data() + offset, end - offset);
            if (end < m_payload.size()) { std::this_thread::sleep_for(200ms); } // a stall, then the break
        } else if (path == "/slow") {
            // The Content-Type matters: without one, NSURLSession sniffs the type from the first bytes
            // of the body before it delivers the head, and a trickling body holds it back.
            std::string response = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/octet-stream\r\nContent-Length: 10000000\r\n\r\n";
            sendAll(connection, response.data(), response.size());
            auto start = std::chrono::steady_clock::now();
            while (!m_stopping.load() && std::chrono::steady_clock::now() - start < 10s) {
                if (!sendAll(connection, "x", 1)) { break; }
                std::this_thread::sleep_for(20ms);
            }
        } else {
            respond(404, "Content-Type: text/plain\r\n", "nope");
        }
        closeSocket(connection);
    }

    SocketHandle m_listen = kInvalidSocket;
    uint16_t m_port = 0;
    std::thread m_thread;
    std::atomic<bool> m_stopping{false};
    std::mutex m_mutex;
    std::vector<std::thread> m_connections;
    std::vector<uint8_t> m_payload;
    std::string m_lastRange;
    std::string m_lastIfRange;
};

// Runs the completions until the condition holds or the time is up.
bool pump(PandaAsync::Scheduler &scheduler, const std::function<bool()> &done, std::chrono::milliseconds timeout = 10s) {
    auto start = std::chrono::steady_clock::now();
    while (!done()) {
        scheduler.processCompletions(PandaAsync::CompletionBudget{64, 0.0});
        if (std::chrono::steady_clock::now() - start > timeout) { return false; }
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

void testSha256() {
    std::printf("testSha256\n");
    Foundation::Sha256Digest abc = Foundation::Sha256::of("abc", 3);
    check(abc.toHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 of abc");
    Foundation::Sha256Digest empty = Foundation::Sha256::of("", 0);
    check(empty.toHex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 of nothing");
    Foundation::Sha256 incremental;
    std::string million(1000000, 'a');
    for (size_t i = 0; i < million.size(); i += 777) { incremental.update(million.data() + i, std::min<size_t>(777, million.size() - i)); }
    check(incremental.finish().toHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 of a million a");
    check(Foundation::Sha256Digest::fromHex(abc.toHex()) == abc, "sha256 hex round trip");
}

void testRequests(TestServer &server, PandaAsync::Scheduler &scheduler, Fern::Net::Client &client) {
    std::printf("testRequests\n");
    bool done = false;
    Fern::Net::Request request;
    request.url = server.url("/hello");
    client.send(request, [&](Fern::Net::Response &&response) {
        check(scheduler.isMainThread(), "complete runs on the main thread");
        check(response.ok(), "GET /hello ok");
        check(response.status == 200, "GET /hello status 200");
        check(response.bodyText() == "hello from fern", "GET /hello body");
        check(response.header("x-test") && response.header("x-test")->value == "1", "GET /hello header");
        done = true;
    });
    check(pump(scheduler, [&] { return done; }), "GET /hello completed");

    done = false;
    request.url = server.url("/missing");
    client.send(request, [&](Fern::Net::Response &&response) {
        check(response.error == Fern::Net::Error::None && response.status == 404, "404 is a status, not an error");
        check(!response.ok(), "404 is not ok");
        done = true;
    });
    check(pump(scheduler, [&] { return done; }), "GET /missing completed");

    done = false;
    Fern::Net::Request post;
    post.method = Fern::Net::Method::Post;
    post.url = server.url("/echo");
    std::string text = "field=value&other=1";
    post.body.assign(text.begin(), text.end());
    client.send(post, [&](Fern::Net::Response &&response) {
        check(response.ok() && response.bodyText() == text, "POST /echo echoes the body");
        done = true;
    });
    check(pump(scheduler, [&] { return done; }), "POST /echo completed");
}

void testDownload(TestServer &server, PandaAsync::Scheduler &scheduler, Fern::Net::Client &client) {
    std::printf("testDownload\n");
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "fern-net-smoke";
    std::filesystem::remove_all(directory);
    std::filesystem::path target = directory / "payload.bin";
    std::filesystem::path part = directory / "payload.bin.part";
    const std::vector<uint8_t> &bytes = server.bytes();
    Foundation::Sha256Digest digest = Foundation::Sha256::of(bytes.data(), bytes.size());

    // The connection breaks after 300000 bytes: the .part stays for a resume.
    bool done = false;
    Fern::Net::Request request;
    request.url = server.url("/file?cut=300000");
    request.downloadTo = target;
    request.expectedSize = bytes.size();
    request.expectedSha256 = digest;
    client.send(request, [&](Fern::Net::Response &&response) {
        check(!response.ok(), "a broken download fails");
        check(response.error == Fern::Net::Error::ConnectionFailed || response.error == Fern::Net::Error::Protocol,
              "a broken download reports the connection");
        done = true;
    });
    check(pump(scheduler, [&] { return done; }), "broken download completed");
    check(!std::filesystem::exists(target), "no file after a broken download");
    // How much of the body a stack hands over before it reports the break differs between the
    // platforms (it may be nothing). The resume is tested from the same point everywhere: the
    // .part is set to the first 300000 bytes, with the validator the first answer carried.
    uint64_t partSize = std::filesystem::exists(part) ? std::filesystem::file_size(part) : 0;
    check(partSize <= 300000, ".part holds at most the bytes the server sent");
    std::filesystem::create_directories(directory);
    if (FILE *file = std::fopen(part.string().c_str(), "wb")) {
        std::fwrite(bytes.data(), 1, 300000, file);
        std::fclose(file);
    }
    if (FILE *file = std::fopen((part.string() + ".validator").c_str(), "wb")) {
        std::fputs("\"v1\"\n", file);
        std::fclose(file);
    }
    partSize = 300000;

    // The resume asks for the rest and checks the whole file.
    done = false;
    uint64_t lastReceived = 0;
    request.url = server.url("/file");
    request.resume = true;
    client.send(
        request,
        [&](Fern::Net::Response &&response) {
            check(response.ok(), "resumed download ok");
            check(response.status == 206, "resumed download got 206");
            done = true;
        },
        [&](const Fern::Net::Progress &progress) { lastReceived = progress.received; }
    );
    check(pump(scheduler, [&] { return done; }), "resumed download completed");
    check(server.lastRange() == "bytes=" + std::to_string(partSize) + "-", "resume sent the Range header");
    check(server.lastIfRange() == "\"v1\"", "resume sent the validator");
    check(std::filesystem::exists(target) && std::filesystem::file_size(target) == bytes.size(), "the file is complete");
    check(!std::filesystem::exists(part), "the .part is gone");
    check(lastReceived == bytes.size(), "progress reached the size");
    if (std::filesystem::exists(target)) {
        std::vector<uint8_t> written(bytes.size());
        FILE *file = std::fopen(target.string().c_str(), "rb");
        size_t read = file ? std::fread(written.data(), 1, written.size(), file) : 0;
        if (file) { std::fclose(file); }
        check(read == bytes.size() && written == bytes, "the file has the bytes of the server");
    }

    // A wrong hash: no file, no .part (a failed download never touches an existing file, so the
    // file of the previous download is removed first).
    std::filesystem::remove(target);
    done = false;
    Fern::Net::Request wrong = request;
    wrong.resume = false;
    wrong.expectedSha256 = Foundation::Sha256::of("other", 5);
    client.send(wrong, [&](Fern::Net::Response &&response) {
        check(response.error == Fern::Net::Error::IntegrityFailed, "a wrong hash is an integrity failure");
        done = true;
    });
    check(pump(scheduler, [&] { return done; }), "integrity check completed");
    check(!std::filesystem::exists(target) && !std::filesystem::exists(part), "nothing left after an integrity failure");
    std::filesystem::remove_all(directory);
}

void testCancelAndPolicy(TestServer &server, PandaAsync::Scheduler &scheduler, Fern::Net::Client &client) {
    std::printf("testCancelAndPolicy\n");
    bool completed = false;
    int progressCalls = 0;
    Fern::Net::Request slow;
    slow.url = server.url("/slow");
    Fern::Net::RequestHandle handle = client.send(
        slow, [&](Fern::Net::Response &&) { completed = true; }, [&](const Fern::Net::Progress &) { ++progressCalls; }
    );
    check(pump(scheduler, [&] { return progressCalls > 0; }), "the slow request reports progress");
    handle.cancel();
    pump(scheduler, [] { return false; }, 300ms);
    check(!completed, "a canceled request never completes");
    check(!handle.isActive(), "a canceled request is not active");

    bool done = false;
    Fern::Net::Request insecure;
    insecure.url = "http://example.com/";
    client.send(insecure, [&](Fern::Net::Response &&response) {
        check(response.error == Fern::Net::Error::InsecureUrl, "http:// outside localhost is refused");
        done = true;
    });
    check(pump(scheduler, [&] { return done; }), "insecure address completed");

    done = false;
    Fern::Net::Request invalid;
    invalid.url = "not a url";
    client.send(invalid, [&](Fern::Net::Response &&response) {
        check(response.error == Fern::Net::Error::InvalidUrl, "a malformed address is invalid");
        done = true;
    });
    check(pump(scheduler, [&] { return done; }), "invalid address completed");
}

void testDestructorInFlight(TestServer &server, PandaAsync::Scheduler &scheduler) {
    std::printf("testDestructorInFlight\n");
    bool completed = false;
    auto start = std::chrono::steady_clock::now();
    {
        Fern::Net::Client client(scheduler);
        Fern::Net::Request slow;
        slow.url = server.url("/slow");
        int progressCalls = 0;
        client.send(slow, [&](Fern::Net::Response &&) { completed = true; }, [&](const Fern::Net::Progress &) { ++progressCalls; });
        pump(scheduler, [&] { return progressCalls > 0; });
    }
    check(std::chrono::steady_clock::now() - start < 5s, "the destructor returns promptly");
    pump(scheduler, [] { return false; }, 100ms);
    check(!completed, "no completion after the client is gone");
}

std::span<const uint8_t> bytesOf(std::string_view text) {
    return {reinterpret_cast<const uint8_t *>(text.data()), text.size()};
}

std::string textOf(const std::vector<uint8_t> &bytes) {
    return std::string(bytes.begin(), bytes.end());
}

// Polls until the condition holds or the time is up.
bool waitFor(const std::function<bool()> &done, std::chrono::milliseconds timeout = 5s) {
    auto start = std::chrono::steady_clock::now();
    while (!done()) {
        if (std::chrono::steady_clock::now() - start > timeout) { return false; }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

void testSockets(PandaAsync::Scheduler &scheduler) {
    using namespace Fern::Net;
    using State = TcpStream::State;

    // Addresses
    std::optional<Address> v4 = Address::parse("192.168.1.5:7777");
    check(v4 && v4->family == Address::Family::IPv4 && v4->port == 7777 && v4->toString() == "192.168.1.5:7777",
          "an IPv4 address with a port parses and prints");
    std::optional<Address> bare4 = Address::parse("10.0.0.1");
    check(bare4 && bare4->port == 0 && bare4->ipString() == "10.0.0.1", "an IPv4 address without a port parses");
    std::optional<Address> v6 = Address::parse("[::1]:8080");
    check(v6 && v6->family == Address::Family::IPv6 && v6->port == 8080 && v6->isLoopback() && v6->toString() == "[::1]:8080",
          "an IPv6 address with a port parses and prints");
    std::optional<Address> bare6 = Address::parse("fe80::1%4");
    check(bare6 && bare6->family == Address::Family::IPv6 && bare6->port == 0 && bare6->scope == 4,
          "a bare IPv6 address with a scope parses");
    check(!Address::parse("example.com:80") && !Address::parse("1.2.3.4:99999") && !Address::parse("1.2.3.4:") &&
              !Address::parse(""),
          "names, bad ports and empty text are not addresses");
    check(Address::loopback(1).isLoopback() && Address::any(0).isAny() && !Address::loopback(1).isAny(), "loopback and any");

    // resolve: a numeric address and localhost (no internet)
    {
        bool done = false;
        std::vector<Address> addresses;
        Error error = Error::None;
        resolve(scheduler, "127.0.0.1", 7777, [&](std::vector<Address> &&result, Error e) {
            addresses = std::move(result);
            error = e;
            done = true;
        });
        check(pump(scheduler, [&] { return done; }), "resolve of a numeric address completed");
        check(error == Error::None && addresses.size() == 1 && addresses[0].isLoopback() && addresses[0].port == 7777,
              "a numeric address resolves to itself");
        done = false;
        addresses.clear();
        resolve(scheduler, "localhost", 80, [&](std::vector<Address> &&result, Error e) {
            addresses = std::move(result);
            error = e;
            done = true;
        });
        check(pump(scheduler, [&] { return done; }), "resolve of localhost completed");
        check(error == Error::None && !addresses.empty() &&
                  std::all_of(addresses.begin(), addresses.end(), [](const Address &a) { return a.isLoopback() && a.port == 80; }),
              "localhost resolves to loopback addresses");
    }

    // UDP
    {
        Error error = Error::None;
        UdpSocket a = UdpSocket::bind(Address::loopback(0), false, &error);
        check(a.isOpen() && error == Error::None && a.localAddress().port != 0 && a.localAddress().isLoopback(),
              "a UDP socket binds to a free port");
        UdpSocket b = UdpSocket::bind(Address::loopback(0));
        check(b.isOpen() && !b.receive(), "a UDP socket has nothing before a send");
        check(a.sendTo(b.localAddress(), bytesOf("ping")), "a datagram is sent");
        std::optional<Datagram> got;
        waitFor([&] {
            got = b.receive();
            return got.has_value();
        });
        check(got && textOf(got->bytes) == "ping" && got->from == a.localAddress(), "the datagram arrives with the sender's address");
        UdpSocket busy = UdpSocket::bind(a.localAddress(), false, &error);
        check(!busy.isOpen() && error == Error::AddressInUse, "a taken port is AddressInUse");
        UdpSocket broadcaster = UdpSocket::bind(Address::any(0), true, &error);
        check(broadcaster.isOpen() && error == Error::None, "a broadcast socket binds");
        UdpSocket moved = std::move(a);
        check(moved.isOpen() && !a.isOpen(), "a UDP socket moves"); // NOLINT(bugprone-use-after-move): the point of the check
        moved.close();
        check(!moved.isOpen() && !moved.sendTo(b.localAddress(), bytesOf("x")), "a closed UDP socket sends nothing");
    }

    // TCP
    {
        Error error = Error::None;
        TcpListener listener = TcpListener::bind(Address::loopback(0), &error);
        check(listener.isOpen() && error == Error::None && listener.localAddress().port != 0, "a TCP listener binds");
        check(!listener.accept(), "accept has nothing before a connect");
        TcpStream client = TcpStream::connect(listener.localAddress(), &error);
        check(error == Error::None && client.isOpen(), "a TCP connect starts");
        std::optional<TcpStream> server;
        waitFor([&] {
            server = listener.accept();
            return server.has_value();
        });
        check(server.has_value(), "the listener accepts the connection");
        waitFor([&] { return client.state() == State::Connected; });
        check(client.state() == State::Connected && client.remoteAddress() == listener.localAddress(),
              "the client is connected to the listener");
        check(server && server->state() == State::Connected && server->remoteAddress() == client.localAddress(),
              "the accepted stream knows its peer");
        check(client.send(bytesOf("hello server")), "a TCP send is accepted");
        std::vector<uint8_t> got;
        waitFor([&] {
            std::vector<uint8_t> chunk = server->receive();
            got.insert(got.end(), chunk.begin(), chunk.end());
            return got.size() >= 12;
        });
        check(textOf(got) == "hello server", "the server receives the bytes");
        // A megabyte in one send: it leaves in pieces as the system takes them and arrives in order.
        const std::vector<uint8_t> big = payload();
        check(server->send(big), "a large send is queued");
        std::vector<uint8_t> received;
        waitFor(
            [&] {
                server->update();
                std::vector<uint8_t> chunk = client.receive();
                received.insert(received.end(), chunk.begin(), chunk.end());
                return received.size() >= big.size();
            },
            10s
        );
        check(received == big, "a megabyte arrives whole and in order");
        check(server->pendingSendBytes() == 0, "the send queue drained");
        server->close();
        waitFor([&] {
            client.receive();
            return client.state() == State::Closed;
        });
        check(client.state() == State::Closed && client.error() == Error::None, "the peer's close ends the stream without an error");
        check(!client.send(bytesOf("x")) && client.receive().empty(), "a closed stream sends and receives nothing");
        // Nothing listens any more: the connect fails.
        const Address closedPort = listener.localAddress();
        listener.close();
        TcpStream refused = TcpStream::connect(closedPort, &error);
        waitFor([&] { return refused.state() == State::Closed; });
        check(refused.state() == State::Closed &&
                  (refused.error() == Error::ConnectionRefused || refused.error() == Error::ConnectionFailed),
              "a connect to a closed port fails");
    }
}

} // namespace

int main() {
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN); // the test server writes to connections the client closed
#endif
    testSha256();

    TestServer server;
    if (!server.start()) {
        std::printf("  FAIL: the loopback server did not start\n");
        return 1;
    }
    PandaAsync::Scheduler scheduler;
    scheduler.registerMainThread();
    testSockets(scheduler);
    {
        Fern::Net::Client client(scheduler);
        testRequests(server, scheduler, client);
        testDownload(server, scheduler, client);
        testCancelAndPolicy(server, scheduler, client);
    }
    testDestructorInFlight(server, scheduler);
    server.stop();

    if (g_failures == 0) {
        std::printf("FernNetSmoke: OK\n");
        return 0;
    }
    std::printf("FernNetSmoke: %d failure(s)\n", g_failures);
    return 1;
}
