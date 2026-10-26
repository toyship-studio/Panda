# Networking from scripts

`#include <Bamboo/Net.hpp>` — HTTP, TCP and UDP for a script. Pick by what you need:

| You need | Use | What it is |
|---|---|---|
| An answer from a server: a login, a leaderboard, a JSON API, a file | **HTTP** `Bamboo::Net::Http` | One request, one `Response`. `https://` only (`http://` to localhost while developing) |
| A conversation between two sides where nothing may be lost: a chat, a lobby, a turn-based game, your own server | **TCP** `TcpConnection`, `TcpServer` | A connection: an ordered stream of bytes both ways |
| Many small messages fast, where a lost one does not matter: positions, finding games in the local network | **UDP** `UdpSocket` | Single datagrams, no connection; they may arrive out of order or not at all |

The rules are the same for all three:

- Every call returns at once; nothing blocks the game.
- Every callback — an answer, a connection, received bytes, a close — runs on the main thread,
  once per frame, before the `update()` of the scripts. You may touch the world from it.
- A connection, a server or a socket lives while a handle to it exists. Keep the handle as a field
  of the script; copies share it; when the last one is gone, it closes. Stop and the unload of the
  scripts close everything, and nothing is called after that.
- Errors are one list, `Bamboo::Net::Error`; `errorName(error)` gives a word for a log line.

## HTTP

```cpp
#include <Bamboo/Net.hpp>

Bamboo::Net::Http::get("https://example.com/api/scores", [this](Bamboo::Net::Response &&r) {
    if (!r.ok()) { LOG_INFO("scores: %s, status %d", Bamboo::Net::errorName(r.error), r.status); return; }
    parseScores(r.bodyText());
});

Bamboo::Net::Http::post("https://example.com/api/score", R"({"score": 120})", "application/json",
    [](Bamboo::Net::Response &&r) { LOG_INFO("saved: %d", r.status); });

Bamboo::Net::Http::download("https://example.com/levels/pack1.bin", savePath,
    [](Bamboo::Net::Response &&r) { /* r.ok(): the file is in place */ },
    [](const Bamboo::Net::Progress &p) { /* p.received, p.total */ });
```

- A `Response` has `ok()` (no error and a 2xx status), `status`, `bodyText()` or `body`
  (bytes), `header("Content-Type")`, `error`. An HTTP status is not an error: a 404 comes with
  `error == None` and `status == 404`.
- `Http::send(Request, …)` takes the full `Request`: method, headers (`{"Authorization",
  "Bearer …"}`), a body or an `uploadFile`, `timeouts` (15 s to connect, 30 s without a byte),
  `maxBodyBytes` (16 MB in memory), `allowInsecure`. `Multipart` builds a form with files.
- A download goes to `<path>.part` and takes its name only when complete; `Request::resume`
  continues an interrupted one, `expectedSha256` checks it.
- Every call returns a `RequestHandle`: `cancel()` means the callback never comes.
- Certificates are checked by the system; there is no switch to accept a bad one. `http://` works
  only for localhost unless `Request::allowInsecure` is set.
- `Http::setUserAgent("MyGame/1.0")` before the first request names the game to the server.

## TCP: a connection

```cpp
class NetClient : public Bamboo::Script {
    Bamboo::Net::TcpConnection m_connection;

    void start() override {
        m_connection = Bamboo::Net::TcpConnection::connect("game.example.com", 7777, {
            .connected = [this] { m_connection.send("hello\n"); },
            .received = [this](std::vector<uint8_t> &&bytes) { m_buffer.append(Bamboo::Net::textOf(bytes)); parseLines(); },
            .closed = [](Bamboo::Net::Error error) { LOG_INFO("connection closed: %s", Bamboo::Net::errorName(error)); },
        });
    }
};
```

- `connect` takes a host name or an address text (`"192.168.1.20"`). A name is resolved in the
  background; every address of it is tried in turn, 15 s each. `isConnecting()`, `isConnected()`.
- `send` queues the bytes; what is sent before `connected` leaves when the connection opens.
  `received` brings whatever arrived since the last frame, in order — TCP is a stream, so one
  `send` may arrive in several pieces and several sends in one: put a length or a line break in
  your protocol and cut messages yourself (`textOf`, `bytesOf` help with text).
