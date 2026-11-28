# G1B-CLEAN-BASELINE — Uninstrumented TCP Work and Memory Baseline

Status: **Workload plan of record; execution remains gated**. This is the
first concrete Goal 1B scenario. The workload settings below apply to the
pilot and baseline unless noted otherwise. The scenario and result contracts
remain draft; it is not a repeatability or leak pass until workload and memory
thresholds are approved.

## Run definition

- **Goal:** 1B — repeatable workload and uninstrumented memory baseline.
- **System class:** `desktop` for the initial dry run; capture a device profile
  for every participating device.
- **Topology:** two-VM Hyper-V topology (one pairwise Private switch) for the
  first vertical slice, then the three-VM mesh; finalize VM roles and address
  mapping in the run record.
- **VM/vSwitch configuration:** record expected VM IDs, generation/config
  version, vCPU and memory settings, VHDX/checkpoint references, vNIC IDs/MACs,
  switch type and IDs, vNIC-to-switch attachments, VLAN/QoS settings,
  extensions, and physical-adapter bindings. For each Private switch, verify
  that no physical adapter is bound. Compare live configuration before setup,
  after setup, and after cleanup.
- **Guest image:** Windows 11 RTM planned; record the actual edition, version,
  build, and architecture after installation. VHDX path is a campaign input.
- **Provisioning and cleanup:** the workflow creates the dedicated VMs
  and test switches from the supplied VHDX. Retain them for reuse after
  cooldown; stop tools and verify health/configuration without automatically
  reverting, reimaging, or deleting resources.
- **Protocol / IO model / pattern:** TCP / IOCP / duplex.
- **Address family:** one family per run (IPv4-only or IPv6-only); record the
  selected family. Do not combine the families in one run.
- **Data verification:** explicitly pass ctsTraffic `-Verify:data`.
- **Duration:** each client iteration uses ctsTraffic `-TimeLimit`. The
  `Time-limit of N reached` notice is the expected end of an iteration.
  Connections in flight at the cutoff are not completed work. The pilot must
  confirm how ctsTraffic counts them and how to identify cutoff-related errors;
  the time-limit notice alone does not establish that an error was caused by
  the cutoff.
- **Iterations:** one discarded warm-up plus five measured iterations on the
  same boot, with ctsTraffic servers kept running across all of them, as
  defined in [leak detection](../leak-detection.md).
- **Application Verifier / Driver Verifier:** profile `NONE` (both disabled).
  Capture the resulting configuration before execution and verify it is still
  disabled after the run.
- **Crash capture:** complete memory dumps, injected-NMI dumps, and WER local
  dumps configured per [crash capture and triage](../crash-capture-and-triage.md).
- **Fault injection:** none. Do not disconnect links, alter network
  configuration, or inject protocol/resource failures during this scenario.
