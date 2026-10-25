# Networking: the network layer of Fern

The one network layer that the editor, the engine and games share: HTTP, WebSocket, UDP and TCP
sockets, and the state of the network. HTTP exists (section 3, the contract); the rest is the
plan. The editor auto-update is the first consumer (`infrastructure.md`, section 5), the plugin
store the second (`infrastructure.md`, section 3, "What the engine needs first").

## 1. Scope

- One layer in Fern, under the MIT License like the rest of Fern, so it is usable without the
  engine as well.
- Four parts: HTTP (section 3), WebSocket (4), sockets (5) and the state of the network (6).
- Not in Fern: game netcode (reliable UDP, replication, RPC, prediction, lobbies). It becomes a
  module on top of the sockets when a game needs it. Services such as leaderboards or cloud saves
  are written by a game or a plugin on top of HTTP.
- Not in this layer: unpacking archives (zip, tar.gz) for the updater and the plugin store; it is
  decided with the updater. SHA-256 lives in Foundation; the layer uses it to check downloads.

## 2. Decisions

1. **The network stacks of the operating systems, no TLS of our own.** Certificates are checked
   against the trust store of the system, which updates with the system: a list of roots shipped
   inside a program grows old (the aws cli of the release workflow could not reach the storage
   because its own list lacked the GlobalSign R6 root, `infrastructure.md`). The system stacks
   also bring the proxy settings, HTTP/2 and connection reuse, and add nothing to the size of a
   game.

   | Platform | HTTP and WebSocket | Sockets | Network state |
   |---|---|---|---|
   | macOS, iOS | NSURLSession, `NSURLSessionWebSocketTask` | BSD sockets | `NWPathMonitor` |
   | Windows | WinHTTP in asynchronous mode, its WebSocket API | Winsock 2 | Network List Manager |
   | Linux | libcurl of the system (`libcurl4`, the OpenSSL build) | BSD sockets | open item |
   | Android (later) | `HttpURLConnection` through JNI, or libcurl | BSD sockets | `ConnectivityManager` |

2. **Part of Fern, not a library of its own.** The network code lives in the `Fern` target like the
   windows and the events: headers in `Fern/include/Fern/Net/`, namespace `Fern::Net`. Fern gains
   two dependencies, PandaAsync (MIT) and, on Linux, the libcurl of the system, and everything
   that links Fern carries them: on macOS, iOS and Windows the stacks are frameworks and DLLs that
   every system has; on Linux `libcurl4` is on practically every desktop and in the Steam runtime,
   and a Linux build needs `libcurl4-openssl-dev`, one more entry in the package list of the
   README. Decided in October 2026 over a separate `FernNet` target: the dependency a separate
   target would save is small, and one target keeps the SDK and `Bamboo::Net` free of optional
   components. Built on its own (the Fern mirror), Fern fetches PandaAsync through FetchContent of
   its mirror (`infrastructure.md`, decision 12).

3. **Asynchronous through PandaAsync.** A `Client` is created with a `PandaAsync::Scheduler`. The
   threads of a platform stack (the delegate queue of NSURLSession, the pool of WinHTTP, the
   libcurl thread of the client) never call user code: they post completions to the scheduler,
   and `processCompletions` runs them on the main thread within the frame budget, like the
   completions of jobs. PandaAsync gets one function for this, `Scheduler::post(JobDesc,
   std::function<void()>)`: a completion queued from any thread without a worker job, dropped when
   it or its group is canceled. The rules of jobs hold: a canceled request never calls
   `complete`; a failed one does, with the error.

4. **One client per owner, nothing global.** The editor owns one (the engine scheduler of
   `Application`, created on first use). A script bundle owns its own with its own scheduler, so
   unloading a bundle cancels and drains only its requests, the rule of `Bamboo::Async`
   (`architecture.md`, "Multithreading"). The engine core (`Panda`, `PandaRuntime`) creates
   none: nothing in a running game uses the network unless its scripts ask for it. The
   connections of a client are shared by its requests.

5. **Secure by default.** Only HTTPS and WSS. `http://` and `ws://` need `allowInsecure` on the
   request, except the loopback addresses (`localhost`, `127.0.0.1`, `::1`). Certificates are
   always verified: there is no switch to turn the check off and no way to accept a certificate
   the system rejects. A redirect from HTTPS to HTTP fails. iOS App Transport Security agrees with
   this default.

6. **Limits.** A body in memory is at most 16 MB unless the request raises it; larger answers go
   to a file. Timeouts: 15 s to connect, 30 s without a byte, no total limit by default. At most
   10 redirects.

7. **No HTTP cache and no cookie jar.** Every request goes to the network (NSURLSession runs with
   an ephemeral configuration, so all platforms behave alike: the update manifest is never stale).
   Authentication is a header the caller sets (the portal API takes `Authorization: Bearer`).

