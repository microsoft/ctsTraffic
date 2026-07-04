# ctsTraffic — Architecture

> Scope: source under `ctsTraffic/ctsTraffic`. This document describes the major
> components, how they fit together, and the key design patterns. See
> `ctsTraffic_CodeFlow.md` for the step-by-step runtime flow of a connection.

## 1. What ctsTraffic is

ctsTraffic is a highly scalable client/server networking test tool. It measures
throughput ("good-put"), connection reliability, and (for UDP) frame
loss/jitter. It is deliberately built to demonstrate Winsock best-practice,
scalable IO models. It supports **TCP** and a custom **UDP media-stream**
protocol, and can act as either **client** (connect out) or **server** (listen).

## 2. Design philosophy: a pluggable, function-pointer-driven engine

The core of ctsTraffic is a small, generic engine that is completely decoupled
from the concrete networking APIs it drives. Behavior is selected at startup by
populating five `std::function<void(std::weak_ptr<ctsSocket>)>` function
pointers on the global settings object (`ctsConfig::ctsConfigSettings`,
`ctsConfig.h:386-390`):

| Function pointer   | Stage           | Example implementations |
| ------------------ | --------------- | ----------------------- |
| `CreateFunction`   | create socket   | `ctsWSASocket` |
| `ConnectFunction`  | client connect  | `ctsConnectEx`, `ctsSimpleConnect`, `ctsConnectByName`, `ctsMediaStreamClientConnect` |
| `AcceptFunction`   | server accept   | `ctsAcceptEx`, `ctsSimpleAccept`, `ctsMediaStreamServerListener` |
| `IoFunction`       | data transfer   | `ctsSendRecvIocp`, `ctsReadWriteIocp`, `ctsRioIocp`, `ctsMediaStreamClient`, `ctsMediaStreamServerIo` |
| `ClosingFunction`  | teardown (opt.) | `ctsMediaStreamServerClose` |

These are wired up in `ctsConfig.cpp` (~lines 544-742) based on parsed command
line options. On the server the create stage reuses the accept function
(`ctsConfig.cpp:3396-3397`). Because the engine only calls through these
pointers, the same state machine drives every IO model.

## 3. Component layers

```
                    wmain (ctsTraffic.cpp)
                          |
                    ctsConfig  (global settings, function-pointer wiring,
                                logging, statistics, printing)
                          |
                    ctsSocketBroker      (connection pool / lifetime manager)
                          |
                    ctsSocketState[]     (per-connection state machine)
                          |
                    ctsSocket            (safe SOCKET container + locking)
                       /        \
        ctsIoPattern (what IO)    IO functors (how IO)
        - Push/Pull/PushPull/     - ctsWSASocket / ConnectEx / AcceptEx
          Duplex (TCP)            - SendRecvIocp / ReadWriteIocp / RioIocp
        - MediaStream Cli/Srv     - MediaStream client/server
```

### 3.1 Entry point — `ctsTraffic.cpp`
`wmain` performs `WSAStartup`, calls `ctsConfig::Startup` (parses args, wires
function pointers), installs a Ctrl-Break handler, prints settings/legend,
creates a single `ctsSocketBroker`, starts it, and waits on it (optionally
bounded by `TimeLimit`). On completion it prints historic connection/byte/frame
statistics and returns the aggregate error count as the process exit code.

### 3.2 Configuration & services — `ctsConfig.{h,cpp}`
The central singleton. Owns the global `g_configSettings` pointer and:
- Parses all command-line options and validates combinations.
- Selects/assigns the five function pointers (the "plug-in" selection).
- Holds the threadpool environment (`pTpEnvironment`), addresses
  (Listen/Target/Bind), limits (Iterations, ConnectionLimit, AcceptLimit,
  ConnectionThrottleLimit, ServerExitLimit), buffer/protocol/pattern settings,
  and UDP receive-sharding config.
- Provides shared services: socket creation helpers (`CreateSocket`,
  `SetPreBindOptions`, `SetPostConnectOptions`), statistics aggregation
  (`ConnectionStatusDetails`, `TcpStatusDetails`, `UdpStatusDetails`), and all
  console/CSV printing (`PrintSettings`, `PrintStatusUpdate`,
  `PrintConnectionResults`, `PrintSummary`, etc.).

