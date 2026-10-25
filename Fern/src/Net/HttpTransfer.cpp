#include "Net/HttpInternal.hpp"

#include <Foundation/Logger.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

namespace Fern::Net::Detail {

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) { return false; }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::optional<ParsedUrl> parseUrl(std::string_view url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos || schemeEnd == 0) { return std::nullopt; }
    std::string_view scheme = url.substr(0, schemeEnd);
    ParsedUrl parsed;
    if (equalsIgnoreCase(scheme, "https")) {
        parsed.secure = true;
    } else if (equalsIgnoreCase(scheme, "http")) {
        parsed.secure = false;
    } else {
        return std::nullopt;
    }

    std::string_view rest = url.substr(schemeEnd + 3);
    size_t authorityEnd = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, authorityEnd);
    std::string_view path = authorityEnd == std::string_view::npos ? std::string_view() : rest.substr(authorityEnd);
    if (authority.empty() || authority.find('@') != std::string_view::npos) { return std::nullopt; }

    std::string_view hostPart = authority;
    std::string_view portPart;
    if (authority.front() == '[') {
        size_t close = authority.find(']');
        if (close == std::string_view::npos) { return std::nullopt; }
        hostPart = authority.substr(1, close - 1);
        std::string_view tail = authority.substr(close + 1);
        if (!tail.empty()) {
            if (tail.front() != ':') { return std::nullopt; }
            portPart = tail.substr(1);
        }
    } else {
        size_t colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            hostPart = authority.substr(0, colon);
            portPart = authority.substr(colon + 1);
        }
    }
    if (hostPart.empty()) { return std::nullopt; }
    for (char c : hostPart) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':' || c == '_')) {
            return std::nullopt;
        }
    }

    unsigned port = parsed.secure ? 443 : 80;
    if (!portPart.empty()) {
        port = 0;
        for (char c : portPart) {
            if (!std::isdigit(static_cast<unsigned char>(c))) { return std::nullopt; }
            port = port * 10 + unsigned(c - '0');
            if (port > 65535) { return std::nullopt; }
        }
        if (port == 0) { return std::nullopt; }
    }

    parsed.host.assign(hostPart.begin(), hostPart.end());
    std::transform(parsed.host.begin(), parsed.host.end(), parsed.host.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    parsed.port = uint16_t(port);
    parsed.pathAndQuery = path.empty() || path.front() == '?' ? std::string("/") + std::string(path) : std::string(path);
    size_t fragment = parsed.pathAndQuery.find('#');
    if (fragment != std::string::npos) { parsed.pathAndQuery.erase(fragment); }
    parsed.loopback = parsed.host == "localhost" || parsed.host == "127.0.0.1" || parsed.host == "::1";
    return parsed;
}

Error redirectError(const ParsedUrl &from, std::string_view location, bool allowInsecure) {
    std::optional<ParsedUrl> target = parseUrl(location);
    if (!target) {
        // A relative location keeps the scheme of the request.
        return location.empty() ? Error::Protocol : Error::None;
    }
    if (!target->secure && !target->loopback && (from.secure || !allowInsecure)) { return Error::InsecureUrl; }
    return Error::None;
}

// ---- Transfer ----------------------------------------------------------------------------------

Transfer::Transfer(Client::Impl &client,
                   Request request,
                   ParsedUrl url,
                   Client::CompleteFn complete,
                   Client::ProgressFn progress,
                   PandaAsync::GroupId group)
    : m_client(client)
    , m_request(std::move(request))
    , m_url(std::move(url))
    , m_complete(std::move(complete))
    , m_progress(std::move(progress))
    , m_group(group) {
    m_startTime = std::chrono::steady_clock::now();
}

Transfer::~Transfer() {
    closePart();
}

void Transfer::recordError(Error error) {
    Error none = Error::None;
    m_error.compare_exchange_strong(none, error);
}

bool Transfer::openPart(bool append) {
    closePart();
#if defined(_WIN32)
    m_file = _wfopen(m_partPath.c_str(), append ? L"ab" : L"wb");
#else
    m_file = std::fopen(m_partPath.c_str(), append ? "ab" : "wb");
#endif
    return m_file != nullptr;
}