- **Duration, connection count, transfer settings, repetitions:** set by
  [configuration v1](#configuration-v1) below. A later version may come from
  system characterization. Intended load, resource bounds, and stop
  conditions are recorded before execution; selecting a configuration alone
  does not establish that it is safe on this system.
- **ctsTraffic logging:** enable `-ConnectionFilename` and `-StatusFilename`
  (optionally `-TcpInfoFilename`); preserve the per-connection CSV, status
  time series, exact per-process command line and argument vector, and original
  stdout and stderr for every iteration. The final historic connection
  statistics, TCP byte totals, and elapsed time are written to stdout, not to
  a CSV connection log. If the final summary is absent, record it as missing;
  retain partial output and the exit code for classification. The read-only
  `Get-CtsTrafficTcpSummary` module function can inspect an already-collected
  stdout file; it does not collect output or determine the run outcome.
- **Pool-tag evidence:** an approved, working pool-tag snapshot tool is
  mandatory before any workload, including characterization pilots.

## Configuration v1

Lab-wide settings (host, guest image, VM shape, management path, credentials)
are in [lab configuration](../lab-configuration.md). This scenario adds:

| Setting | Value |
| --- | --- |
| Emphasis | Data path: few long-lived connections at maximum throughput |
| Topology | Two VMs on `Private-AB`; VM-A is the server, VM-B the client |
| Address family | IPv4-only first (`192.168.1.1` server, `192.168.1.2` client); IPv6-only is a separate later run |
| ctsTraffic binary | Release ARM64 built from this repository; record the commit and SHA-256 |
| Concurrent connections | 8 (ctsTraffic default) |
| Bytes per connection | 1 GiB / 1,073,741,824 bytes (ctsTraffic default) |
| Measured iteration length | 5 minutes (`-TimeLimit:300000`) |
| Pilot | 1,024 connection attempts, with a five-minute client time cap; see the separate pilot below |
| Iterations | 1 warm-up plus 5 measured, on one boot, persistent server |
| Server lifetime | Persistent for the baseline series; a safe time bound and stop procedure are unresolved. The pilot alone uses a seven-minute watchdog. |
| Counter sampling | Every 5 seconds |
| Status updates | `-StatusUpdate:5000`, matching the counter interval |

Server (VM-A), started once and kept running for the whole series:

```text
ctsTraffic.exe -Listen:192.168.1.1 -Pattern:duplex -IO:iocp -Transfer:1073741824 -Verify:data
  -ConnectionFilename:<dir>\server_conn.csv -StatusFilename:<dir>\server_status.csv -StatusUpdate:5000
```

Client (VM-B), once per iteration:

```text
ctsTraffic.exe -Target:192.168.1.1 -Bind:192.168.1.2 -Pattern:duplex -IO:iocp -Transfer:1073741824
  -Connections:8 -Verify:data -TimeLimit:300000
  -ConnectionFilename:<dir>\client_conn_<n>.csv -StatusFilename:<dir>\client_status_<n>.csv -StatusUpdate:5000
```

`<dir>` is the run's guest output folder and `<n>` the iteration number
(`0` for warm-up). Before using these G1B templates, confirm that their
options are accepted together and how `-TimeLimit` reports connections cut off
in flight.

Source review of the G1B templates identifies additional execution
constraints, not a completed pilot:

- The client omits `-Iterations` and `-RateLimit`, so it attempts connections
  indefinitely at an unlimited configured rate until the time limit. Its
  8 connections are a concurrency target, not a total-work limit.
- The server omits `-ServerExitLimit` and `-TimeLimit`, so it has no automatic
  exit bound. A verified, bounded stop and fallback are required before launch.
- In duplex mode, the 1 GiB transfer target is the total per connection,
  split between send and receive, not 1 GiB in each direction.
- The tool's advanced help describes `-TimeLimit` as an abort cap, whereas
  this scenario intentionally uses it to end each fixed-duration iteration.
  The resulting in-flight outcomes and their attribution remain unverified;
  neither the notice nor timing alone proves an error is expected.

These observations come from `ctsConfig.cpp`, `ctsSocketBroker.cpp`,
`ctsIOPattern.cpp`, and `ctsTraffic.cpp`; they do not prove the staged binary
behaves as intended under load. The pilot still requires a reviewed safety
envelope, monitoring, stop procedure, and explicit approval.

Pass/fail thresholds are not part of v1. The work-rate tolerance and leak
thresholds are proposed from 3 pilot leak series and approved by the lab owner
(see [leak detection](../leak-detection.md#analysis)). Until then, every run of
this configuration is pilot or characterization only.

Leak growth is normalized per GiB transferred as well as per 1,000 connections;
the number of completed connections per iteration remains to be measured.

## 1,024-connection duplex pilot

The operator selected a separate pilot targeting 1,024 connection attempts,
using duplex, explicit logging, and otherwise the tool's defaults. The
five-minute client and seven-minute server caps remain safety limits, so the
pilot may stop before reaching its target. Subject to verifying the test
addresses and resolving the unique guest output directory, the proposed
commands are:

```text
C:\ctsReliability\tools\ctsTraffic.exe -Listen:192.168.1.1 -Pattern:duplex -ServerExitLimit:1024 -TimeLimit:420000 -ConsoleVerbosity:0 -ConnectionFilename:<run-dir>\g1b-pilot-server-connection.csv -ErrorFilename:<run-dir>\g1b-pilot-server-error.log -StatusFilename:<run-dir>\g1b-pilot-server-status.csv
C:\ctsReliability\tools\ctsTraffic.exe -Target:192.168.1.1 -Pattern:duplex -Iterations:128 -TimeLimit:300000 -ConsoleVerbosity:0 -ConnectionFilename:<run-dir>\g1b-pilot-client-connection.csv -ErrorFilename:<run-dir>\g1b-pilot-client-error.log -StatusFilename:<run-dir>\g1b-pilot-client-status.csv
```

`<run-dir>` is a separately resolved, unique directory on each guest; these
are command templates, not shell-ready commands. `-StatusFilename` selects
the status CSV; `-StatusUpdate` takes an interval in milliseconds, not a
filename. It is omitted here, retaining the 5000 ms default.

Retain each run's logs and dumps in a dedicated directory under
`C:\ctsReliability`. The intended layout is
`C:\ctsReliability\runs\pilot-<run-id>` on the host and each guest, with
separate server and client evidence in the host's run directory. The run
directory must be newly reserved, not an existing directory adopted or
overwritten. The current guest WER configuration writes to the shared
`C:\ctsReliability\dumps` folder, and system dumps may use another fixed path.
Final cleanup collects WER dumps produced by the run into its run directory,
distinguishing them from pre-existing files and accounting for incomplete
writes. Inventory, guest-side copy, and verified host-transfer helpers exist,
but integrated cleanup and attribution are not implemented yet. Do not delete
or overwrite the original dumps during collection.

VM-A is the server and VM-B is the client. The client time limit begins after
its own launch; it does not stop the server. The server's seven-minute time
limit is an operator-selected watchdog and starts when the server starts, so
the controller must ensure enough server time remains for the client.
The server and client `-TimeLimit` options are abort caps, not guarantees of
graceful connection completion. `-ServerExitLimit` counts server connections,
not time; the server watchdog remains necessary if fewer than 1,024 arrive.
Defaults include TCP/IOCP, data verification, eight concurrent client
connections, a 1 GiB total transfer per connection, and no configured rate
limit. With eight concurrent connections, 128 client iterations target 1,024
attempts, not necessarily 1,024 successes. Completing all 1,024 transfers
would transfer 1 TiB in total per endpoint. The client
does not specify a bind address, so the chosen source interface must be
verified before use.

The operator selected observation thresholds of 100 MiB free memory and 10 GiB
free disk on each guest. The latter leaves a nominal margin over the 8 GiB
guest memory size for a complete dump. Guardians may terminate a running
ctsTraffic instance only on loss of required monitoring, such as a lost
heartbeat or observer failure. Connection failures, a nonzero peer exit, and
resource-threshold breaches must not themselves terminate a running instance.
Record those conditions as evidence; the configured ctsTraffic time limits
remain in effect. Resource exhaustion remains possible, and sampling cannot
prevent breaches between observations.
The operator approved `RemoteSigned` for each dedicated guardian process so
it can run its locally staged script; this does not change the persistent
guest execution policy.

Use the connection CSV and error log to inspect recorded outcomes, and the
status CSV and recorded process timestamps to assess progress and elapsed
time. Record attempts, successes, failures, and whether either time cap ended
the run; an incomplete or failed target is not a successful 1,024-connection
run. Keep original stdout, stderr, commands, exit codes, and the other required
observations: the CSV alone does not establish that all attempts were recorded.
Resolve the remaining evidence requirements and verify monitoring-loss
handling and coordinated startup before execution. The current preliminary
scenario check still blocks execution; no pilot runner or complete preflight exists. This
pilot is not an acceptance pass and is not represented by the G1B JSON
argument templates below.

## Structured scenario and result contracts

Status: **draft design; full validation and a runner do not exist yet**. The
[draft JSON scenario](../../../TestScripts/Reliability/scenarios/g1b-clean-baseline.v1.json)
records known v1 settings and unresolved values; it is not an executable run
definition. Use versioned JSON for execution inputs and results, consistent
with the lab definition. This Markdown remains the human-readable scenario. The JSON
scenario, the resolved run definition, and the result are distinct documents;
each has an explicit `schemaVersion`. A run references an immutable scenario
ID and version, and retains the exact resolved input and its SHA-256.

The scenario definition must describe, at minimum:

| Field | Required content |
| --- | --- |
| `scenarioId`, `scenarioVersion` | Stable identity and version; this scenario is `G1B-CLEAN-BASELINE`. |
| `scope` | Goal, system class, supported guest OS/architecture, topology, device roles, and required snapshot fields. Record actual device IDs, addresses, build, and snapshot ID in the resolved run definition. |
| `workload` | Server and client argument templates, executable identity, address family, connections, transfer bytes, time limits, warm-up and measured iterations. Resolve the v1 settings above and record the exact argument vector per process. Require TCP/IOCP duplex and `-Verify:data`. |
| `instrumentation` | Both verifiers disabled, no fault injection, counter interval, required monitors, noise-floor captures, quiescence conditions, and `S0` through `S5` capture points. |
| `measurements` | For every G1B criterion, its source, unit, interval or capture points, `artifactPath`, `missingDataBehavior`, and threshold or explicit calibration status. |
| `safety` | Approved resource bounds, stop conditions, monitor-loss behavior, bounded cleanup and recovery, and allowed operator actions. Values not yet approved remain unresolved and block execution, including pilots. |
| `artifacts` | Required and optional artifacts, their device/iteration scope, output paths, and completeness rules. Per-process command lines, stdout, stderr, final summaries, connection and status CSVs, counter series, pool snapshots, verifier evidence, and run configuration are required. |
| `outcomeRules` | Pass, pilot/characterization, inconclusive, and failure rules, including how observed failures and missing evidence take precedence. |

The resolved run definition supplies the run ID, owner, mode, snapshot
reference, approved topology and participant IDs, selected channel per
operation, output roots, and approved system-specific bounds and thresholds.
It must not contain credentials. Paths and argument values are data, not shell
commands; the eventual runner must construct an argument vector without
evaluating input as PowerShell code.

Before any setup, validation must reject unsupported versions, missing required
fields, unsupported fields, unresolved commands, sources, units or safety bounds,
and incompatible topology, verifier or fault settings. Acceptance mode also
requires every pass/fail threshold. Characterization may leave only explicitly
identified thresholds under investigation unresolved; it cannot report an
acceptance pass. The run plan and explicit operator approval remain required
after validation.

The result contract must retain the resolved-input reference and hash, UTC
timestamps, participant and channel identities, planned and observed commands,
exit codes, setup/cleanup findings, measurements with units and capture points,
missing or non-quiescent evidence, and the outcome with its reasons. Its
artifact inventory records each required or optional file's device, path,
presence, size and SHA-256 when present. An observed failure is not converted
to a pilot or inconclusive result merely because other evidence is missing.

The module's preliminary `Test-CtsScenario` reports known blockers, including
inconsistent workload arguments, measurement coverage, and quiescence settings,
and always blocks execution; it does not validate the complete contract. The
draft JSON still lacks approved resource bounds, calibrated thresholds, and
required measurement details. Phase 4 is not complete and no workload is
authorized by this section.

## Measurement points

Follow the [leak detection workflow](../leak-detection.md):

1. **Noise floor:** with servers listening and no client traffic, take the
   idle snapshots. Start time-stamped performance sampling.
2. **Warm-up iteration:** run and retain, but exclude from the trend.
3. **Baseline `S0`:** after the quiescence gate, take a full snapshot (pool
   tags, pool and memory counters, system objects, persistent server process,
   TCP connection states).
4. **Measured iterations 1–5:** during each, collect ctsTraffic status,
   per-connection outcomes, and the client and server process time series;
   after each, apply the quiescence gate and take snapshot `S1` … `S5`.
5. **Cooldown:** stop servers cleanly, take a final post-cooldown snapshot,
   and verify configuration and health.

Record the sampling interval, any missing samples, and any snapshot taken
without meeting the quiescence gate.

## Measurements and acceptance

| Criterion ID | Measurement | Source / unit / capture points | Success criterion |
| --- | --- | --- | --- |
| G1B-00 | Environment/configuration conformance | Approved snapshot compared with live Hyper-V VM, vSwitch/switch-port/vNIC and guest OS/network configuration; before setup, after setup, and after cleanup | Actual configuration matches the reviewed scenario before execution; owned VMs/switches remain reusable with no unapproved drift after cleanup. Compare final state to the approved post-setup state; account explicitly for dynamic neighbor/cache changes. |
| G1B-01 | Connection attempts, successes, failures, retries, and success rate | ctsTraffic aggregate output and `-ConnectionFilename` CSV; counts and percent (successful completions / attempts); each measured iteration | Every failure is accounted for; zero unexpected connection failures in this no-fault run. Classify an error as a cutoff effect only with evidence supporting that attribution; unresolved errors are not automatically expected. Report retries separately; they do not erase failed attempts. |
| G1B-02 | Payload correctness | ctsTraffic `-Verify:data` result and error output; verified transfers and error count; entire execution | All completed transfers pass data verification; zero data-integrity errors. |
| G1B-03 | Completed work and work rate | ctsTraffic summary, `-StatusFilename` time series, and CSV; completed connections and transferred bytes per `-TimeLimit` iteration | Each measured iteration meets the resolved work target, and iteration-to-iteration variance is within the approved tolerance. Without an approved target and tolerance, execution is characterization only. |
| G1B-04 | User-mode memory | Persistent server process (private bytes, handles, threads) at `S0` … `S5`; client process time series per iteration | No metric flagged by the [leak detection](../leak-detection.md) analysis. Thresholds are TBD; do not claim a leak pass until they are approved. |
| G1B-05 | System memory and kernel pool totals | Pool paged/nonpaged bytes, committed and available memory, and system handle count at noise floor and `S0` … `S5`, plus time series | No metric flagged by the leak analysis; transient peaks recorded. Thresholds are TBD. |
| G1B-06 | Pool allocation detail | Approved pool-tag tool output at noise floor and `S0` … `S5`; per-tag allocation count and bytes | No tag flagged by the leak analysis. Missing tool capability blocks all workloads; a missing required snapshot makes the leak result inconclusive. |
| G1B-07 | Process/system health | ctsTraffic exit code, process monitor, heartbeat/hang probe, crash-dump and WER folders, Windows event evidence; setup through cleanup | Exit code is zero or fully explained by classified cutoff errors; no bugcheck, hang, unexpected reboot, unexplained process failure, or required-monitor loss. Crash capture was verified as configured before execution. On normal completion the exit code is network plus protocol errors, capped at `MAXINT`; early failures can return other codes. |
| G1B-08 | Verifier state | Captured Application Verifier and Driver Verifier configuration; enabled/disabled state; setup, execution, and cleanup | Both remain disabled for the full run; any unexpected enabled verifier invalidates the run. |

## Outcome rules

- **Pass:** only after calibrated duration/work targets and user-mode and
  kernel-pool thresholds are set, all required evidence is present, and every
  criterion above is met.
- **Pilot / inconclusive:** the clean workload completes but one or more
  numeric calibration or memory thresholds are still TBD, or required
  measurement evidence is missing, with no known scenario failure. An
  observed failure remains failed even if other evidence is incomplete.
- **Fail:** any unexpected connection failure, data-integrity error,
  incomplete target, unexplained nonzero exit code or process failure,
  bugcheck, hang, unexpected verifier activation, or a confirmed leak (a
  suspected leak requires the confirmation run first).
- Preserve raw ctsTraffic CSVs, status files, aggregate logs, time-series
  counter data, every leak-series snapshot, device profiles, verifier-state
  evidence, crash/WER dumps if any, and the run configuration. Assign each
  failure a [triage bucket](../crash-capture-and-triage.md). Do not summarize
  expected failures: this scenario injects none.

Use the [readiness plan](../end-to-end-readiness-plan.md) for phase tests,
measurement IDs, parameter-selection routes, and implementation gates.
