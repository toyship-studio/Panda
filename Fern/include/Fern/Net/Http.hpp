#pragma once

// HTTP of the network layer of Fern (context/networking.md, section 3).
//
// A Client sends requests on the network stack of the system (NSURLSession, WinHTTP, libcurl) and
// brings every answer to the main thread through the PandaAsync scheduler it was created with:
// `complete` and `progress` run inside processCompletions, like the completion of a job. The
// threads of the stack never call user code. A canceled request never calls `complete`; a failed
// one does, with the error. An HTTP status is not an error: a 404 arrives as error None and
// status 404, `Response::ok()` is the usual check.
//
// Only https:// (and http:// to localhost) unless the request allows insecure addresses; the
// certificate check of the system cannot be turned off. A download into a file goes to
// `<path>.part`, is hashed while it arrives, and takes its name only after the size and the
// SHA-256 the caller expects have matched; `resume` continues a `.part` with Range / If-Range.

#include <Fern/Net/Error.hpp>
#include <Foundation/Hash/Sha256.hpp>
#include <PandaAsync/PandaAsync.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Fern::Net {

enum class Method : uint8_t { Get, Head, Post, Put, Patch, Delete };
const char *methodName(Method method);

// Error and errorName: Fern/Net/Error.hpp (shared with the sockets).

struct Header {
    std::string name;
    std::string value;
};

struct Timeouts {
    double connectSeconds = 15;
    double idleSeconds = 30; // no byte for this long (on Apple platforms it covers connecting too)
    double totalSeconds = 0; // 0: no limit
};

struct Request {
    Method method = Method::Get;
    std::string url;
    std::vector<Header> headers;
    std::vector<uint8_t> body;        // or uploadFile; Multipart builds a form body
    std::filesystem::path uploadFile; // streamed from disk instead of body
    std::filesystem::path downloadTo; // empty: the answer comes back in memory
    bool resume = false;              // continue downloadTo.part when it exists
    std::optional<uint64_t> expectedSize;
    std::optional<Foundation::Sha256Digest> expectedSha256;
    uint64_t maxBodyBytes = 16u << 20;
    Timeouts timeouts;
    bool allowInsecure = false;
};

struct Response {
    Error error = Error::None;
    int status = 0; // the HTTP status; 0 when no answer came
    std::vector<Header> headers;
    std::vector<uint8_t> body; // empty when the body went to downloadTo
    std::string finalUrl;      // after redirects

    bool ok() const {
        return error == Error::None && status >= 200 && status < 300;
    }
    // The first header with this name, compared without case; nullptr when there is none.
    const Header *header(std::string_view name) const;
    std::string bodyText() const;
};

struct Progress {
    uint64_t received = 0;         // the resumed part included
    std::optional<uint64_t> total; // when the server sent a length
};

struct ClientDesc {
    std::string userAgent; // empty: defaultUserAgent()
    uint32_t maxConnectionsPerHost = 6;
};

namespace Detail {
class Transfer;
}

class RequestHandle {
public:
    RequestHandle() = default;

    void cancel(); // complete is not called after it
    bool isActive() const;

private:
    friend class Client;
    explicit RequestHandle(std::shared_ptr<Detail::Transfer> transfer);

    std::shared_ptr<Detail::Transfer> m_transfer;
};

class Client {
public:
    using CompleteFn = std::function<void(Response &&)>;
    using ProgressFn = std::function<void(const Progress &)>;

    // The scheduler runs the completions; it must outlive the client.
    explicit Client(PandaAsync::Scheduler &scheduler, ClientDesc desc = {});
    ~Client(); // cancels every request and waits for the stack to let go of them
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // Progress comes at most once per frame per request, with the latest numbers. The group is
    // the one of the scheduler's jobs: cancelGroup aborts the requests sent with it.
    RequestHandle
    send(Request request, CompleteFn complete, ProgressFn progress = {}, PandaAsync::GroupId group = 0);
    void cancelGroup(PandaAsync::GroupId group);
    void cancelAll();

    PandaAsync::Scheduler &scheduler() const;
    const ClientDesc &desc() const;

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

// A multipart/form-data body (RFC 7578): fields and files. apply() sets the body and the
// Content-Type of the request.
class Multipart {
public:
    void addField(std::string name, std::string value);
    void addFile(std::string name,
                 std::string fileName,
                 std::vector<uint8_t> bytes,
                 std::string contentType = "application/octet-stream");
    void apply(Request &request) const;

private:
    struct Part {
        std::string name;
        std::string fileName;
        std::string contentType;
        std::vector<uint8_t> bytes;
    };
    std::vector<Part> m_parts;
};

// "Fern (<os>; <arch>)"; the engine and the editor pass PandaEngine/<version> in ClientDesc.
std::string defaultUserAgent();

} // namespace Fern::Net