### 3.3 Connection pool manager — `ctsSocketBroker.{h,cpp}`
Owns the population of connections and enforces the throttling policy.
- Computes total work: for a server, `ServerExitLimit` connections with
  `AcceptLimit` pending; for a client, `Iterations * ConnectionLimit` with
  `ConnectionLimit` pending (`ctsSocketBroker.cpp:33-63`).
- Holds a `std::vector<shared_ptr<ctsSocketState>>` pool guarded by a
  `wil::critical_section`.
- `Start()` seeds the pool up to the pending limit, respecting
  `ConnectionThrottleLimit` for outbound connections.
- Child `ctsSocketState` objects call back into the broker: `InitiatingIo()`
  (pending→active) and `Closing(wasActive)` (decrement counts). Both post a
  `RefreshSockets()` work item onto a `ctThreadpoolQueue`.
- `RefreshSockets()` scavenges `Closed` sockets and creates replacements to
  keep the pending window full, until no work remains — then signals the
  done-event.
- `Wait()` blocks on the done-event OR the Ctrl-C handle.

### 3.4 Per-connection state machine — `ctsSocketState.{h,cpp}`
Each connection is one `ctsSocketState`. It drives an 8-value state machine
(`InternalState`): `Creating → Created → Connecting → Connected →
InitiatingIo → InitiatedIo → Closing → Closed`. Key points:
- Runs its transitions on a **threadpool work item** (`ThreadPoolWorker`), so no
  single thread owns a connection; work hops between TP threads.
- `ThreadPoolWorker` invokes the correct config function pointer for the current
  state (Create/Connect/SetIoPattern+Io) and advances state *before* invoking
  the functor (so a synchronous failure is handled correctly).
- `CompleteState(error)` is the re-entry point after each stage: on success it
  advances the state and increments the active-connection counter; on error it
  jumps straight to `Closing`.
- The `Closing` stage updates historic statistics (success / protocol-error /
  connection-error), closes the socket, prints per-connection results, invokes
  the optional `ClosingFunction`, sets state `Closed`, and notifies the broker.
- Careful lifetime rules: closing runs on a separate TP thread so the socket is
  never destroyed while holding a lock / on its own callback thread.

### 3.5 Safe socket container — `ctsSocket.{h,cpp}`
Wraps one `SOCKET` with the locking, lifetime and IO-accounting needed by the
async engine.
- `AcquireSocketLock()` returns a `SocketReference` RAII object exposing the
  `SOCKET` and a locked `shared_ptr<ctsIoPattern>` — the only sanctioned way to
  touch the socket + pattern together.
- Owns the `ctsIoPattern` (`SetIoPattern()` creates it), the per-socket IOCP
  threadpool binding (`ctThreadIocp`, lazily created via `GetIocpThreadpool`),
  local/remote `ctSockaddr`, and a per-socket timer for scheduled tasks
  (`SetTimer`).
- `IncrementIo()/DecrementIo()` reference-count outstanding IO so the connection
  is only completed when the last IO finishes.
- `CompleteState(error)` forwards to the parent `ctsSocketState`.
- `CloseSocket()` is the only allowed way to close; direct `closesocket()` by
  callers is forbidden (would desync container state).

