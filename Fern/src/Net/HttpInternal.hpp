#pragma once

// The platform-independent part of Fern::Net HTTP: the parsed address, the state of one request
// in flight (the sink of its body, the checks, the completion) and the interface of a platform
// backend. The backends (Apple, Windows, Linux) drive a Transfer from their own threads.

#include "Fern/Net/Http.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Fern::Net::Detail {

struct ParsedUrl {
    bool secure = false;   // https
    bool loopback = false; // localhost, 127.0.0.1, ::1
    std::string host;      // without brackets
    uint16_t port = 0;
    std::string pathAndQuery; // starts with /
};
std::optional<ParsedUrl> parseUrl(std::string_view url);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
// Error for a redirect target: the policy of decision 5 applied to the new address.
Error redirectError(const ParsedUrl &from, std::string_view location, bool allowInsecure);

class Backend;

// One request in flight. prepare() runs on the launch thread of the client, the on*() calls on the
// thread of the backend, cancel() on any thread. finish() runs exactly once, on the thread of the
// backend (or on the launch thread when the request never started).
class Transfer : public std::enable_shared_from_this<Transfer> {
public:
    Transfer(Client::Impl &client,
             Request request,
             ParsedUrl url,
             Client::CompleteFn complete,
             Client::ProgressFn progress,
             PandaAsync::GroupId group);
    ~Transfer();

    const Request &request() const {
        return m_request;
    }
    const ParsedUrl &url() const {
        return m_url;
    }
    PandaAsync::GroupId group() const {
        return m_group;
    }

    // Opens the sink of a download (the .part, the bytes of a resume are hashed). False: error set.
    bool prepare(Error &error);
    // Bytes already in the .part; the backend sends Range/If-Range when > 0.
    uint64_t resumeOffset() const {
        return m_resumeOffset;
    }
    const std::string &resumeValidator() const {
        return m_resumeValidator;
    }
    // The request headers with Range and If-Range added for a resume.
    std::vector<Header> requestHeaders() const;

    // The head of the final answer. False: the transfer must stop (recordedError says why).
    bool onResponse(int status, std::vector<Header> headers, std::string finalUrl);
    // A piece of the body. False: the transfer must stop.
    bool onData(const uint8_t *bytes, size_t size);
    void onProgress(uint64_t received, std::optional<uint64_t> total);
    // The end, with the error of the transport (None when it finished). A recorded error wins.
    void finish(Error transportError);

    void recordError(Error error);
    Error recordedError() const {
        return m_error.load();
    }
    void cancel() {
        m_canceled.store(true);
    }
    bool isCanceled() const {
        return m_canceled.load();
    }
    bool isFinished() const {
        return m_finished.load();
    }
    bool markStarted() {
        return !m_started.exchange(true);
    }
    bool isStarted() const {
        return m_started.load();
    }

private:
    bool hashExistingPart();
    bool openPart(bool append);
    void closePart();
    void postProgress();
    void completeFile(Error &error);

    Client::Impl &m_client;
    Request m_request;
    ParsedUrl m_url;
    Client::CompleteFn m_complete;
    Client::ProgressFn m_progress;
    PandaAsync::GroupId m_group;

    std::atomic<bool> m_canceled{false};
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_finished{false};
    std::atomic<Error> m_error{Error::None};

    // The sink
    std::filesystem::path m_partPath;
    std::filesystem::path m_validatorPath;
    FILE *m_file = nullptr;
    bool m_toFile = false; // the body of this answer goes to the .part (status 200 or 206)
    uint64_t m_resumeOffset = 0;
    std::string m_resumeValidator;
    Foundation::Sha256 m_hasher;
    uint64_t m_received = 0;
    std::optional<uint64_t> m_total;
    std::chrono::steady_clock::time_point m_startTime;

    Response m_response;

    // Progress, coalesced to one completion per frame
    std::atomic<uint64_t> m_progressReceived{0};
    std::atomic<int64_t> m_progressTotal{-1};
    std::atomic<bool> m_progressQueued{false};
};

// A platform backend. start() runs on the launch thread and must lead to exactly one finish() of
// the transfer, also after abort(). shutdown() runs when no transfer is active and stops the
// threads of the stack.
class Backend {
public:
    virtual ~Backend() = default;
    virtual void start(const std::shared_ptr<Transfer> &transfer) = 0;
    virtual void abort(Transfer &transfer) = 0;
    virtual void shutdown() = 0;

    static std::unique_ptr<Backend> create(const ClientDesc &desc);
};

} // namespace Fern::Net::Detail

namespace Fern::Net {

struct Client::Impl {
    Impl(PandaAsync::Scheduler &scheduler, ClientDesc desc);
    ~Impl();

    RequestHandle send(Request request, CompleteFn complete, ProgressFn progress, PandaAsync::GroupId group);
    void abort(Detail::Transfer &transfer);
    void cancelGroup(PandaAsync::GroupId group);
    void cancelAll();
    // Called by Transfer::finish: the transfer leaves the active set.
    void transferFinished(Detail::Transfer &transfer);

    PandaAsync::Scheduler &scheduler;
    ClientDesc desc;

private:
    void runLauncher();

    std::unique_ptr<Detail::Backend> m_backend;
    std::mutex m_mutex;
    std::condition_variable m_launchCv;
    std::condition_variable m_drainedCv;
    std::deque<std::shared_ptr<Detail::Transfer>> m_pending;
    std::unordered_map<Detail::Transfer *, std::shared_ptr<Detail::Transfer>> m_active;
    bool m_stopping = false;
    std::thread m_launcher;
};

} // namespace Fern::Net
