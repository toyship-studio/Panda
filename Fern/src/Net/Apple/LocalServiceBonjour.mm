// Bonjour (DNS-SD) for the local services on macOS and iOS: the dns_sd API of the system. Each
// operation is a DNSServiceRef with a socket of its own; poll() asks the socket whether the
// system answered and lets dns_sd call back — on the thread of the owner, in its tick.
#include "Fern/Net/LocalService.hpp"

#include <Foundation/Logger.hpp>

#include <arpa/inet.h>
#include <dns_sd.h>
#include <poll.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Fern::Net {

namespace {

    // The answers that are waiting on the socket of a reference, given to its callback.
    void processPending(DNSServiceRef ref) {
        if (ref == nullptr) { return; }
        const int fd = DNSServiceRefSockFD(ref);
        if (fd < 0) { return; }
        for (int i = 0; i < 16; i++) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 0) <= 0 || (p.revents & POLLIN) == 0) { return; }
            if (DNSServiceProcessResult(ref) != kDNSServiceErr_NoError) { return; }
        }
    }

    std::string txtValue(uint16_t length, const unsigned char *txt, const char *key) {
        uint8_t valueLength = 0;
        const void *value = TXTRecordGetValuePtr(length, txt, key, &valueLength);
        return value ? std::string(static_cast<const char *>(value), valueLength) : std::string();
    }

} // namespace

bool isValidServiceType(const std::string &type) {
    // "_name._tcp" or "_name._udp", the name of 1..15 letters, digits and hyphens.
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
    DNSServiceRef ref = nullptr;
    bool failed = false;

    ~Impl() {
        if (ref) { DNSServiceRefDeallocate(ref); }
    }

    static void DNSSD_API registered(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType error, const char *name, const char *, const char *,
                                     void *context) {
        auto *self = static_cast<Impl *>(context);
        if (error != kDNSServiceErr_NoError) {
            self->failed = true;
            LOG_WARN("Local service: the announcement failed (dns_sd error %d)", static_cast<int>(error));
        } else {
            LOG_INFO("Local service: announced as \"%s\"", name ? name : "");
        }
    }
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
    TXTRecordRef txt;
    TXTRecordCreate(&txt, 0, nullptr);
    TXTRecordSetValue(&txt, "id", static_cast<uint8_t>(std::min<size_t>(id.size(), 255)), id.data());
    const DNSServiceErrorType result =
        DNSServiceRegister(&impl->ref, 0, 0, name.c_str(), type.c_str(), nullptr, nullptr, htons(port), TXTRecordGetLength(&txt),
                           TXTRecordGetBytesPtr(&txt), &Impl::registered, impl.get());
    TXTRecordDeallocate(&txt);
    if (result != kDNSServiceErr_NoError) {
        impl->ref = nullptr;
        if (error) { *error = Error::Unavailable; }
        return false;
    }
    m_impl = std::move(impl);
    if (error) { *error = Error::None; }
    return true;
}

void LocalServiceAdvertiser::stop() {
    m_impl.reset();
}

void LocalServiceAdvertiser::poll() {
    if (m_impl) { processPending(m_impl->ref); }
}

bool LocalServiceAdvertiser::isRunning() const {
    return m_impl && !m_impl->failed;
}

/* ------------------------------ browser ------------------------------ */

struct LocalServiceBrowser::Impl {
    struct Resolving {
        DNSServiceRef ref = nullptr;
        std::string name;
        bool done = false;
    };
    DNSServiceRef browse = nullptr;
    std::vector<std::unique_ptr<Resolving>> resolving;
    std::vector<LocalService> services;
    bool changed = false;

    ~Impl() {
        for (auto &r : resolving) {
            if (r->ref) { DNSServiceRefDeallocate(r->ref); }
        }
        if (browse) { DNSServiceRefDeallocate(browse); }
    }

    static void DNSSD_API resolved(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType error, const char *, const char *host,
                                   uint16_t port, uint16_t txtLength, const unsigned char *txt, void *context) {
        auto *r = static_cast<std::pair<Impl *, Resolving *> *>(context);
        Impl *self = r->first;
        Resolving *resolving = r->second;
        resolving->done = true;
        if (error != kDNSServiceErr_NoError || host == nullptr) { return; }
        LocalService service;
        service.name = resolving->name;
        service.host = host;
        service.port = ntohs(port);
        service.id = txtValue(txtLength, txt, "id");
        auto it = std::find_if(self->services.begin(), self->services.end(), [&](const LocalService &s) { return s.name == service.name; });
        if (it == self->services.end()) {
            self->services.push_back(service);
            self->changed = true;
        } else if (!(*it == service)) {
            *it = service;
            self->changed = true;
        }
    }

    std::vector<std::unique_ptr<std::pair<Impl *, Resolving *>>> contexts;

    static void DNSSD_API browsed(DNSServiceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType error, const char *name,
                                  const char *type, const char *domain, void *context) {
        auto *self = static_cast<Impl *>(context);
        if (error != kDNSServiceErr_NoError || name == nullptr) { return; }
        if ((flags & kDNSServiceFlagsAdd) == 0) {
            const size_t before = self->services.size();
            std::erase_if(self->services, [&](const LocalService &s) { return s.name == name; });
            self->changed = self->changed || before != self->services.size();
            return;
        }
        auto resolving = std::make_unique<Resolving>();
        resolving->name = name;
        auto ctx = std::make_unique<std::pair<Impl *, Resolving *>>(self, resolving.get());
        if (DNSServiceResolve(&resolving->ref, 0, interfaceIndex, name, type, domain, &Impl::resolved, ctx.get()) == kDNSServiceErr_NoError) {
            self->resolving.push_back(std::move(resolving));
            self->contexts.push_back(std::move(ctx));
        }
    }
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
    if (DNSServiceBrowse(&impl->browse, 0, 0, type.c_str(), nullptr, &Impl::browsed, impl.get()) != kDNSServiceErr_NoError) {
        impl->browse = nullptr;
        if (error) { *error = Error::Unavailable; }
        return false;
    }
    m_impl = std::move(impl);
    if (error) { *error = Error::None; }
    return true;
}

void LocalServiceBrowser::stop() {
    m_impl.reset();
}

bool LocalServiceBrowser::poll() {
    if (!m_impl) { return false; }
    processPending(m_impl->browse);
    for (auto &r : m_impl->resolving) { processPending(r->ref); }
    // A resolve that answered is over: its reference goes (outside its own callback).
    for (size_t i = 0; i < m_impl->resolving.size();) {
        if (!m_impl->resolving[i]->done) {
            i++;
            continue;
        }
        DNSServiceRefDeallocate(m_impl->resolving[i]->ref);
        Impl::Resolving *gone = m_impl->resolving[i].get();
        std::erase_if(m_impl->contexts, [gone](const auto &ctx) { return ctx->second == gone; });
        m_impl->resolving.erase(m_impl->resolving.begin() + static_cast<std::ptrdiff_t>(i));
    }
    const bool changed = m_impl->changed;
    m_impl->changed = false;
    return changed;
}

const std::vector<LocalService> &LocalServiceBrowser::services() const {
    static const std::vector<LocalService> none;
    return m_impl ? m_impl->services : none;
}

} // namespace Fern::Net