8. **Downloads into a file are atomic and verifiable.** The bytes go to `<path>.part`; the file is
   renamed into place only after the transfer and the checks. A resume continues the `.part` with
   `Range` and `If-Range` (the ETag or `Last-Modified` saved next to it); a server that answers
   200 instead of 206 restarts from the first byte. The expected size and SHA-256 given by the
   caller are checked while the bytes arrive (a resumed file is hashed from its first byte); a
   mismatch is an error and the `.part` is deleted.

9. **Progress is coalesced:** at most one progress callback per request and frame, with the latest
   numbers.

10. **User-Agent** `PandaEngine/<ENGINE_VERSION> (<os>; <arch>)` for the engine and the editor; a
    game sets its own in the description of its client.

11. **Tests stay minimal.** One smoke executable, `FernNetSmoke`, with a loopback HTTP server
    inside it (plain sockets of the test, no internet): a request into memory, a download into a
    file with resume and a SHA-256 check, cancellation, a refused `http://` address. CI builds it
    with the other targets and runs it only in the macOS job, next to the existing smoke test.

## 3. HTTP (implemented)

`Fern/include/Fern/Net/Http.hpp`; the shared part in `Fern/src/Net/` (`HttpInternal.hpp`,
`HttpTransfer.cpp`, `HttpClient.cpp`), the backends in `Fern/src/Net/Apple/` (NSURLSession, the
file is compiled with ARC), `Windows/` (WinHTTP, asynchronous) and `Linux/` (libcurl, a multi
handle on a thread of the client). Foundation has `Hash/Sha256.hpp`, PandaAsync has
`Scheduler::post`.

- `Client(scheduler, ClientDesc{userAgent, maxConnectionsPerHost})`, `send(Request, complete,
  progress, group) -> RequestHandle`, `cancelGroup`, `cancelAll`; `RequestHandle::cancel`,
  `isActive`. `Request`: method, url, headers, body or `uploadFile`, `downloadTo`, `resume`,
  `expectedSize`, `expectedSha256`, `maxBodyBytes` (16 MB), `Timeouts{connect 15 s, idle 30 s,
  total 0}`, `allowInsecure`. `Response`: `error`, `status`, `headers`, `body`, `finalUrl`,
  `ok()`, `header(name)`, `bodyText()`. `Progress{received, total}`. `Multipart` builds a form
  body. `Error`: InvalidUrl, InsecureUrl, Offline, HostNotFound, ConnectionFailed, TlsFailed,
  Timeout, TooLarge, IntegrityFailed, FileError, Protocol.
- The client owns a launch thread: `send` only validates the address and queues; the thread opens
  the sink of a download (hashing an existing `.part` on a resume) and hands the request to the
  stack, so the main thread never waits for a file. The stack reports on its own threads to a
  shared `Transfer`, which writes, hashes and checks, and posts `complete` and `progress` with
  `Scheduler::post`; a canceled request posts nothing. The destructor cancels everything and waits
  until the stack has let go of every request.
- An address error (InvalidUrl, InsecureUrl) completes like any other error, on the main thread.
- The resume validator (the ETag, or Last-Modified) is kept in `<path>.part.validator`; a server
  that answers 200 to a Range request restarts the file from the first byte. A broken transfer
  keeps the `.part` for a resume; an integrity or size failure deletes it.
- Platform notes: on Apple platforms the idle timeout covers connecting too (NSURLSession has one
  request timeout); the total timeout is enforced by the transfer as bytes arrive, so a silent
  connection ends by the idle timeout. The default User-Agent of Fern is `Fern (<os>; <arch>)`;
  the editor passes `PandaEngine/<version> (<os>; <arch>)` in `ClientDesc`.
- `FernNetSmoke` (`Fern/Smoke/NetSmoke.cpp`, `scripts/fern_net_run_smoke.*`): SHA-256 known
  answers, a request into memory, a 404, a POST, a download broken after 300000 bytes and its resume
  with the Range and If-Range headers and the hash check, an integrity failure, cancellation, the
  refused `http://` address, the destructor with a request in flight. CI builds it on every
  platform and runs it in the macOS job. Linux builds need `libcurl4-openssl-dev`; the editor and
  the games on Linux need `libcurl4` at run time (the SDK config links `curl`, and `winhttp` on
  Windows).

## 4. WebSocket

```cpp
struct WebSocketCallbacks {
    std::function<void()> opened;
    std::function<void(std::string &&text)> text;
    std::function<void(std::vector<uint8_t> &&bytes)> binary;
    std::function<void(uint16_t code, std::string &&reason, Error error)> closed;
};

class WebSocket {
public:
    void sendText(std::string text);
    void sendBinary(std::vector<uint8_t> bytes);
    void close(uint16_t code = 1000, std::string reason = {});
    bool isOpen() const;
};

// A member of Client:
WebSocket openWebSocket(std::string url, WebSocketCallbacks callbacks,
                        std::vector<Header> headers = {}, PandaAsync::GroupId group = 0);
```

