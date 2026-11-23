# Goals and Scenario Catalog

Status: **Draft**. This catalog defines what the runs are intended to prove.
Concrete ctsTraffic command lines, parameter values, thresholds, and
automation are to be added after the open decisions in
[the plan index](README.md) are answered.

The initial VM scenarios are TCP-only, starting with two Hyper-V guests on one
link and growing to three, with a maximum of five VMs. UDP is deferred until
the TCP workflow works end to end. Select and record the Windows build and
architecture for each
campaign. IPv4-only and
IPv6-only configurations are both required. Dual-stack runs are blocked until
ctsTraffic implements the necessary support. Start with the duplex IO pattern.
Vary connection count and transfer duration independently and in combinations,
calibrating load per system and campaign. Goal 1 and Goal 3 TCP scenarios, and
every post-stress verification check, must explicitly use `-Verify:data`.
Goal 2 stress scenarios may use `-Verify:connection` when the stress technique
requires it (for example, protocol-level RST injection that aborts transfers
mid-stream); each such scenario must state the verification mode and why.

There are no custom drivers in this initial scope; bare-metal test coverage
will need explicit driver installation references and is tracked as a later
extension.

## Goal 1 — Repeatable workload reliability

**Customer promise:** for a declared IO pattern, protocol, topology, and
configuration, socket applications reliably complete the expected work and
consistently perform the same amount of work over a defined duration.

This goal has two complementary run variants. Do not merge their acceptance
criteria or compare their performance numbers directly.

### 1A. Instrumented correctness

- Enable the `AV-DV-CORRECTNESS` [verifier profile](verifier-profiles.md).
  1A never injects faults: no low-resource simulation, network faults, or
  protocol resets. Every connection, protocol, or data failure is unexpected.
- Run the declared ctsTraffic workload to exercise connection establishment,
  data transfer, protocol completion, and teardown.
- Explicitly enable ctsTraffic data verification (`-Verify:data`) so received
  data is checked for corruption.
- Capture verifier diagnostics, bugchecks, process exit status, ctsTraffic
  connection/protocol/data errors, and system event/crash evidence.
- Judge this variant on correctness and diagnostics, not throughput, elapsed
  work rate, or memory stability: verifier overhead changes those observations.
- Fail on an unexpected verifier finding, bugcheck, data/protocol corruption,
  any connection failure, or incomplete run.

### 1B. Uninstrumented work and memory baseline

- Disable Application Verifier and Driver Verifier.
- Run the same declared workload with `-Verify:data` for a fixed duration and
  capture work over time (for example completed connections and transferred
  bytes/frames), as well as ctsTraffic errors and process exit status.
- **Fixed duration uses ctsTraffic `-TimeLimit`.** When the limit is reached,
  ctsTraffic prints `Time-limit of N reached`, stops, still writes its final
  status and summary, and returns the count of network plus protocol errors as
  its exit code. Treat the time-limit notice as the expected end of the run.
  Connections still in flight at the cutoff are not completed work; the pilot
  must confirm whether they are counted as errors, and any such cutoff errors
  are classified as expected end-of-run effects rather than failures. Compare
  work by completed connections and bytes, normalized per unit time.
- Capture the work-over-time series with `-StatusFilename` (and optionally
  `-TcpInfoFilename`) so throughput and errors can be compared across the run,
  not only as end totals.
- Detect user-mode and kernel-pool leaks with the
  [leak detection workflow](leak-detection.md): persistent servers, a noise
  floor, a discarded warm-up, and five measured identical iterations analyzed
  for steady growth normalized by work.
- Compare repeated runs only under the same OS, driver, topology, workload,
  duration, and host-capacity conditions.
- When connection success rate is an acceptance metric, enable and retain
  ctsTraffic per-connection CSV results (`-ConnectionFilename`) so each
  connection outcome is accounted for.
