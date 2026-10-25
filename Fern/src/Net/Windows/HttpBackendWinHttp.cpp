// The HTTP backend of Fern::Net on Windows: WinHTTP in asynchronous mode. The certificates are
// checked by the system, redirects from https to http are refused by WinHTTP's default policy.

#include "Net/HttpInternal.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#    define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#    define WINHTTP_OPTION_DECOMPRESSION 118
#    define WINHTTP_DECOMPRESSION_FLAG_GZIP 0x00000001
#    define WINHTTP_DECOMPRESSION_FLAG_DEFLATE 0x00000002
#    define WINHTTP_DECOMPRESSION_FLAG_ALL (WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE)
#endif

namespace Fern::Net::Detail {

namespace {

std::wstring toWide(std::string_view utf8) {
    if (utf8.empty()) { return {}; }
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring wide(size_t(std::max(length, 0)), L'\0');
    if (length > 0) { MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), length); }
    return wide;
}

std::string toUtf8(std::wstring_view wide) {
    if (wide.empty()) { return {}; }
    int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(size_t(std::max(length, 0)), '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), utf8.data(), length, nullptr, nullptr);
    }
    return utf8;
}

Error mapError(DWORD code, bool secureRequest) {
    switch (code) {
        case ERROR_WINHTTP_INVALID_URL:
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return Error::InvalidUrl;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return Error::HostNotFound;
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR: return Error::ConnectionFailed;
        case ERROR_WINHTTP_TIMEOUT: return Error::Timeout;
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_INVALID_CA:
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
        case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
        case ERROR_WINHTTP_SECURE_INVALID_CERT:
        case ERROR_WINHTTP_SECURE_CERT_REVOKED:
        case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE: return Error::TlsFailed;
        // The default policy refuses https → http; too many redirects end the same way.
        case ERROR_WINHTTP_REDIRECT_FAILED: return secureRequest ? Error::InsecureUrl : Error::Protocol;
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
        case ERROR_WINHTTP_INVALID_HEADER:
        case ERROR_WINHTTP_HEADER_NOT_FOUND:
        case ERROR_WINHTTP_CHUNKED_ENCODING_HEADER_SIZE_OVERFLOW:
        case ERROR_WINHTTP_RESPONSE_DRAIN_OVERFLOW: return Error::Protocol;
        default: return Error::ConnectionFailed;
    }
}

} // namespace

class WinHttpBackend final : public Backend {
public:
    explicit WinHttpBackend(const ClientDesc &desc) {
        std::wstring agent = toWide(desc.userAgent);
        m_session = WinHttpOpen(agent.c_str(),
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS,
                                WINHTTP_FLAG_ASYNC);
        if (!m_session) {
            m_session = WinHttpOpen(agent.c_str(),
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    WINHTTP_FLAG_ASYNC);
        }
        if (m_session) {
            DWORD perServer = desc.maxConnectionsPerHost;
            WinHttpSetOption(m_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &perServer, sizeof(perServer));
            // Inherited by every request handle of the session.
            WinHttpSetStatusCallback(m_session,
                                     &WinHttpBackend::callback,
                                     WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
                                     0);
        }
    }

    void start(const std::shared_ptr<Transfer> &transfer) override {
        if (!m_session) {
            transfer->finish(Error::ConnectionFailed);
            return;
        }
        auto state = std::make_unique<State>();
        state->transfer = transfer;
        state->backend = this;
        State *raw = state.get();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_states[raw] = std::move(state);
        }
        if (!open(*raw)) {
            DWORD error = GetLastError();
            raw->error = error ? error : ERROR_WINHTTP_INTERNAL_ERROR;
            if (raw->request) {
                closeRequest(*raw); // HANDLE_CLOSING finishes the transfer with the error
            } else {
                if (raw->connect) { WinHttpCloseHandle(raw->connect); }
                remove(raw);
                transfer->finish(mapError(error, transfer->url().secure));
            }
        }
    }

    void abort(Transfer &transfer) override {
        HINTERNET handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &[raw, state] : m_states) {
                if (state->transfer.get() == &transfer) {
                    handle = takeRequestToClose(*state);
                    break;
                }
            }
        }
        if (handle) { WinHttpCloseHandle(handle); }
    }

    void shutdown() override {
        if (m_session) {
            WinHttpSetStatusCallback(m_session, nullptr, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);
            WinHttpCloseHandle(m_session);
            m_session = nullptr;
        }
    }

