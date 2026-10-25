// The HTTP backend of Fern::Net on Linux: the libcurl of the system, one multi handle driven by a
// thread of the client. The certificates are checked against the store of the system, as the
// distribution built libcurl to do.

#include "Net/HttpInternal.hpp"

#include <Foundation/Thread.hpp>

#define CURL_DISABLE_DEPRECATION
#include <curl/curl.h>

#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Fern::Net::Detail {

namespace {

std::once_flag g_curlInit;

Error mapError(CURLcode code) {
    switch (code) {
        case CURLE_OK: return Error::None;
        case CURLE_URL_MALFORMAT: return Error::InvalidUrl;
        case CURLE_UNSUPPORTED_PROTOCOL: return Error::InsecureUrl; // a redirect outside https
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY: return Error::HostNotFound;
        case CURLE_COULDNT_CONNECT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_PARTIAL_FILE:
        case CURLE_GOT_NOTHING: return Error::ConnectionFailed;
        case CURLE_OPERATION_TIMEDOUT: return Error::Timeout;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_ISSUER_ERROR:
        case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
        case CURLE_SSL_INVALIDCERTSTATUS: return Error::TlsFailed;
        case CURLE_TOO_MANY_REDIRECTS:
        case CURLE_WEIRD_SERVER_REPLY:
        case CURLE_HTTP2:
        case CURLE_HTTP2_STREAM:
        case CURLE_BAD_CONTENT_ENCODING:
        case CURLE_RANGE_ERROR: return Error::Protocol;
        case CURLE_READ_ERROR: return Error::FileError;
        default: return Error::ConnectionFailed;
    }
}

} // namespace

class CurlBackend final : public Backend {
public:
    explicit CurlBackend(const ClientDesc &desc)
        : m_desc(desc) {
        std::call_once(g_curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
        m_multi = curl_multi_init();
        curl_multi_setopt(m_multi, CURLMOPT_MAX_HOST_CONNECTIONS, long(desc.maxConnectionsPerHost));
        m_thread = std::thread([this] {
            Foundation::setCurrentThreadName("Fern Net");
            run();
        });
    }

    void start(const std::shared_ptr<Transfer> &transfer) override {
        auto state = std::make_unique<State>();
        state->transfer = transfer;
        if (!setup(*state)) {
            transfer->finish(Error::InvalidUrl);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_adding.push_back(std::move(state));
        }
        curl_multi_wakeup(m_multi);
    }

    void abort(Transfer &transfer) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_aborting.push_back(&transfer);
        }
        curl_multi_wakeup(m_multi);
    }

    void shutdown() override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        curl_multi_wakeup(m_multi);
        if (m_thread.joinable()) { m_thread.join(); }
        curl_multi_cleanup(m_multi);
        m_multi = nullptr;
    }