- Fail on unexpected ctsTraffic errors, incomplete work, a repeatability
  regression beyond the agreed tolerance, or memory growth beyond the agreed
  threshold after accounting for expected warm-up and system noise.

The initial concrete instance is
[G1B-CLEAN-BASELINE](scenarios/goal1b-clean-baseline.md). Its first execution
is a pilot to establish workload and memory observations; repeatability and
leak thresholds remain open until calibrated and approved.

Application Verifier and Driver Verifier settings are defined by name in
[verifier profiles](verifier-profiles.md). Fixed duration uses `-TimeLimit`
and leak analysis uses the [leak detection workflow](leak-detection.md); the
workload parameters, duration value, work-rate comparison method and
tolerance, and leak thresholds are open decisions. Process memory and kernel
pool use are distinct measurements and must have separate baselines and
criteria.

### Workload sizing

Do not use one global connection count as the definition of load. Calibrate
connection count and transfer duration for each system and campaign, against
the capacity and configuration of the specific system under test. The scenario
matrix should include high connection counts with short transfers, low
connection counts with long transfers, and selected combinations, then expand
the combinations needed to expose regressions.

Before a run, select a compatible existing workload configuration or use
optional system characterization to derive one. Validate capacity and declare
the intended load level, target workload, upper resource/safety bounds, and
stop conditions. Use
measured system behavior to show that the scenario applied enough pressure to
meet its goal without exceeding those bounds. A run below its declared stress
target is not evidence of resilience; a run beyond its approved envelope is
an aborted/unsafe run, not a stronger pass. Exact sizing methodology,
monitored signals, and thresholds remain to be set per system class. The
signals and thresholds are TBD and must be resolved before automation can
enforce this gate.

## IO implementation coverage

IO implementation is a separate dimension from protocol, IO pattern, address
family, connection count, and transfer duration. Record the selected model in
every run and compare models only with otherwise equivalent configurations.

| Model | Current ctsTraffic state | Run-matrix note |
| --- | --- | --- |
| Winsock overlapped IOCP (`WSASend`/`WSARecv`, `-IO:iocp`) | Implemented | Include in initial TCP coverage. |
| Registered I/O (`RIO`, `-IO:rioiocp`) | Implemented | Expand to this as a distinct IO model after the IOCP baseline; do not combine its results with default IOCP results. |
| File API overlapped IOCP (`ReadFile`/`WriteFile`, `-IO:ReadWriteFile`) | Implemented | Expand to this as a distinct IO model after the IOCP baseline, where supported by the selected workload. |
| Nonblocking sockets with `select` | Future ctsTraffic addition | Add after implementation and validation. |
| `WSAEventSelect` | Future ctsTraffic addition | Add after implementation and validation. |

The three implemented TCP models are distinct choices, not aliases. Future
models are planned coverage, not currently runnable cases. Decide whether
each supported model gets the full connection-count/duration matrix or a
staged representative subset. Phase one starts with IOCP; expand to RIO and
ReadWriteFile in later stages using comparable workloads and acceptance
criteria.

## Goal 2 — Stress resilience and recovery

**Customer promise:** after a declared stress or failure scenario, the system
does not bugcheck and returns to a healthy state capable of completing a
simple verification workload.

Run each stress scenario with its declared Goal 2
[verifier profile](verifier-profiles.md); the exact settings remain TBD. Stress workloads may use
`-Verify:connection` (declared per scenario); the post-stress verification
workload must use `-Verify:data`. A verifier finding or bugcheck is a failure even if the fault was
injected. For each test, specify expected connection-level effects separately
from system-level acceptance criteria. At the end, disable/restore verifier
configuration as prescribed, reboot if required, verify health, and run a
known-good ctsTraffic check before declaring recovery.

Choose ctsTraffic CSV logging according to the measurement goal. Collect
per-connection CSV results (`-ConnectionFilename`) for connection-success,
recovery, or per-connection acceptance measurements. In scenarios that
intentionally cause many expected failures, such as simulated resource
failures or randomized resets, omit noisy per-connection CSVs when they do not
inform the acceptance criteria; retain aggregate ctsTraffic results,
verifier/crash evidence, and system health data.

