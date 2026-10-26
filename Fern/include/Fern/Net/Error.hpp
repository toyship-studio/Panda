#pragma once

// The errors of the network layer of Fern: HTTP (Http.hpp) and the sockets (Socket.hpp) share one
// list, so a game shows one message for one cause whatever carried it.

#include <cstdint>

namespace Fern::Net {

enum class Error : uint8_t {
    None,
    InvalidUrl,       // the address cannot be parsed
    InsecureUrl,      // http:// without allowInsecure, or a redirect from HTTPS to HTTP
    Offline,          // the system reports no network
    HostNotFound,     // the name does not resolve
    ConnectionFailed, // reset, unreachable, or the connection broke
    TlsFailed,        // the handshake failed or the certificate did not verify
    Timeout,
    TooLarge,         // more than maxBodyBytes into memory; a TCP send queue over its limit
    IntegrityFailed,  // the size or the SHA-256 is not the expected one
    FileError,        // the target file cannot be written or renamed
    Protocol,         // a malformed answer, too many redirects
    // Sockets (Socket.hpp)
    InvalidAddress,    // not an IP address with a port, or one this socket cannot reach (IPv6 from IPv4)
    AddressInUse,      // bind: another socket holds the port
    ConnectionRefused, // connect: nothing listens at the address
    Unavailable,       // the system refused to create the socket (no IPv6, out of descriptors)
};
const char *errorName(Error error);

} // namespace Fern::Net