- `closed` comes once: `None` — the other side ended the connection; `HostNotFound`,
  `ConnectionRefused`, `Timeout`, `ConnectionFailed` — why it did not work or broke. After your own
  `close()` nothing is called.
- More than 4 MB waiting to be sent closes the connection with `TooLarge`: a peer that does not
  read is gone.

## TCP: a server

```cpp
class LobbyServer : public Bamboo::Script {
    Bamboo::Net::TcpServer m_server;

    void start() override {
        Bamboo::Net::Error error;
        m_server = Bamboo::Net::TcpServer::listen(7777, {
            .connected = [](Bamboo::Net::TcpConnection client) { client.send("welcome\n"); },
            .received = [this](Bamboo::Net::TcpConnection client, std::vector<uint8_t> &&bytes) {
                for (Bamboo::Net::TcpConnection &other : m_server.clients()) { if (other != client) { other.send(bytes); } }
            },
            .disconnected = [](Bamboo::Net::TcpConnection client, Bamboo::Net::Error) { LOG_INFO("client %llu left", client.id()); },
        }, &error);
        if (!m_server) { LOG_INFO("cannot listen: %s", Bamboo::Net::errorName(error)); } // AddressInUse: the port is taken
    }
};
```

- The server listens on every interface, IPv4 and IPv6 at once. `port()` tells the port (useful
  with `listen(0, …)`, a free one), `clients()` the connections alive, `close()` ends them all.
- Each client is a `TcpConnection`: `send`, `close`, `id()` and `remote()` to tell them apart.
  `disconnected` comes when a client leaves (`None`) or its connection breaks, not after your
  `close()`.

## UDP

```cpp
Bamboo::Net::UdpSocket m_socket;

m_socket = Bamboo::Net::UdpSocket::open(7777, [](Bamboo::Net::Datagram &&d) {
    LOG_INFO("%zu bytes from %s", d.bytes.size(), d.from.toString().c_str());
});
m_socket.sendTo(*Bamboo::Net::Address::parse("192.168.1.20:7777"), positionBytes);

// Finding games in the local network: open with broadcast, shout, answer the sender.
m_discovery = Bamboo::Net::UdpSocket::open(0, [this](Bamboo::Net::Datagram &&d) { m_servers.push_back(d.from); }, true);
m_discovery.broadcast(7777, Bamboo::Net::bytesOf("anyone?"));
```

- `open(port, received)` listens on the port of every interface (IPv4); port 0 takes a free one
  for a client. `open(address, …)` binds one address: an IPv6 one receives IPv4 too.
- `sendTo` sends one datagram; `false` means it could not be sent (the socket is closed, or the
  system refused). A datagram that does not arrive is normal for UDP — keep messages small (under
  1200 bytes stays whole on every network) and repeat what matters.
- UDP has no connection and so no "closed": a server that went away is silence.
- A host name has to be resolved first: `Bamboo::Net::resolve("game.example.com", 7777,
  [](std::vector<Bamboo::Net::Address> &&addresses, Bamboo::Net::Error) { … })`.

## Addresses and errors

- `Address::parse("192.168.1.20:7777")`, `"[::1]:7777"`, `"10.0.0.1"` (port 0); `toString()`,
  `ipString()`, `port`. A host name is not an address.
- `Error`: `HostNotFound`, `ConnectionRefused` (nothing listens there), `ConnectionFailed`
  (the connection broke), `Timeout`, `AddressInUse` (the port is taken), `InvalidAddress`,
  `Unavailable` (the system refused the socket), `TooLarge`; HTTP adds `InvalidUrl`,
  `InsecureUrl`, `TlsFailed`, `IntegrityFailed`, `FileError`, `Protocol`, `Offline`.

## What is not here

- WebSocket and the state of the network (online, metered) come later (`context/networking.md`).
- No TLS on TCP and UDP: HTTPS covers encryption; a game protocol over sockets is in the clear.
- No netcode: reliable UDP, replication, prediction and lobbies are a module on top of these
  sockets when a game needs them.
- On Linux a game needs the system `libcurl4` for HTTP (on practically every desktop and in the
  Steam runtime); TCP and UDP need nothing.