private:
    struct State {
        std::shared_ptr<Transfer> transfer;
        CURL *easy = nullptr;
        curl_slist *headers = nullptr;
        FILE *uploadFile = nullptr;
        std::vector<Header> responseHeaders;
        bool headersDone = false; // the head of the final answer was handed to the transfer
        char errorBuffer[CURL_ERROR_SIZE] = {};

        ~State() {
            if (easy) { curl_easy_cleanup(easy); }
            if (headers) { curl_slist_free_all(headers); }
            if (uploadFile) { std::fclose(uploadFile); }
        }
    };

    bool setup(State &state) {
        const Request &request = state.transfer->request();
        CURL *easy = curl_easy_init();
        if (!easy) { return false; }
        state.easy = easy;
        curl_easy_setopt(easy, CURLOPT_PRIVATE, &state);
        curl_easy_setopt(easy, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, state.errorBuffer);
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy, CURLOPT_USERAGENT, m_desc.userAgent.c_str());
        curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(easy, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
        // Decision 5: https, and http only where the request itself was allowed to be insecure.
        long protocols = CURLPROTO_HTTPS;
        if (!state.transfer->url().secure) { protocols |= CURLPROTO_HTTP; }
        curl_easy_setopt(easy, CURLOPT_PROTOCOLS, protocols);
        long redirectProtocols = CURLPROTO_HTTPS;
        if (!state.transfer->url().secure && request.allowInsecure) { redirectProtocols |= CURLPROTO_HTTP; }
        curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS, redirectProtocols);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);

        const Timeouts &timeouts = request.timeouts;
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, long(timeouts.connectSeconds * 1000));
        if (timeouts.idleSeconds > 0) {
            curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, long(timeouts.idleSeconds));
        }
        if (timeouts.totalSeconds > 0) { curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, long(timeouts.totalSeconds * 1000)); }

        switch (request.method) {
            case Method::Get: break;
            case Method::Head: curl_easy_setopt(easy, CURLOPT_NOBODY, 1L); break;
            case Method::Post: curl_easy_setopt(easy, CURLOPT_POST, 1L); break;
            default: curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, methodName(request.method)); break;
        }
        if (!request.uploadFile.empty()) {
            state.uploadFile = std::fopen(request.uploadFile.c_str(), "rb");
            if (!state.uploadFile) { return false; }
            std::error_code ec;
            uint64_t size = std::filesystem::file_size(request.uploadFile, ec);
            if (request.method == Method::Put) {
                curl_easy_setopt(easy, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(easy, CURLOPT_INFILESIZE_LARGE, curl_off_t(size));
            } else {
                curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(size));
            }
            curl_easy_setopt(easy, CURLOPT_READFUNCTION, &CurlBackend::readCallback);
            curl_easy_setopt(easy, CURLOPT_READDATA, &state);
        } else if (!request.body.empty() || request.method == Method::Post) {
            curl_easy_setopt(easy, CURLOPT_POSTFIELDS, request.body.data());
            curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(request.body.size()));
        }

        for (const Header &header : state.transfer->requestHeaders()) {
            state.headers = curl_slist_append(state.headers, (header.name + ": " + header.value).c_str());
        }
        if (state.headers) { curl_easy_setopt(easy, CURLOPT_HTTPHEADER, state.headers); }

        curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &CurlBackend::headerCallback);
        curl_easy_setopt(easy, CURLOPT_HEADERDATA, &state);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &CurlBackend::writeCallback);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &state);
        curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &CurlBackend::progressCallback);
        curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &state);
        return true;
    }

    // The head of the answer, one line at a time; a new status line starts the next answer (a
    // redirect was followed), the empty line ends the head of the final one.
    static size_t headerCallback(char *buffer, size_t size, size_t count, void *userData) {
        State &state = *static_cast<State *>(userData);
        size_t length = size * count;
        std::string_view line(buffer, length);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) { line.remove_suffix(1); }
        if (line.rfind("HTTP/", 0) == 0) {
            state.responseHeaders.clear();
            state.headersDone = false;
            return length;
        }
        if (line.empty()) {
            if (state.headersDone) { return length; }
            long status = 0;
            curl_easy_getinfo(state.easy, CURLINFO_RESPONSE_CODE, &status);
            if (status >= 300 && status < 400) { return length; } // libcurl follows it; the final answer comes
            if (status == 100) { return length; }
            char *effective = nullptr;
            curl_easy_getinfo(state.easy, CURLINFO_EFFECTIVE_URL, &effective);
            state.headersDone = true;
            if (!state.transfer->onResponse(int(status), std::move(state.responseHeaders), effective ? effective : "")) {
                return 0; // CURLE_WRITE_ERROR; the recorded error wins in finish
            }
            return length;
        }
        size_t colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string_view name = line.substr(0, colon);
            std::string_view value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') { value.remove_prefix(1); }
            state.responseHeaders.push_back({std::string(name), std::string(value)});
        }
        return length;
    }

    static size_t writeCallback(char *buffer, size_t size, size_t count, void *userData) {
        State &state = *static_cast<State *>(userData);
        size_t length = size * count;
        if (!state.transfer->onData(reinterpret_cast<const uint8_t *>(buffer), length)) { return 0; }
        return length;
    }

    static size_t readCallback(char *buffer, size_t size, size_t count, void *userData) {
        State &state = *static_cast<State *>(userData);
        if (!state.uploadFile) { return 0; }
        size_t read = std::fread(buffer, 1, size * count, state.uploadFile);
        if (read == 0 && std::ferror(state.uploadFile)) { return CURL_READFUNC_ABORT; }
        return read;
    }

    static int progressCallback(void *userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        State &state = *static_cast<State *>(userData);
        return state.transfer->isCanceled() ? 1 : 0; // CURLE_ABORTED_BY_CALLBACK
    }

    void run() {
        for (;;) {
            // The commands of the other threads: new transfers, aborts, the end.
            std::vector<std::unique_ptr<State>> adding;
            std::vector<Transfer *> aborting;
            bool stopping = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                adding.swap(m_adding);
                aborting.swap(m_aborting);
                stopping = m_stopping;
            }
            for (auto &state : adding) {
                State *raw = state.get();
                m_states[raw] = std::move(state);
                if (curl_multi_add_handle(m_multi, raw->easy) != CURLM_OK) { finish(raw, Error::ConnectionFailed); }
            }
            for (Transfer *transfer : aborting) {
                for (auto &[raw, state] : m_states) {
                    if (state->transfer.get() == transfer) {
                        finish(raw, Error::ConnectionFailed); // canceled: no completion follows
                        break;
                    }
                }
            }
            if (stopping) {
                while (!m_states.empty()) { finish(m_states.begin()->first, Error::ConnectionFailed); }
                return;
            }

            int running = 0;
            curl_multi_perform(m_multi, &running);
            int queued = 0;
            while (CURLMsg *message = curl_multi_info_read(m_multi, &queued)) {
                if (message->msg != CURLMSG_DONE) { continue; }
                State *state = nullptr;
                curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, reinterpret_cast<char **>(&state));
                CURLcode result = message->data.result;
                finish(state, result == CURLE_WRITE_ERROR || result == CURLE_ABORTED_BY_CALLBACK ? Error::ConnectionFailed
                                                                                                   : mapError(result));
            }
            int numfds = 0;
            curl_multi_poll(m_multi, nullptr, 0, 1000, &numfds);
        }
    }

    // Removes the transfer from the multi handle and ends it. Runs on the client thread only.
    void finish(State *state, Error transportError) {
        auto it = m_states.find(state);
        if (it == m_states.end()) { return; }
        std::unique_ptr<State> owned = std::move(it->second);
        m_states.erase(it);
        curl_multi_remove_handle(m_multi, owned->easy);
        owned->transfer->finish(transportError);
    }

    ClientDesc m_desc;
    CURLM *m_multi = nullptr;
    std::thread m_thread;
    std::mutex m_mutex;
    std::vector<std::unique_ptr<State>> m_adding;
    std::vector<Transfer *> m_aborting;
    bool m_stopping = false;
    std::unordered_map<State *, std::unique_ptr<State>> m_states; // the client thread's
};

std::unique_ptr<Backend> Backend::create(const ClientDesc &desc) {
    return std::make_unique<CurlBackend>(desc);
}

} // namespace Fern::Net::Detail
