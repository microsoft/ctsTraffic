# Goals and Scenario Catalog

Status: **Draft**. This catalog defines what the runs are intended to prove.
Concrete ctsTraffic command lines, parameter values, thresholds, and
automation are to be added after the open decisions in
[the plan index](README.md) are answered.

The initial VM scenarios are TCP-only on three Hyper-V guests, with a maximum
of five VMs. Select and record the Windows build and architecture for each
campaign. IPv4-only and
IPv6-only configurations are both required. Dual-stack runs are blocked until
ctsTraffic implements the necessary support. Start with the duplex IO pattern.
Vary connection count and transfer duration independently and in combinations,
calibrating load per system and campaign. Every TCP reliability scenario,
including stress and recovery scenarios, must explicitly use `-Verify:data`.

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

- Enable the approved Application Verifier and Driver Verifier settings.
- Run the declared ctsTraffic workload to exercise connection establishment,
  data transfer, protocol completion, and teardown.
- Explicitly enable ctsTraffic data verification (`-Verify:data`) so received
  data is checked for corruption. Classify intentionally incomplete transfers
  from fault scenarios separately from data-integrity failures.
- Capture verifier diagnostics, bugchecks, process exit status, ctsTraffic
  connection/protocol/data errors, and system event/crash evidence.
- Judge this variant on correctness and diagnostics, not throughput, elapsed
  work rate, or memory stability: verifier overhead changes those observations.
- Fail on an unexpected verifier finding, bugcheck, data/protocol corruption,
  unexplained connection failure, or incomplete run. If a failure is an
  intentional scenario input, define and label it in that scenario.

### 1B. Uninstrumented work and memory baseline

- Disable Application Verifier and Driver Verifier.
- Run the same declared workload with `-Verify:data` for a fixed duration and
  capture work over time (for example completed connections and transferred
  bytes/frames), as well as ctsTraffic errors and process exit status.
- Capture user-mode process memory and kernel pool observations before and
  after the run, with a defined observation window and attribution method.
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

Application Verifier and Driver Verifier configuration, workload parameters,
fixed duration, repeat count, comparison method, tolerances, and leak
thresholds are open decisions. Process memory and kernel pool use are distinct
measurements and must have separate baselines and criteria.

### Workload sizing

Do not use one global connection count as the definition of load. Calibrate
connection count and transfer duration for each system and campaign, against
the capacity and configuration of the specific system under test. The scenario
matrix should include high connection counts with short transfers, low
connection counts with long transfers, and selected combinations, then expand
the combinations needed to expose regressions.

Before a run, characterize capacity and declare its intended load level,
target workload, upper resource/safety bounds, and stop conditions. Use
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

Run each stress scenario with Application Verifier and Driver Verifier enabled;
the exact approved settings remain TBD. Explicitly use `-Verify:data` for every
TCP scenario. A verifier finding or bugcheck is a failure even if the fault was
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
| Resource pressure | Application Verifier and/or Driver Verifier options that simulate constrained or failed allocations/resources. | No bugcheck or unexpected verifier finding; expected operation failures are classified; the guest recovers and passes the post-stress check. Exact verifier settings and expected failures are TBD. |
| Network infrastructure | Break a selected VM-to-VM path, then restore it, while ctsTraffic is active. | Observe and record loss/reconnect behavior; verify unaffected paths as applicable; confirm healthy post-fault operation. Fault method, timing, and recovery contract are TBD. |
| TCP protocol/connection | Configure ctsTraffic to close/reset connections at randomized points in their lifecycle (the existing `TestScripts/ctsTraffic_io_reliability.cmd` uses `-Shutdown:random`). | Expected connection failures are allowed and counted separately; no system crash/verifier violation; system passes post-stress check. Confirm the exact supported injection semantics before treating this as a run contract. |

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

Every concrete scenario should record:

- System class selected by the user: `desktop` or `server`.
- Goal and scenario identifier; pass/fail criteria and expected failure classes.
- Topology and VM roles; OS/build, driver, ctsTraffic build, and tool versions.
- Protocol, IO pattern, transfer/connection settings, duration, repeat count,
  and random seed or other reproducibility input when available.
- Verifier state and exact settings, including when a reboot is required.
- Fault type, target, trigger, start/stop times, and recovery action, if any.
- For control-path scenarios, configuration targets and intended final state,
  mutation sequence and operation outcomes, concurrent traffic effects, and
  convergence and final-state observations.
- Connection-result CSVs when connection success/recovery is measured; for
  intentional-failure stress scenarios, record when per-connection CSVs are
  omitted as uninformative.
- Start/stop/cleanup outcomes, observed work and health metrics, artifacts,
  deviations, and incident references.

The initial acceptance matrix must keep these dimensions explicit:

| Goal / variant | Verifiers | Fault injection | Primary acceptance |
| --- | --- | --- | --- |
| 1A correctness | Enabled, settings TBD | None unless specified by its own scenario | `-Verify:data`; correct completion; no unexpected verifier, bugcheck, data, or protocol failure |
| 1B baseline | Disabled | None | `-Verify:data`; fixed-duration work and error criteria plus separate user-mode and kernel-pool leak criteria |
| 2 stress | Enabled, settings TBD | Resource, infrastructure, or protocol failure | `-Verify:data`; no bugcheck/unexpected verifier finding; expected fault behavior; successful recovery check |
| 3 control path | State and settings TBD | Configuration changes; semantics TBD | Operation outcomes, declared versus unexpected traffic effects, system/verifier health, convergence, and final configuration state against campaign criteria |