- Messages arrive whole, on the main thread, in order. Ping and pong are answered by the stack.
- Sending queues the message; a queue over the limit (4 MB) closes the socket with an error
  instead of growing without bound.
- `closed` comes exactly once, also after a failure to connect.
- Backends: `NSURLSessionWebSocketTask` (macOS 10.15, iOS 13), the WebSocket API of WinHTTP
  (Windows 8). libcurl has its WebSocket API since 7.86, while Ubuntu 22.04, the oldest Linux the
  editor supports, ships 7.81: open item.

## 5. Sockets

```cpp
struct Address {                          // IPv4 or IPv6 and a port
    static std::optional<Address> parse(std::string_view text);
    std::string toString() const;
};

// getaddrinfo blocks, so resolving is a job of the scheduler.
PandaAsync::JobHandle resolve(PandaAsync::Scheduler &scheduler, std::string host, uint16_t port,
                              std::function<void(std::vector<Address> &&, Error)> complete);

class UdpSocket {
public:
    static std::optional<UdpSocket> bind(const Address &local, bool broadcast = false);
    bool sendTo(const Address &to, std::span<const uint8_t> bytes);
    std::optional<Datagram> receive();    // never blocks; empty when nothing waits
};

class TcpListener;                        // bind, accept without blocking
class TcpStream;                          // connect in the background, then send and receive
```

- Sockets are polled by their owner, without callbacks: a game drains them in its own tick, where
  latency matters more than convenience. Only `resolve` and connecting a `TcpStream` work in the
  background.
- IPv4 and IPv6, dual stack where the system allows it. UDP broadcast for finding games in a local
  network.
- No TLS on raw sockets: HTTPS and WSS cover TLS, and a netcode module protects its own packets.

## 6. The state of the network

```cpp
enum class Connectivity : uint8_t { Unknown, Offline, Online };

struct NetworkState {
    Connectivity connectivity = Connectivity::Unknown;
    bool expensive = false;               // a mobile or metered connection
    bool constrained = false;             // a low data mode of the system
};

// Members of Client: the current state and its changes on the main thread.
NetworkState networkState() const;
void watchNetworkState(std::function<void(const NetworkState &)> changed);
```

The updater waits for `Online` instead of failing in a loop, and does not download a build over an
expensive connection without asking.

## 7. Who uses it

- **The editor.** The updater (`infrastructure.md`, section 5): the manifest from the portal with
  the `panda-builds` fallback, the build through `/dl/` with resume and the SHA-256 of the manifest.
  Later the plugin store, publishing a plugin (a multipart upload with the publish token), the news
  feed and the telemetry (`infrastructure.md`, section 6).
- **The engine core.** Nothing. `Panda` and `PandaRuntime` create no client; the editor's client
  runs on the engine scheduler, which `Application::tick` pumps.
- **Games.** `Bamboo::Net` (`PandaSDK/include/Bamboo/Net.hpp`): a thin layer over `Fern::Net`
  inside the script bundle, in the manner of `Bamboo::Async`. The client of the bundle is created
  lazily with the scheduler of the bundle; the engine drives it through the hooks of
  `Bamboo::Async`: the completions of requests are drained with those of jobs (the same budget),
  `async_CancelAll` also aborts the requests and closes the WebSockets and sockets of the bundle,
  and `async_WaitAll` destroys the client before the scheduler. The API: `get`, `send`,
  `download`, WebSocket and sockets as those parts arrive; a `Bamboo::Async::Group` held by a
  script also cancels the requests sent with its id. It is always in the SDK, like
  `Bamboo::Async`; a game that never calls it pays nothing but the libcurl link on Linux. Its page
  in `PandaSDK/docs/` comes with the first phase that exposes it.

## 8. Build

- macOS and iOS: the system frameworks Foundation (NSURLSession) and Network (`NWPathMonitor`).
- Windows: `winhttp`, `ws2_32` and, for the network state, `ole32`.
- Linux: `find_package(CURL)` in Fern, `curl` in the link libraries of the SDK config. The CI and
  release jobs install `libcurl4-openssl-dev` (the mirror job of Fern will too); the editor archive
  and every Linux game need `libcurl4` at run time, written into the requirements of the README
  and of `panda-builds`.

## 9. Phases

1. HTTP: done (section 3). Its first consumer is the editor updater.
2. `Bamboo::Net` for HTTP and its page in `PandaSDK/docs/`.
3. WebSocket.
4. Sockets and `resolve`.
5. The state of the network.

A finished phase turns its part of this document into a compact contract; the plan of the rest
stays.

## 10. Open items

- WebSocket on Linux with libcurl 7.81 (Ubuntu 22.04): RFC 6455 framing of our own over a
  `CURLOPT_CONNECT_ONLY` connection of libcurl, which keeps TLS in libcurl, or a newer minimum for
  this one feature.
- The network state on Linux: route events of netlink for connectivity, NetworkManager over D-Bus
  for metered connections.
- The Android backends.
- Certificate pinning for the portal API: not needed while the trust store of the system decides.
