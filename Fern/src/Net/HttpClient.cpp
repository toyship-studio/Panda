#include "Net/HttpInternal.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Foundation/Thread.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace Fern::Net {

const char *methodName(Method method) {
    switch (method) {
        case Method::Get: return "GET";
        case Method::Head: return "HEAD";
        case Method::Post: return "POST";
        case Method::Put: return "PUT";
        case Method::Patch: return "PATCH";
        case Method::Delete: return "DELETE";
    }
    return "GET";
}

const char *errorName(Error error) {
    switch (error) {
        case Error::None: return "none";
        case Error::InvalidUrl: return "invalid url";
        case Error::InsecureUrl: return "insecure url";
        case Error::Offline: return "offline";
        case Error::HostNotFound: return "host not found";
        case Error::ConnectionFailed: return "connection failed";
        case Error::TlsFailed: return "tls failed";
        case Error::Timeout: return "timeout";
        case Error::TooLarge: return "too large";
        case Error::IntegrityFailed: return "integrity failed";
        case Error::FileError: return "file error";
        case Error::Protocol: return "protocol error";
    }
    return "unknown";
}

const Header *Response::header(std::string_view name) const {
    for (const Header &header : headers) {
        if (Detail::equalsIgnoreCase(header.name, name)) { return &header; }
    }
    return nullptr;
}

std::string Response::bodyText() const {
    return std::string(body.begin(), body.end());
}

std::string defaultUserAgent() {
#if defined(PLATFORM_MACOS)
    const char *os = "macOS";
#elif defined(PLATFORM_IOS)
    const char *os = "iOS";
#elif defined(PLATFORM_WINDOWS)
    const char *os = "Windows";
#elif defined(PLATFORM_LINUX)
    const char *os = "Linux";
#elif defined(PLATFORM_ANDROID)
    const char *os = "Android";
#else
    const char *os = "unknown";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char *arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *arch = "x86_64";
#else
    const char *arch = "unknown";
#endif
    return std::string("Fern (") + os + "; " + arch + ")";
}

// ---- RequestHandle ----------------------------------------------------------------------------

RequestHandle::RequestHandle(std::shared_ptr<Detail::Transfer> transfer)
    : m_transfer(std::move(transfer)) {}

void RequestHandle::cancel() {
    if (m_transfer) { m_transfer->cancel(); }
}

bool RequestHandle::isActive() const {
    return m_transfer && !m_transfer->isFinished() && !m_transfer->isCanceled();
}

// ---- Client::Impl -----------------------------------------------------------------------------

Client::Impl::Impl(PandaAsync::Scheduler &scheduler_, ClientDesc desc_)
    : scheduler(scheduler_)
    , desc(std::move(desc_)) {
    if (desc.userAgent.empty()) { desc.userAgent = defaultUserAgent(); }
    if (desc.maxConnectionsPerHost == 0) { desc.maxConnectionsPerHost = 1; }
    m_backend = Detail::Backend::create(desc);
    m_launcher = std::thread([this] {
        Foundation::setCurrentThreadName("Fern Net Launch");
        runLauncher();
    });
}

Client::Impl::~Impl() {
    cancelAll();
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_launchCv.notify_all();
        // Every started transfer ends with finish() from the backend; the launcher finishes the
        // ones that never started.
        m_drainedCv.wait(lock, [this] { return m_active.empty() && m_pending.empty(); });
    }
    if (m_launcher.joinable()) { m_launcher.join(); }
    m_backend->shutdown();
}

RequestHandle Client::Impl::send(Request request, CompleteFn complete, ProgressFn progress, PandaAsync::GroupId group) {
    std::optional<Detail::ParsedUrl> url = Detail::parseUrl(request.url);
    Error early = Error::None;
    if (!url) {
        early = Error::InvalidUrl;
        url = Detail::ParsedUrl{};
    } else if (!url->secure && !url->loopback && !request.allowInsecure) {
        early = Error::InsecureUrl;
    }
    if (!request.body.empty() && !request.uploadFile.empty()) { early = Error::InvalidUrl; }

    std::string requestUrl = request.url;
    auto transfer = std::make_shared<Detail::Transfer>(
        *this, std::move(request), std::move(*url), std::move(complete), std::move(progress), group
    );
    if (early != Error::None) {
        // Reported like every other error: on the main thread, in processCompletions.
        transfer->recordError(early);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_active.emplace(transfer.get(), transfer);
        }
        transfer->finish(early); // takes the mutex itself through transferFinished
        return RequestHandle(transfer);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active.emplace(transfer.get(), transfer);
        m_pending.push_back(transfer);
    }
    m_launchCv.notify_one();
    return RequestHandle(transfer);
}

