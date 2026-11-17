#pragma once

// Services of the local network (context/networking.md, section 5a): a program announces that it
// listens on a port — a game that hosts a world — and another one on the same network finds it,
// with no address typed by hand.
//
// On macOS and iOS it is Bonjour (DNS-SD over multicast DNS, the `dns_sd` API of the system); an
// iOS game lists its service type under NSBonjourServices and says why it looks at the local
// network in NSLocalNetworkUsageDescription of its Info.plist, and the system asks the player once.
// Elsewhere it is a question broadcast over UDP on DISCOVERY_PORT and the answers of the
// advertisers.
//
// Like the sockets, nothing here blocks or owns a thread: the owner polls in its tick.
#include <Fern/Net/Error.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Fern::Net {

// A service found in the local network.
struct LocalService {
    std::string name; // what a person reads: "Bunny's island"
    std::string id;   // what the advertiser says of itself: a program tells its own service apart
    std::string host; // where to connect: a host name ("Mishas-iPhone.local.") or an address
    uint16_t port = 0;

    bool operator==(const LocalService &other) const = default;
};

// The type of a service is "_<name>._tcp" (DNS-SD): letters, digits and hyphens, at most 15 of them.
bool isValidServiceType(const std::string &type);

class LocalServiceAdvertiser final {
public:
    LocalServiceAdvertiser();
    ~LocalServiceAdvertiser();
    LocalServiceAdvertiser(LocalServiceAdvertiser &&) noexcept;
    LocalServiceAdvertiser &operator=(LocalServiceAdvertiser &&) noexcept;

    // Announces `name` for a service of `type` that listens on `port` of this device. false — it
    // could not (a wrong type, the system refused): `error` says why.
    bool start(const std::string &type, const std::string &name, const std::string &id, uint16_t port, Error *error = nullptr);
    void stop();
    void poll();
    bool isRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class LocalServiceBrowser final {
public:
    LocalServiceBrowser();
    ~LocalServiceBrowser();
    LocalServiceBrowser(LocalServiceBrowser &&) noexcept;
    LocalServiceBrowser &operator=(LocalServiceBrowser &&) noexcept;

    bool start(const std::string &type, Error *error = nullptr);
    void stop();
    // true — the list changed since the last poll.
    bool poll();
    // The services of the type that are there now, with where to connect to them.
    const std::vector<LocalService> &services() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// The UDP port of the broadcast discovery (not used where Bonjour is).
inline constexpr uint16_t DISCOVERY_PORT = 47888;

} // namespace Fern::Net