| Scenario family | Fault under test | Expected result and checks |
| --- | --- | --- |
| Resource pressure | The `AV-DV-LOWRES` [verifier profile](verifier-profiles.md), which simulates constrained or failed allocations/resources. | No bugcheck or unexpected verifier finding; expected operation failures are classified; the guest recovers and passes the post-stress check. Exact verifier settings and expected failures are TBD. |
| Network infrastructure: link loss | From the Hyper-V host, disconnect a test vNIC from its Private switch (`Disconnect-VMNetworkAdapter`), hold for the declared outage, then reconnect it (`Connect-VMNetworkAdapter`). The guest sees media disconnect; the adapter and its addresses remain. | Short outages may leave connections intact and longer ones time out; each scenario declares the outage duration and expected connection outcome. Unaffected links must keep working. Host-side control is unaffected by guest load. Hyper-V only. |
| Network infrastructure: adapter lifecycle | Inside the guest, through PowerShell Direct, disable and re-enable a test adapter (`Disable-NetAdapter` / `Enable-NetAdapter`) with a declared down time, or bounce it with `Reset-NetAdapter`. The interface and its addresses are removed and recreated. | Connections on that adapter fail immediately and are expected failures; replacements must succeed after re-enable. Exercises interface teardown racing in-flight IO. Commands use timeouts; a disable that hangs is a finding. Must never target the management adapter. Also works on bare metal. |
| TCP protocol/connection | Reset connections at randomized points in their lifecycle using `-Verify:connection`, randomized `-Transfer` ranges, and `-Shutdown:random`, as in the existing `TestScripts/ctsTraffic_io_reliability.cmd`. Note: `-Shutdown:random` alone only chooses graceful close vs. RST after the transfer completes. | Expected connection failures are allowed and counted separately; no system crash/verifier violation; system passes post-stress check. Confirm the exact supported injection semantics before treating this as a run contract. |

## Goal 3 — Network configuration control-path reliability

**Customer promise:** network configuration operations at scale can be
exercised while traffic uses the affected test interfaces and addresses, with
operation outcomes, traffic effects, system health, and the resulting
configuration all accounted for.

This is a distinct control-path objective, not simply payload or IO-path
coverage. Create large numbers of IP addresses, routes, and neighbor entries,
then randomly update, modify, and remove entries while ctsTraffic sends traffic
concurrently through the affected test interfaces and addresses. Apply the
initial TCP, address-family, duplex, data-verification, and IO-model choices
above. Record the verifier state and settings for each campaign.

Test both relationships between churn and traffic: (1) mutate objects actively
used by the traffic path, and (2) mutate unrelated objects on a test link while
traffic continues using its own unaffected address/route/neighbor state. A
change that leaves the active path usable must not cause a connection failure.
A brief no-progress stall may be acceptable, but stalls should be rare and an
allowed duration/number must be defined per campaign. If an object is deleted
or modified so that it breaks the path, resulting connection failures are
expected; the path and traffic must recover according to the scenario's
declared contract.

For finite ctsTraffic workloads, it immediately initiates replacement
connections while the configured total (`connections * iterations`) has not
been met. Record every failure and retry; replacement attempts must not hide
failures during path-preserving changes. Distinguish the configured test-
connection target from successful completions. For an intentional path break,
verify that attempts after the configuration is restored recover as required
and that the scenario-defined successful-completion criteria are met. Specify
per scenario whether existing connections must recover or new replacement
connections are sufficient.

Exercise both in-place updates where the object supports them and
remove-and-recreate sequences. Define the mutation semantics per object type;
do not assume that every object supports an in-place modify operation.