void Client::Impl::runLauncher() {
    for (;;) {
        std::shared_ptr<Detail::Transfer> transfer;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_launchCv.wait(lock, [this] { return m_stopping || !m_pending.empty(); });
            if (m_pending.empty()) {
                if (m_stopping) { return; }
                continue;
            }
            transfer = std::move(m_pending.front());
            m_pending.pop_front();
            if (m_pending.empty()) { m_drainedCv.notify_all(); }
        }
        if (transfer->isCanceled()) {
            transfer->finish(Error::None);
            continue;
        }
        Error error = Error::None;
        if (!transfer->prepare(error)) {
            transfer->finish(error);
            continue;
        }
        bool start = false;
        {
            // abort() looks at the started flag under the same lock: a transfer is either aborted
            // before it starts (and finishes here) or after (and the backend finishes it).
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!transfer->isCanceled()) {
                transfer->markStarted();
                start = true;
            }
        }
        if (!start) {
            transfer->finish(Error::None);
            continue;
        }
        m_backend->start(transfer);
    }
}

void Client::Impl::abort(Detail::Transfer &transfer) {
    bool started = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        transfer.cancel();
        started = transfer.isStarted();
    }
    if (started && !transfer.isFinished()) { m_backend->abort(transfer); }
}

void Client::Impl::cancelGroup(PandaAsync::GroupId group) {
    if (group == 0) { return; }
    std::vector<std::shared_ptr<Detail::Transfer>> transfers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &[pointer, transfer] : m_active) {
            if (transfer->group() == group) { transfers.push_back(transfer); }
        }
    }
    for (auto &transfer : transfers) { abort(*transfer); }
}

void Client::Impl::cancelAll() {
    std::vector<std::shared_ptr<Detail::Transfer>> transfers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &[pointer, transfer] : m_active) { transfers.push_back(transfer); }
    }
    for (auto &transfer : transfers) { abort(*transfer); }
}

void Client::Impl::transferFinished(Detail::Transfer &transfer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active.erase(&transfer);
    if (m_active.empty()) { m_drainedCv.notify_all(); }
}

// ---- Client -----------------------------------------------------------------------------------

Client::Client(PandaAsync::Scheduler &scheduler, ClientDesc desc)
    : m_impl(std::make_unique<Impl>(scheduler, std::move(desc))) {}

Client::~Client() = default;

RequestHandle Client::send(Request request, CompleteFn complete, ProgressFn progress, PandaAsync::GroupId group) {
    return m_impl->send(std::move(request), std::move(complete), std::move(progress), group);
}

void Client::cancelGroup(PandaAsync::GroupId group) {
    m_impl->cancelGroup(group);
}

void Client::cancelAll() {
    m_impl->cancelAll();
}

PandaAsync::Scheduler &Client::scheduler() const {
    return m_impl->scheduler;
}

const ClientDesc &Client::desc() const {
    return m_impl->desc;
}

// ---- Multipart --------------------------------------------------------------------------------

void Multipart::addField(std::string name, std::string value) {
    Part part;
    part.name = std::move(name);
    part.bytes.assign(value.begin(), value.end());
    m_parts.push_back(std::move(part));
}

void Multipart::addFile(std::string name, std::string fileName, std::vector<uint8_t> bytes, std::string contentType) {
    Part part;
    part.name = std::move(name);
    part.fileName = std::move(fileName);
    part.contentType = std::move(contentType);
    part.bytes = std::move(bytes);
    m_parts.push_back(std::move(part));
}

void Multipart::apply(Request &request) const {
    // A boundary no part contains: the parts are checked and the boundary extended until none does.
    std::string boundary = "----PandaFormBoundary" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    for (bool collides = true; collides;) {
        collides = false;
        for (const Part &part : m_parts) {
            if (std::search(part.bytes.begin(), part.bytes.end(), boundary.begin(), boundary.end()) != part.bytes.end()) {
                collides = true;
                boundary += "x";
                break;
            }
        }
    }

    std::vector<uint8_t> &body = request.body;
    body.clear();
    auto append = [&body](std::string_view text) { body.insert(body.end(), text.begin(), text.end()); };
    for (const Part &part : m_parts) {
        append("--" + boundary + "\r\n");
        append("Content-Disposition: form-data; name=\"" + part.name + "\"");
        if (!part.fileName.empty()) { append("; filename=\"" + part.fileName + "\""); }
        append("\r\n");
        if (!part.contentType.empty()) { append("Content-Type: " + part.contentType + "\r\n"); }
        append("\r\n");
        body.insert(body.end(), part.bytes.begin(), part.bytes.end());
        append("\r\n");
    }
    append("--" + boundary + "--\r\n");

    for (Header &header : request.headers) {
        if (Detail::equalsIgnoreCase(header.name, "Content-Type")) {
            header.value = "multipart/form-data; boundary=" + boundary;
            return;
        }
    }
    request.headers.push_back({"Content-Type", "multipart/form-data; boundary=" + boundary});
}

} // namespace Fern::Net