void Transfer::closePart() {
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

bool Transfer::hashExistingPart() {
#if defined(_WIN32)
    FILE *file = _wfopen(m_partPath.c_str(), L"rb");
#else
    FILE *file = std::fopen(m_partPath.c_str(), "rb");
#endif
    if (!file) { return false; }
    std::vector<uint8_t> buffer(1u << 20);
    uint64_t total = 0;
    for (;;) {
        size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
        if (read == 0) { break; }
        m_hasher.update(buffer.data(), read);
        total += read;
    }
    bool ok = std::ferror(file) == 0;
    std::fclose(file);
    if (ok) { m_resumeOffset = total; }
    return ok;
}

bool Transfer::prepare(Error &error) {
    if (m_request.downloadTo.empty()) { return true; }
    std::error_code ec;
    m_partPath = m_request.downloadTo;
    m_partPath += ".part";
    m_validatorPath = m_partPath;
    m_validatorPath += ".validator";
    std::filesystem::create_directories(m_request.downloadTo.parent_path(), ec);

    bool resume = false;
    if (m_request.resume && std::filesystem::is_regular_file(m_partPath, ec) &&
        std::filesystem::file_size(m_partPath, ec) > 0) {
        FILE *validator =
#if defined(_WIN32)
            _wfopen(m_validatorPath.c_str(), L"rb");
#else
            std::fopen(m_validatorPath.c_str(), "rb");
#endif
        if (validator) {
            char line[512];
            if (std::fgets(line, sizeof(line), validator)) {
                m_resumeValidator = line;
                while (!m_resumeValidator.empty() &&
                       (m_resumeValidator.back() == '\n' || m_resumeValidator.back() == '\r')) {
                    m_resumeValidator.pop_back();
                }
            }
            std::fclose(validator);
        }
        resume = !m_resumeValidator.empty() && hashExistingPart() && m_resumeOffset > 0;
    }
    if (!resume) {
        m_hasher.reset();
        m_resumeOffset = 0;
        m_resumeValidator.clear();
        std::filesystem::remove(m_validatorPath, ec);
    }
    if (!openPart(resume)) {
        error = Error::FileError;
        return false;
    }
    m_received = m_resumeOffset;
    return true;
}

std::vector<Header> Transfer::requestHeaders() const {
    std::vector<Header> headers = m_request.headers;
    if (m_resumeOffset > 0) {
        headers.push_back({"Range", "bytes=" + std::to_string(m_resumeOffset) + "-"});
        headers.push_back({"If-Range", m_resumeValidator});
    }
    return headers;
}

bool Transfer::onResponse(int status, std::vector<Header> headers, std::string finalUrl) {
    m_response.status = status;
    m_response.headers = std::move(headers);
    m_response.finalUrl = std::move(finalUrl);
    m_toFile = m_file != nullptr && (status == 200 || status == 206);

    std::optional<uint64_t> contentLength;
    bool encoded = false;
    for (const Header &header : m_response.headers) {
        if (equalsIgnoreCase(header.name, "Content-Length")) {
            contentLength = std::strtoull(header.value.c_str(), nullptr, 10);
        } else if (equalsIgnoreCase(header.name, "Content-Encoding") && !equalsIgnoreCase(header.value, "identity")) {
            encoded = true; // the length is the compressed one; the stack unpacks
        }
    }

    if (m_toFile) {
        if (m_resumeOffset > 0 && status == 200) {
            // The server ignored the range: the whole file comes, from the first byte.
            m_resumeOffset = 0;
            m_received = 0;
            m_hasher.reset();
            if (!openPart(false)) {
                recordError(Error::FileError);
                return false;
            }
        }
        // The validator of a later resume: the ETag, or Last-Modified when there is none.
        std::string validator;
        for (const Header &header : m_response.headers) {
            if (equalsIgnoreCase(header.name, "ETag")) {
                validator = header.value;
                break;
            }
            if (equalsIgnoreCase(header.name, "Last-Modified")) { validator = header.value; }
        }
        if (!validator.empty() && validator.find('\n') == std::string::npos) {
#if defined(_WIN32)
            FILE *file = _wfopen(m_validatorPath.c_str(), L"wb");
#else
            FILE *file = std::fopen(m_validatorPath.c_str(), "wb");
#endif
            if (file) {
                std::fputs(validator.c_str(), file);
                std::fputc('\n', file);
                std::fclose(file);
            }
        }
        if (contentLength && !encoded) {
            m_total = m_received + *contentLength;
            if (m_request.expectedSize && *m_total != *m_request.expectedSize) {
                recordError(Error::IntegrityFailed);
                return false;
            }
        }
    } else {
        if (m_resumeOffset > 0 && status == 416) {
            recordError(Error::Protocol);
            return false;
        }
        m_received = 0;
        if (contentLength && !encoded) {
            m_total = *contentLength;
            if (*contentLength > m_request.maxBodyBytes) {
                recordError(Error::TooLarge);
                return false;
            }
        }
    }
    onProgress(m_received, m_total);
    return true;
}

bool Transfer::onData(const uint8_t *bytes, size_t size) {
    if (size == 0) { return !isCanceled(); }
    if (m_request.timeouts.totalSeconds > 0) {
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count();
        if (elapsed > m_request.timeouts.totalSeconds) {
            recordError(Error::Timeout);
            return false;
        }
    }
    if (m_toFile) {
        if (m_request.expectedSize && m_received + size > *m_request.expectedSize) {
            recordError(Error::IntegrityFailed);
            return false;
        }
        if (std::fwrite(bytes, 1, size, m_file) != size) {
            recordError(Error::FileError);
            return false;
        }
        m_hasher.update(bytes, size);
    } else {
        if (m_response.body.size() + size > m_request.maxBodyBytes) {
            recordError(Error::TooLarge);
            return false;
        }
        m_response.body.insert(m_response.body.end(), bytes, bytes + size);
    }
    m_received += size;
    onProgress(m_received, m_total);
    return !isCanceled();
}

void Transfer::onProgress(uint64_t received, std::optional<uint64_t> total) {
    if (!m_progress) { return; }
    m_progressReceived.store(received);
    m_progressTotal.store(total ? int64_t(*total) : -1);
    postProgress();
}

void Transfer::postProgress() {
    if (m_progressQueued.exchange(true)) { return; }
    std::shared_ptr<Transfer> self = shared_from_this();
    PandaAsync::JobDesc desc;
    desc.name = "net progress";
    desc.group = m_group;
    m_client.scheduler.post(desc, [self] {
        self->m_progressQueued.store(false);
        if (self->isCanceled()) { return; }
        Progress progress;
        progress.received = self->m_progressReceived.load();
        int64_t total = self->m_progressTotal.load();
        if (total >= 0) { progress.total = uint64_t(total); }
        self->m_progress(progress);
    });
}

void Transfer::completeFile(Error &error) {
    std::error_code ec;
    if (std::fflush(m_file) != 0) { error = Error::FileError; }
    closePart();
    if (error == Error::None) {
        if (m_request.expectedSize && m_received != *m_request.expectedSize) { error = Error::IntegrityFailed; }
    }
    if (error == Error::None && m_request.expectedSha256) {
        if (m_hasher.finish() != *m_request.expectedSha256) { error = Error::IntegrityFailed; }
    }
    if (error == Error::None) {
        std::filesystem::remove(m_request.downloadTo, ec);
        std::filesystem::rename(m_partPath, m_request.downloadTo, ec);
        if (ec) { error = Error::FileError; }
    }
    if (error == Error::None || error == Error::IntegrityFailed || error == Error::TooLarge) {
        // Done, or a .part that can never become the file: nothing to resume.
        std::filesystem::remove(m_partPath, ec);
        std::filesystem::remove(m_validatorPath, ec);
    }
}

void Transfer::finish(Error transportError) {
    if (m_finished.exchange(true)) { return; }
    Error error = m_error.load();
    if (error == Error::None) { error = transportError; }

    if (isCanceled()) {
        closePart(); // the .part stays for a resume
    } else if (m_file && m_toFile) {
        completeFile(error);
    } else {
        closePart();
    }

    if (!isCanceled()) {
        m_response.error = error;
        if (error != Error::None) {
            LOG_WARN("Fern::Net: %s %s failed: %s%s",
                     methodName(m_request.method),
                     m_request.url.c_str(),
                     errorName(error),
                     m_response.status ? (" (status " + std::to_string(m_response.status) + ")").c_str() : "");
        }
        auto response = std::make_shared<Response>(std::move(m_response));
        std::shared_ptr<Transfer> self = shared_from_this();
        PandaAsync::JobDesc desc;
        desc.name = "net complete";
        desc.group = m_group;
        m_client.scheduler.post(desc, [self, response] {
            if (self->isCanceled()) { return; }
            self->m_complete(std::move(*response));
        });
    }
    m_client.transferFinished(*this);
}

} // namespace Fern::Net::Detail