| Scenario family | Control-path activity | Traffic relationship and expected impact |
| --- | --- | --- |
| IP addresses | Add many addresses; update in place where supported; remove and recreate entries; randomly select entries for changes. | Test traffic using the changed addresses and traffic using unaffected addresses on the same link. If an address stays usable, traffic must not be interrupted; removal or a breaking change requires declared recovery. |
| Routes | Add many routes; update in place where supported; remove and recreate entries; randomly select entries for changes. | Test traffic using the changed routes and traffic using unaffected routes on the same link. A still-usable route must not interrupt traffic; deletion or a breaking change requires declared recovery. |
| Neighbor entries | Add many entries; update in place where supported; remove and recreate entries; randomly select entries for changes. | Test traffic relying on the changed neighbor state and traffic relying on unaffected entries. A still-usable entry must not interrupt traffic; deletion or a breaking change requires declared recovery. |
| Combined configuration | Exercise address, route, and neighbor additions and randomized updates/removals together. | Correlate each operation to affected and unaffected traffic; apply the same no-interruption and recovery criteria per operation. |

For each scenario, define and record these separate acceptance dimensions:

- **Operations:** attempted, successful, failed, and incomplete operations,
  including error details and declared versus unexpected failures.
- **Active traffic:** connection, transfer, data-verification, and completion
  outcomes correlated with configuration changes. For changes that preserve a
  usable path, any connection failure is an interruption and is not allowed.
  Brief no-progress stalls may be allowed only within a campaign-defined
  limit. For deliberate path breaks, permit the declared connection failures,
  record replacement attempts, and require recovery within the
  scenario-defined contract.
- **System and verifier health:** system and process health, bugchecks, and
  verifier diagnostics, with verifier configuration recorded and unexpected
  findings identified.
- **Convergence and final state:** time and observations needed to reach the
  declared stable state, and a comparison of actual final configuration with
  the intended state, including missing, extra, or incorrect entries.
- **Connection CSV policy:** collect per-connection CSV results when success
  rate or individual outcomes are measured; omit them for expected-failure
  stress cases when they add noise, and record the reason.

Scale, operation rates, mutation semantics, exact API or tool, acceptable
disruption and convergence tolerances, cleanup behavior, and all thresholds are
TBD. Define these and the expected operation and traffic effects for each
campaign before judging its results; retain the random seed or equivalent
reproducibility inputs when available.

Recovery criteria must specify whether existing connections recover or
replacement connections are sufficient, how the affected path's usability is
verified, and the required successful completions separately from the
`connections * iterations` test-connection target. Stall limits and exact
criteria remain TBD.

## Scenario definition and result

Record each concrete scenario with the [scenario template](scenario-template.md).
The run-time inputs it must resolve are listed in
[run lifecycle](run-lifecycle.md#run-inputs), and the required evidence in its
[minimum result bundle](run-lifecycle.md#minimum-result-bundle).

The initial acceptance matrix must keep these dimensions explicit:

| Goal / variant | Verifiers | Fault injection | Primary acceptance |
| --- | --- | --- | --- |
| 1A correctness | `AV-DV-CORRECTNESS` | None | `-Verify:data`; correct completion; no verifier finding, bugcheck, connection, data, or protocol failure |
| 1B baseline | `NONE` | None | `-Verify:data`; fixed-duration (`-TimeLimit`) work and error criteria plus the [leak detection](leak-detection.md) user-mode and kernel-pool criteria |
| 2 stress | `AV-DV-LOWRES` or `AV-DV-STRESS` | Resource, link-loss, adapter-lifecycle, or protocol failure | `-Verify:connection` allowed during stress (declared per scenario); `-Verify:data` for the post-stress check; no bugcheck/unexpected verifier finding; expected fault behavior; successful recovery check |
| 3 control path | TBD; `AV-DV-CORRECTNESS` is the candidate | Configuration changes; semantics TBD | Operation outcomes, declared versus unexpected traffic effects, system/verifier health, convergence, and final configuration state against campaign criteria |