### 3.6 The IO pattern engine — `ctsIOPattern.{h,cpp}` (+ policies)
`ctsIoPattern` decides **what** IO to perform and validates results; it is
independent of **how** the bytes move (that's the IO functors). Contract:
- `InitiateIo()` returns the next `ctsTask` (buffer + action + offsets).
- `CompleteIo(task, bytesTransferred, statusCode)` reports a completion and
  returns `ctsIoStatus` (`ContinueIo` / `CompletedIo` / `FailedIo`).
- Callers may pipeline: call `InitiateIo()` multiple times before completions.
- All public methods are lock-protected.

Structure:
- Abstract base `ctsIoPattern` implements the buffer management, RIO buffer-id
  bookkeeping, rate limiting (bytes-per-quantum), the shared send/recv buffers,
  and error/last-error tracking.
- `ctsIoPatternStatistics<S>` templated layer binds a statistics type
  (`ctsTcpStatistics` or `ctsUdpStatistics`) and connection-id handling.
- Concrete patterns (in `ctsIOPattern.h`): `ctsIoPatternPull`,
  `ctsIoPatternPush`, `ctsIoPatternPushPull`, `ctsIoPatternDuplex` (all TCP),
  and `ctsIoPatternMediaStreamServer` / `ctsIoPatternMediaStreamClient` (UDP).
- `MakeIoPattern()` is the factory (`ctsIOPattern.cpp:96`) selecting the pattern
  from `IoPattern` + listening role.

The **protocol state machine** lives in the policy headers, driving the framing
around raw data transfer:
- `ctsIOPatternProtocolPolicy.hpp` / `ctsIOPatternState.hpp`: sequence of
  `ctsIoPatternType` values — send/recv connection GUID, MoreIo (bulk data),
  send/recv completion message, graceful/hard shutdown, request-FIN. This is how
  both sides agree on exact byte counts and detect protocol errors.
- `ctsIOPatternRateLimitPolicy.hpp`: throttles send rate to a target bitrate.
- `ctsIOPatternBufferPolicy.hpp`: buffer allocation strategy.

### 3.7 The IO task — `ctsIOTask.hpp`
`ctsTask` is the unit of work passed between pattern and functor: an action
(`Send`/`Recv`/`GracefulShutdown`/`HardShutdown`/`Abort`/`FatalAbort`/`None`), a
buffer + length + offset, an optional RIO buffer id, a time offset (for
scheduled/rate-limited sends), a buffer-type tag, and a `trackIo` flag
(whether it counts toward the transfer total and gets buffer-verified).

## 4. IO functors (the "how")

All functors share the same contract: they are invoked with a
`weak_ptr<ctsSocket>`, they drive the pattern via `InitiateIo`/`CompleteIo`,
they reference-count with `IncrementIo`/`DecrementIo`, and they call
`ctsSocket::CompleteState()` when their stage is done. See `ctsTCPFunctions.h`
for declarations.

- **Socket creation** — `ctsWSASocket.cpp`: creates the socket via
  `ctsConfig::CreateSocket`, applies pre-bind options, optional dual-mode IPv6,
  binds (with retry on fixed local ports), then `CompleteState`.
- **TCP connect** — `ctsConnectEx.cpp` (async `ConnectEx` + IOCP),
  `ctsSimpleConnect.cpp` (blocking `connect`), `ctsConnectByName.cpp`
  (`WSAConnectByNameW`).
- **TCP accept** — `ctsAcceptEx.cpp` (async `AcceptEx`, pre-posts ~100 accepts
  per listener, decouples accepted sockets from waiting `ctsSocket`s via internal
  queues), `ctsSimpleAccept.cpp` (blocking `accept` on a TP thread).
- **TCP data** — `ctsSendRecvIocp.cpp` (`WSASend`/`WSARecv` + IOCP),
  `ctsReadWriteIocp.cpp` (`ReadFile`/`WriteFile` + IOCP), `ctsRioIocp.cpp`
  (Registered IO with a dedicated RIO completion queue bridged to IOCP).
- **Shared winsock wrappers** — `ctsWinsockLayer.{h,cpp}`: `ctsWSARecvFrom`,
  `ctsWSASendTo`, `ctsSetLingerToResetSocket`; handles inline-completion
  (`HandleInlineIocp`) vs pended-IOCP semantics and IOCP request
  allocation/cancellation.

### Inline vs pended completions
Every IOCP path supports an optimization: when a Winsock call completes inline
(`HandleInlineIocp`), the functor cancels the pended TP request and processes the
result immediately, instead of waiting for the IOCP callback. Otherwise the
`ctThreadIocp` callback runs later on a TP thread. Both funnel into the same
`CompleteIo` → maybe start more IO → `DecrementIo` → on zero, `CompleteState`.

## 5. UDP media-stream subsystem

A custom UDP protocol for measuring frame loss/jitter (see also
`documents/UDP_Receive_Sharding.md`).

- **Wire protocol** — `ctsMediaStreamProtocol.hpp`: DATA datagrams (flag
  `0x0000`) carry a sequence number, sender QPC timestamp, sender QPF, and
  payload; ID datagrams (flag `0x1000`) carry a connection id; a plaintext
  `"START"` control message begins negotiation.
- **Client** — `ctsMediaStreamClient.{h,cpp}` + pattern
  `ctsIoPatternMediaStreamClient`: sends `START`, receives the datagram stream,
  and classifies each frame as successful / dropped / duplicate / error, with
  jitter computed from QPC/QPF deltas. Uses two TP timers: a start-retransmit
  timer and a renderer timer that processes frames after a buffering window.
- **Server** — `ctsMediaStreamServer.{h,cpp}` (`ctsMediaStreamServerImpl`),
  `ctsMediaStreamServerListeningSocket.*`, `ctsMediaStreamServerConnectedSocket.*`
  + pattern `ctsIoPatternMediaStreamServer`: one or more listening UDP sockets
  demultiplex clients by source endpoint. A `START` matches a waiting `ctsSocket`
  (or is parked in `g_awaitingEndpoints`). Each client session is a
  `ctsMediaStreamServerConnectedSocket` that reuses the listener socket and owns
  a per-client TP timer which paces frame transmission at the configured rate.
- **Sharding** (`EnableRecvSharding`): multiple listener sockets, one per shard,
  optionally pinned to CPUs via `SIO_CPU_AFFINITY`, each with its own IOCP shard.

## 6. Statistics & logging

- `ctsStatistics.hpp`: `ctsConnectionStatistics`, `ctsTcpStatistics`,
  `ctsUdpStatistics` — lock-free-ish counters using QPC timestamps; connection
  ids generated for server-side patterns.
- `ctsLogger.hpp`, `ctsPrintStatus.hpp`: pluggable console + CSV loggers.
  `ctsConfig` prints periodic status updates (driven by a threadpool timer in
  `wmain`), per-connection results, and end-of-run summaries.

## 7. Concurrency model

- **No dedicated per-connection thread.** Everything runs on the Windows
  threadpool: `ctsSocketState` transitions on TP work items, IO completions on
  `ctThreadIocp` TP threads, scheduled/paced sends on TP timers, and the broker's
  refresh on a `ctThreadpoolQueue`.
- **Locking discipline:** `ctsSocket` holds a `critical_section`; the pattern
  holds its own lock; `SocketReference`/`AcquireIoPatternLock` provide the
  sanctioned ways to acquire them. IO accounting (`IncrementIo`/`DecrementIo`)
  plus the "close on a separate TP thread" rule prevent premature destruction
  and self-deadlock.
- **Lifetime:** `shared_ptr`/`weak_ptr` throughout —
  `enable_shared_from_this` on broker, state, socket, and pattern; timers and
  callbacks hold `weak_ptr` so scheduled work never keeps an object alive.

## 8. Key files quick reference

| File | Responsibility |
| ---- | -------------- |
| `ctsTraffic.cpp` | Process entry point / lifecycle |
| `ctsConfig.{h,cpp}` | Settings, option parsing, function-pointer wiring, printing, stats aggregation |
| `ctsSocketBroker.{h,cpp}` | Connection pool, throttling, completion signaling |
| `ctsSocketState.{h,cpp}` | Per-connection state machine on the threadpool |
| `ctsSocket.{h,cpp}` | Safe SOCKET container, locking, IO accounting, timers |
| `ctsIOPattern.{h,cpp}` | IO pattern engine (what to send/recv) + factory |
| `ctsIOPatternState.hpp`, `ctsIOPattern*Policy.hpp` | Protocol/rate/buffer state machines |
| `ctsIOTask.hpp` | Unit of IO work |
| `ctsWSASocket / ctsConnectEx / ctsSimpleConnect / ctsConnectByName` | Create + connect functors |
| `ctsAcceptEx / ctsSimpleAccept` | Accept functors |
| `ctsSendRecvIocp / ctsReadWriteIocp / ctsRioIocp` | TCP data functors |
| `ctsWinsockLayer.{h,cpp}` | Low-level winsock wrappers, inline-IOCP handling |
| `ctsMediaStream*` | UDP media-stream client/server + protocol |
| `ctsStatistics.hpp`, `ctsLogger.hpp`, `ctsPrintStatus.hpp` | Metrics + output |
