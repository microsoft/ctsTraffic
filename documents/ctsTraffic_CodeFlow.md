# ctsTraffic — Code Flow

> Companion to `ctsTraffic_Architecture.md`. This document traces execution
> paths through the engine: startup, the per-connection state machine, the
> TCP IO loop, and the UDP media-stream flow. File/line citations refer to
> source under `ctsTraffic/ctsTraffic`.

## 1. Startup flow (`ctsTraffic.cpp : wmain`)

```
wmain(argc, argv)
 ├─ WSAStartup(WINSOCK_VERSION)
 ├─ ctsConfig::Startup(argc, argv)          // parse args → wire function pointers
 │     └─ (on failure) print help, return ERROR_INVALID_DATA
 ├─ SetConsoleCtrlHandler(CtrlBreakHandlerRoutine)   // Ctrl-Break → Shutdown(Rude)
 ├─ ctsConfig::PrintSettings(); PrintLegend()
 ├─ g_configSettings->StartTimeMilliseconds = snap_qpc_as_msec()
 ├─ broker = make_shared<ctsSocketBroker>()  // computes total/pending limits
 ├─ broker->Start()                          // seed the connection pool
 ├─ create status TP timer → ctsConfig::PrintStatusUpdate() every N ms
 ├─ broker->Wait(TimeLimit ? TimeLimit : INFINITE)   // block until done/ctrl-c
 ├─ (optional) PauseAtEnd
 ├─ ctsConfig::PrintStatusUpdate()           // final status line
 ├─ ctsConfig::Shutdown(Normal)
 ├─ print historic connection + byte/frame summary
 └─ return aggregate (connection + protocol) error count as exit code
```

`ctsConfig::Startup` is where the "which IO model" decision is made: it assigns
`CreateFunction`, `ConnectFunction`/`AcceptFunction`, `IoFunction`, and
(optionally) `ClosingFunction` (`ctsConfig.cpp:544-742`). The server also copies
`AcceptFunction` into `CreateFunction` and nulls `ConnectFunction`
(`ctsConfig.cpp:3396-3397`).

## 2. Broker: populating and refilling the pool (`ctsSocketBroker.cpp`)

```
ctsSocketBroker()                    // ctor
 ├─ if AcceptFunction (server): totalRemaining=ServerExitLimit, pendingLimit=AcceptLimit
 ├─ else (client): totalRemaining = Iterations*ConnectionLimit, pendingLimit=ConnectionLimit
 ├─ clamp pendingLimit ≤ totalRemaining
 └─ create manual-reset done-event

Start()  (under lock)
 └─ while totalRemaining>0 && pendingSockets<pendingLimit:
        (client also breaks at ConnectionThrottleLimit)
        pool.push_back(make_shared<ctsSocketState>(shared_from_this()))
        state->Start(); ++pendingSockets; --totalRemaining
```

Callbacks from each `ctsSocketState`:
- `InitiatingIo()` → `--pendingSockets; ++activeSockets;` then queue
  `RefreshSockets()`.
- `Closing(wasActive)` → decrement the appropriate counter, queue
  `RefreshSockets()`.

