// The local services where there is no Bonjour (Windows, Linux, Android): a browser broadcasts a
// question for a type on DISCOVERY_PORT every second and a half, an advertiser listening on that
// port answers the asker with the name, the id and the port of its service. A service that has
// not answered for a while is gone. One advertiser per machine and port: the second one cannot
// bind it.
#if !defined(__APPLE__)

#include "Fern/Net/LocalService.hpp"
#include "Fern/Net/Socket.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string_view>

namespace Fern::Net {

namespace {

    using Clock = std::chrono::steady_clock;
    constexpr const char *ASK = "PANDA?";
    constexpr const char *ANSWER = "PANDA!";
    constexpr double ASK_SECONDS = 1.5;
    constexpr double FORGET_SECONDS = 5.0;

    std::vector<uint8_t> bytes(std::string_view text) {
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    std::vector<std::string> lines(const std::vector<uint8_t> &data) {
        std::vector<std::string> out(1);
        for (uint8_t c : data) {
            if (c == '\n') {
                out.emplace_back();
            } else {
                out.back().push_back(static_cast<char>(c));
            }
        }
        return out;
    }

} // namespace

bool isValidServiceType(const std::string &type) {
    if (type.size() < 7 || type[0] != '_') { return false; }
    const size_t dot = type.find('.');
    if (dot == std::string::npos || dot < 2 || dot > 16) { return false; }
    const std::string proto = type.substr(dot);
    if (proto != "._tcp" && proto != "._udp") { return false; }
    for (size_t i = 1; i < dot; i++) {
        const char c = type[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) { return false; }
    }
    return true;
}

/* ------------------------------ advertiser ------------------------------ */

struct LocalServiceAdvertiser::Impl {
    UdpSocket socket;
    std::string type, name, id;
    uint16_t port = 0;
};

LocalServiceAdvertiser::LocalServiceAdvertiser() = default;
LocalServiceAdvertiser::~LocalServiceAdvertiser() = default;
LocalServiceAdvertiser::LocalServiceAdvertiser(LocalServiceAdvertiser &&) noexcept = default;
LocalServiceAdvertiser &LocalServiceAdvertiser::operator=(LocalServiceAdvertiser &&) noexcept = default;

bool LocalServiceAdvertiser::start(const std::string &type, const std::string &name, const std::string &id, uint16_t port, Error *error) {
    stop();
    if (!isValidServiceType(type)) {
        if (error) { *error = Error::InvalidAddress; }
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->socket = UdpSocket::bind(Address::any(DISCOVERY_PORT), false, error);
    if (!impl->socket.isOpen()) { return false; }
    impl->type = type;
    impl->name = name;
    impl->id = id;
    impl->port = port;
    m_impl = std::move(impl);
    return true;
}

void LocalServiceAdvertiser::stop() {
    m_impl.reset();
}

void LocalServiceAdvertiser::poll() {
    if (!m_impl) { return; }
    for (int i = 0; i < 64; i++) {
        std::optional<Datagram> datagram = m_impl->socket.receive();
        if (!datagram) { return; }
        const std::vector<std::string> parts = lines(datagram->bytes);
        if (parts.size() < 2 || parts[0] != ASK || parts[1] != m_impl->type) { continue; }
        const std::string answer = std::string(ANSWER) + "\n" + m_impl->type + "\n" + m_impl->name + "\n" + m_impl->id + "\n" +
                                   std::to_string(m_impl->port);
        m_impl->socket.sendTo(datagram->from, bytes(answer));
    }
}

bool LocalServiceAdvertiser::isRunning() const {
    return m_impl != nullptr;
}

/* ------------------------------ browser ------------------------------ */

struct LocalServiceBrowser::Impl {
    UdpSocket socket;
    std::string type;
    Clock::time_point lastAsk{};
    struct Seen {
        LocalService service;
        Clock::time_point at;
    };
    std::vector<Seen> seen;
    std::vector<LocalService> services;
    bool changed = false;
};

LocalServiceBrowser::LocalServiceBrowser() = default;
LocalServiceBrowser::~LocalServiceBrowser() = default;
LocalServiceBrowser::LocalServiceBrowser(LocalServiceBrowser &&) noexcept = default;
LocalServiceBrowser &LocalServiceBrowser::operator=(LocalServiceBrowser &&) noexcept = default;

bool LocalServiceBrowser::start(const std::string &type, Error *error) {
    stop();
    if (!isValidServiceType(type)) {
        if (error) { *error = Error::InvalidAddress; }
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->socket = UdpSocket::bind(Address::any(0), true, error);
    if (!impl->socket.isOpen()) { return false; }
    impl->type = type;
    m_impl = std::move(impl);
    return true;
}

void LocalServiceBrowser::stop() {
    m_impl.reset();
}

bool LocalServiceBrowser::poll() {
    if (!m_impl) { return false; }
    const Clock::time_point now = Clock::now();
    if (std::chrono::duration<double>(now - m_impl->lastAsk).count() > ASK_SECONDS) {
        m_impl->lastAsk = now;
        const std::string ask = std::string(ASK) + "\n" + m_impl->type;
        m_impl->socket.sendTo(Address::broadcast(DISCOVERY_PORT), bytes(ask));
    }
    for (int i = 0; i < 64; i++) {
        std::optional<Datagram> datagram = m_impl->socket.receive();
        if (!datagram) { break; }
        const std::vector<std::string> parts = lines(datagram->bytes);
        if (parts.size() < 5 || parts[0] != ANSWER || parts[1] != m_impl->type) { continue; }
        LocalService service;
        service.name = parts[2];
        service.id = parts[3];
        service.host = datagram->from.ipString();
        service.port = static_cast<uint16_t>(std::atoi(parts[4].c_str()));
        auto it = std::find_if(m_impl->seen.begin(), m_impl->seen.end(), [&](const Impl::Seen &s) { return s.service.name == service.name; });
        if (it == m_impl->seen.end()) {
            m_impl->seen.push_back({service, now});
        } else {
            it->service = service;
            it->at = now;
        }
    }
    std::erase_if(m_impl->seen, [&](const Impl::Seen &s) { return std::chrono::duration<double>(now - s.at).count() > FORGET_SECONDS; });
    std::vector<LocalService> current;
    for (const Impl::Seen &s : m_impl->seen) { current.push_back(s.service); }
    const bool changed = current != m_impl->services;
    m_impl->services = std::move(current);
    return changed;
}

const std::vector<LocalService> &LocalServiceBrowser::services() const {
    static const std::vector<LocalService> none;
    return m_impl ? m_impl->services : none;
}

} // namespace Fern::Net

#endif