private:
    struct State {
        std::shared_ptr<Transfer> transfer;
        WinHttpBackend *backend = nullptr;
        HINTERNET connect = nullptr;
        HINTERNET request = nullptr;
        std::vector<uint8_t> buffer;
        FILE *uploadFile = nullptr;
        uint64_t uploadRemaining = 0;
        std::vector<uint8_t> uploadChunk;
        bool completed = false; // the whole body arrived
        bool closing = false;   // WinHttpCloseHandle(request) was issued
        DWORD error = 0;

        ~State() {
            if (uploadFile) { std::fclose(uploadFile); }
        }
    };

    bool open(State &state) {
        const Request &request = state.transfer->request();
        const ParsedUrl &url = state.transfer->url();
        std::string host = url.host.find(':') != std::string::npos ? "[" + url.host + "]" : url.host;
        state.connect = WinHttpConnect(m_session, toWide(host).c_str(), url.port, 0);
        if (!state.connect) { return false; }
        state.request = WinHttpOpenRequest(state.connect,
                                           toWide(methodName(request.method)).c_str(),
                                           toWide(url.pathAndQuery).c_str(),
                                           nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           url.secure ? WINHTTP_FLAG_SECURE : 0);
        if (!state.request) { return false; }
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&state);
        WinHttpSetOption(state.request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context));

        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(state.request, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
        DWORD redirects = 10;
        WinHttpSetOption(state.request, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects, sizeof(redirects));
        const Timeouts &timeouts = request.timeouts;
        int connectMs = int(timeouts.connectSeconds * 1000);
        int idleMs = timeouts.idleSeconds > 0 ? int(timeouts.idleSeconds * 1000) : 0;
        WinHttpSetTimeouts(state.request, connectMs, connectMs, idleMs, idleMs);

        std::wstring headers;
        for (const Header &header : state.transfer->requestHeaders()) {
            headers += toWide(header.name + ": " + header.value) + L"\r\n";
        }
        if (!headers.empty()) {
            WinHttpAddRequestHeaders(state.request,
                                     headers.c_str(),
                                     DWORD(-1),
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        state.buffer.resize(64 * 1024);
        if (!request.uploadFile.empty()) {
            state.uploadFile = _wfopen(request.uploadFile.c_str(), L"rb");
            if (!state.uploadFile) {
                state.transfer->recordError(Error::FileError); // wins over the mapped error
                SetLastError(ERROR_WINHTTP_INTERNAL_ERROR);
                return false;
            }
            std::error_code ec;
            state.uploadRemaining = std::filesystem::file_size(request.uploadFile, ec);
            state.uploadChunk.resize(64 * 1024);
            return WinHttpSendRequest(state.request,
                                      WINHTTP_NO_ADDITIONAL_HEADERS,
                                      0,
                                      WINHTTP_NO_REQUEST_DATA,
                                      0,
                                      DWORD(state.uploadRemaining),
                                      reinterpret_cast<DWORD_PTR>(&state)) == TRUE;
        }
        const std::vector<uint8_t> &body = request.body;
        LPVOID optional = body.empty() ? nullptr : static_cast<LPVOID>(const_cast<uint8_t *>(body.data()));
        return WinHttpSendRequest(state.request,
                                  WINHTTP_NO_ADDITIONAL_HEADERS,
                                  0,
                                  optional,
                                  DWORD(body.size()),
                                  DWORD(body.size()),
                                  reinterpret_cast<DWORD_PTR>(&state)) == TRUE;
    }

    // Closing the request handle is how a transfer ends, with success, an error or a cancel: the
    // HANDLE_CLOSING notification that follows is the last one and finishes the transfer. The
    // callback may run on the closing thread, so the handle is closed outside the mutex: this
    // marks the state under the mutex and hands the handle back to close.
    HINTERNET takeRequestToClose(State &state) {
        if (state.closing) { return nullptr; }
        state.closing = true;
        return state.request;
    }

    void closeRequest(State &state) {
        HINTERNET handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            handle = takeRequestToClose(state);
        }
        if (handle) { WinHttpCloseHandle(handle); }
    }

    void remove(State *state) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_states.erase(state);
    }

    void fail(State &state, DWORD error) {
        state.error = error ? error : ERROR_WINHTTP_INTERNAL_ERROR;
        closeRequest(state);
    }

    void stop(State &state) {
        closeRequest(state);
    }

    void writeNextChunk(State &state) {
        size_t size = size_t(std::min<uint64_t>(state.uploadRemaining, state.uploadChunk.size()));
        size_t read = std::fread(state.uploadChunk.data(), 1, size, state.uploadFile);
        if (read == 0) {
            fail(state, ERROR_WINHTTP_INTERNAL_ERROR);
            return;
        }
        state.uploadRemaining -= read;
        if (!WinHttpWriteData(state.request, state.uploadChunk.data(), DWORD(read), nullptr)) { fail(state, GetLastError()); }
    }

    void receiveResponse(State &state) {
        if (!WinHttpReceiveResponse(state.request, nullptr)) { fail(state, GetLastError()); }
    }

    void headersAvailable(State &state) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(state.request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                 &status,
                                 &size,
                                 WINHTTP_NO_HEADER_INDEX)) {
            fail(state, GetLastError());
            return;
        }

        std::vector<Header> headers;
        size = 0;
        WinHttpQueryHeaders(state.request,
                            WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            nullptr,
                            &size,
                            WINHTTP_NO_HEADER_INDEX);
        if (size > 0) {
            std::wstring raw(size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(state.request,
                                    WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX,
                                    raw.data(),
                                    &size,
                                    WINHTTP_NO_HEADER_INDEX)) {
                std::string text = toUtf8(std::wstring_view(raw.data(), size / sizeof(wchar_t)));
                size_t position = text.find("\r\n"); // after the status line
                while (position != std::string::npos) {
                    position += 2;
                    size_t end = text.find("\r\n", position);
                    std::string_view line = std::string_view(text).substr(position, end == std::string::npos ? std::string::npos : end - position);
                    size_t colon = line.find(':');
                    if (colon != std::string_view::npos) {
                        std::string_view value = line.substr(colon + 1);
                        while (!value.empty() && value.front() == ' ') { value.remove_prefix(1); }
                        headers.push_back({std::string(line.substr(0, colon)), std::string(value)});
                    }
                    position = end;
                }
            }
        }

        std::string finalUrl = state.transfer->request().url;
        size = 0;
        WinHttpQueryOption(state.request, WINHTTP_OPTION_URL, nullptr, &size);
        if (size > 0) {
            std::wstring url(size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryOption(state.request, WINHTTP_OPTION_URL, url.data(), &size)) {
                finalUrl = toUtf8(std::wstring_view(url.data(), size / sizeof(wchar_t)));
                while (!finalUrl.empty() && finalUrl.back() == '\0') { finalUrl.pop_back(); }
            }
        }

        if (!state.transfer->onResponse(int(status), std::move(headers), std::move(finalUrl))) {
            stop(state);
            return;
        }
        queryData(state);
    }

    void queryData(State &state) {
        if (!WinHttpQueryDataAvailable(state.request, nullptr)) { fail(state, GetLastError()); }
    }

    void dataAvailable(State &state, DWORD available) {
        if (available == 0) {
            state.completed = true;
            stop(state);
            return;
        }
        DWORD size = DWORD(std::min<size_t>(available, state.buffer.size()));
        if (!WinHttpReadData(state.request, state.buffer.data(), size, nullptr)) { fail(state, GetLastError()); }
    }

    void readComplete(State &state, DWORD length) {
        if (length == 0) {
            state.completed = true;
            stop(state);
            return;
        }
        if (!state.transfer->onData(state.buffer.data(), length)) {
            stop(state);
            return;
        }
        queryData(state);
    }

    void handleClosing(State &state) {
        std::shared_ptr<Transfer> transfer = state.transfer;
        bool secure = transfer->url().secure;
        Error error = Error::None;
        if (!state.completed) {
            error = state.error ? mapError(state.error, secure) : Error::ConnectionFailed;
        }
        state.request = nullptr;
        if (state.connect) {
            WinHttpCloseHandle(state.connect);
            state.connect = nullptr;
        }
        remove(&state); // the state is gone after this line
        transfer->finish(error);
    }

    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info, DWORD length) {
        if (context == 0) { return; } // the session and the connections carry no context
        State &state = *reinterpret_cast<State *>(context);
        WinHttpBackend &backend = *state.backend;
        switch (status) {
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
                if (state.uploadFile && state.uploadRemaining > 0) {
                    backend.writeNextChunk(state);
                } else {
                    backend.receiveResponse(state);
                }
                break;
            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: backend.headersAvailable(state); break;
            case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: backend.dataAvailable(state, *static_cast<DWORD *>(info)); break;
            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: backend.readComplete(state, length); break;
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
                backend.fail(state, static_cast<WINHTTP_ASYNC_RESULT *>(info)->dwError);
                break;
            case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING: backend.handleClosing(state); break;
            default: break;
        }
    }

    HINTERNET m_session = nullptr;
    std::mutex m_mutex;
    std::unordered_map<State *, std::unique_ptr<State>> m_states;
};

std::unique_ptr<Backend> Backend::create(const ClientDesc &desc) {
    return std::make_unique<WinHttpBackend>(desc);
}

} // namespace Fern::Net::Detail