`RefreshSockets()` (on the `ctThreadpoolQueue`):
- If `totalRemaining==0 && pending==0 && active==0` → move whole pool out, then
  `SetEvent(done)` (this unblocks `wmain`'s `Wait`).
- Else erase any `Closed` states, and (unless the done-event is signaled) create
  new states to refill the pending window, respecting `ConnectionLimit` and
  `ConnectionThrottleLimit` for clients.

Destruction (`~ctsSocketBroker`): signal done, cancel the TP queue, then clear
the pool (destroys children before the CS).

## 3. Per-connection state machine (`ctsSocketState.cpp`)

State transitions run inside `ThreadPoolWorker` (a TP work item). Each call
handles one state; `CompleteState()` re-submits the work item for the next
state. The functor for a state is invoked *after* the state is advanced, so an
inline failure is attributed to the right stage.

```
Start() → SubmitThreadpoolWork → ThreadPoolWorker
```

### ThreadPoolWorker dispatch

```
Creating:
   m_socket = make_shared<ctsSocket>(shared_from_this())
   state = Created
   CreateFunction(m_socket)          // e.g. ctsWSASocket

Connecting:
   state = Connected
   ConnectFunction(m_socket)         // e.g. ctsConnectEx / ctsSimpleAccept

InitiatingIo:
   broker->InitiatingIo()            // pending → active
   m_socket->SetIoPattern()          // create ctsIoPattern via MakeIoPattern()
   state = InitiatedIo
   IoFunction(m_socket)              // e.g. ctsSendRecvIocp / MediaStream

Closing:
   update historic stats (success / protocol-error / connection-error)
   m_socket->CloseSocket(lastError)
   m_socket->PrintPatternResults(lastError)
   ClosingFunction(m_socket)         // if set
   state = Closed
   broker->Closing(initiatedIo)
```

### CompleteState(error) — the re-entry point (called by functors)

```
if error == NO_ERROR:
   Created      → Connecting (if ConnectFunction) else InitiatingIo (+active count)
   Connected    → InitiatingIo (+active count)
   InitiatedIo  → initiatedIo=true; Closing
   Closing/Closed → ignored (racing functor)
else:
   record lastError; if state>Connected mark initiatedIo; → Closing
then SubmitThreadpoolWork  // run the next state
```

So the happy path for a TCP client is:
`Creating → Created → Connecting → Connected → InitiatingIo → InitiatedIo →
Closing → Closed`, each arrow triggered by a functor calling `CompleteState`.

## 4. TCP data flow (`ctsSendRecvIocp.cpp`, representative)

Once `IoFunction` (e.g. `ctsSendRecvIocp`) is invoked with the socket:

```
ctsSendRecvIocp(weak_ptr<ctsSocket>)
 ├─ auto ref = socket->AcquireSocketLock()      // SOCKET + locked ctsIoPattern
 ├─ socket->IncrementIo()                        // hold the connection open
 └─ loop:
      task = pattern->InitiateIo()               // next ctsTask
      switch task.action:
        None                → break (no more work right now)
        GracefulShutdown/HardShutdown → do shutdown(), CompleteIo, continue
        Send/Recv:
          socket->IncrementIo()
          alloc ctThreadIocp OVERLAPPED request
          WSASend / WSARecv(...)
          if pending  → IOCP callback will finish later
          if inline (HandleInlineIocp) or failure:
              cancel request
              status = CompleteIo(task, bytes, error)   // process immediately
      ...
   finally: if DecrementIo()==0 and no more IO → CompleteState(patternError)
```

### Completion callback (per pended IO)

```
IOCP TP callback:
 ├─ WSAGetOverlappedResult → bytes, status
 ├─ status = pattern->CompleteIo(task, bytes, status)
 ├─ if status == ContinueIo → issue more IO (same loop as above)
 └─ if DecrementIo()==0 → socket->CompleteState(pattern->GetLastPatternError())
```

`ctsReadWriteIocp` (ReadFile/WriteFile) and `ctsRioIocp` (Registered IO) follow
the same pattern → CompleteIo → refcount → CompleteState contract; RIO differs
only in using a dedicated RIO completion queue bridged to an IOCP wakeup thread.

## 5. The pattern engine: InitiateIo / CompleteIo (`ctsIOPattern.cpp`)

`InitiateIo()` (`ctsIOPattern.cpp:250`) consults the protocol state machine
(`m_patternState.GetNextPatternType()`) and returns the right task:

```
MoreIo            → GetNextTaskFromPattern()   // derived pattern picks send/recv
SendConnectionId  → send the connection GUID (untracked)
RecvConnectionId  → recv the connection GUID (untracked)
SendCompletion    → EndStatistics(); send completion message
RecvCompletion    → EndStatistics(); recv completion message
HardShutdown      → EndStatistics(); HardShutdown task
GracefulShutdown  → EndStatistics(); GracefulShutdown task
RequestFin        → EndStatistics(); one final zero-byte recv (FIN)
NoIo              → empty task
→ m_patternState.NotifyNextTask(task)
```

`CompleteIo(task, bytes, status)` (`ctsIOPattern.cpp:363`):
- Recycles dynamic recv buffers and RIO buffer-ids back to their free lists.
- On error: sets last-error (unless it's an extra recv canceled after completion).
- On success: `m_patternState.CompletedTask()` advances the protocol machine,
  and — for tracked TCP recvs with `ShouldVerifyBuffers` — validates the received
  bytes against the expected bit-pattern (data-integrity check).
- Notifies the derived pattern via `CompleteTaskBackToPattern()`.
- Returns `ContinueIo` / `CompletedIo` / `FailedIo` derived from the last-error.

The **TCP protocol handshake** (framing around bulk data), driven by
`ctsIOPatternState`/`ctsIOPatternProtocolPolicy.hpp`, is roughly:
`[send/recv connection GUID] → [MoreIo bulk data until byte total met] →
[send/recv completion message] → [graceful/hard shutdown or FIN]`. This is how
both peers agree on the exact number of bytes and detect protocol errors
(TooFew/TooMany bytes, corrupted bit-pattern, missing GUID).

## 6. Protocol/pattern error propagation

- Pattern-level errors are encoded as sentinel status codes near `MAXINT`
  (`c_statusErrorNotAllDataTransferred`, `...TooMuchDataTransferred`,
  `...DataDidNotMatchBitPattern`), recognized by `ctsIoPattern::IsProtocolError`.
- On close, `ctsSocketState` classifies the connection outcome using
  `IsProtocolError(lastError)` into protocol-error vs connection-error vs
  success, and updates `ConnectionStatusDetails` counters.
- `wmain` sums connection + protocol errors as the process exit code.

## 7. UDP media-stream flow

### Client (`ctsMediaStreamClient.cpp` + `ctsIoPatternMediaStreamClient`)
```
ConnectFunction = ctsMediaStreamClientConnect
 └─ WSASendTo("START") to server; record addrs; CompleteState → InitiatingIo

IoFunction = ctsMediaStreamClient
 └─ post recvs (WSARecvFrom) for datagrams
    pattern->CompleteTaskBackToPattern():
       validate size + protocol header
       ID datagram   → store connection id
       DATA datagram → verify payload, record seq #, sender/receiver QPC/QPF
    renderer TP timer → RenderFrame(): classify frames
       successful (bytes==frameSize) / dropped (<) / duplicate (>) / error (stale)
       compute jitter from QPC/QPF deltas; update UdpStatusDetails
    start TP timer → retransmit START until frames arrive; FatalAbort if none
```

### Server (`ctsMediaStreamServer.cpp` + `ctsIoPatternMediaStreamServer`)
```
AcceptFunction = ctsMediaStreamServerListener
 └─ ctsMediaStreamServerImpl::InitOnceImpl() creates listening UDP socket(s)
    (one per shard if EnableRecvSharding, optional SIO_CPU_AFFINITY)
    each listener posts WSARecvFrom

On "START" arrival (RecvCompletion):
 └─ Start(socket, localAddr, remoteAddr):
       match a waiting ctsSocket in g_acceptingSockets  → accept it
       else park remote endpoint in g_awaitingEndpoints
    (demux is purely by source endpoint equality)

IoFunction = ctsMediaStreamServerIo
 └─ per client: ctsMediaStreamServerConnectedSocket (reuses listener socket)
    ScheduleTask(): <2ms → send now, else arm per-client TP timer
    MediaStreamTimerCallback():
       send DATA frame (ctsWSASendTo)
       pattern->CompleteIo(...); pattern->InitiateIo() for next frame
       chain until pattern yields no more / completes

ClosingFunction = ctsMediaStreamServerClose   // per-client cleanup
```

Server-side per-client sends are paced by a TP timer at the configured
frame/bit rate; the client buffers, renders on its own timer, and accounts for
loss/dupes/jitter. UDP counters live in `g_configSettings->UdpStatusDetails`.

## 8. Shutdown / Ctrl-C

- `CtrlBreakHandlerRoutine` (`ctsTraffic.cpp`) calls
  `ctsConfig::Shutdown(Rude)`, which signals the Ctrl-C handle that
  `ctsSocketBroker::Wait` also waits on, causing `wmain` to unwind.
- On normal completion the broker's `RefreshSockets` sets the done-event once
  all connections are drained.
- `~ctsSocketState` calls `ctsSocket::Shutdown()` (closes the socket and drains
  its TP callbacks) before canceling its own TP work — the ordering that
  prevents socket-extension races and self-deadlock described in the source
  comments.

## 9. End-to-end summary (TCP client, happy path)

```
wmain → Startup wires ctsWSASocket/ctsConnectEx/ctsSendRecvIocp
      → broker.Start() creates ctsSocketState #1..N
         each state (on TP threads):
           Creating   → ctsWSASocket    → CompleteState(0)
           Connecting → ctsConnectEx    → (IOCP) CompleteState(0)
           InitiatingIo → SetIoPattern (ctsIoPatternPush) + ctsSendRecvIocp
              InitiateIo → SendConnectionId → MoreIo* (1GB) → SendCompletion → shutdown
              each IO: WSASend/WSARecv → IOCP → CompleteIo → more or done
           on last IO → CompleteState(patternError)
           InitiatedIo → Closing → stats + CloseSocket + print → Closed
         broker refills pending window; when all done → done-event
      → wmain prints summary, returns error count
```
