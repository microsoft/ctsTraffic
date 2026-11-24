# Windows Networking Reliability Test Plan

Status: **Draft**. This document set defines the plan and run-record contract;
it is not yet an executable VM orchestration runbook.

## Purpose

Define repeatable, evidence-backed reliability runs for the Windows networking
stack using ctsTraffic. Each run must identify its topology, workload, verifier
configuration, duration, fault model, acceptance criteria, and collected
artifacts so that results can be reproduced and compared.

Current scope, in brief (each item is defined in the linked document):

- **Topology:** two Hyper-V VMs on one Private switch first, then three, up to
  five; see [topologies](topologies.md).
- **Workload:** TCP only (UDP deferred), IOCP duplex first, IPv4-only and
  IPv6-only runs, ctsTraffic verification mode per goal; see
  [goals and scenarios](goals-and-scenarios.md).
- **Control and firewall:** PowerShell Direct and WinRM are both evaluated;
  guest firewalls stay enabled with a scoped rule group; see
  [remote control and firewall](remote-control-and-firewall.md).
- **Out of initial scope:** custom drivers and bare-metal runs.

## Test promises

| Goal | Customer promise | Evidence |
| --- | --- | --- |
| 1 — Repeatable workload reliability | For a declared IO pattern and configuration, socket applications complete the expected work reliably and consistently. | Instrumented correctness runs plus separate uninstrumented, fixed-duration work and memory observations. |
| 2 — Stress recovery | After resource pressure or connectivity/protocol faults, systems do not bugcheck and return to a healthy, usable state. | Stress-run evidence, host/guest health, and a post-stress verification workload. |
| 3 — Control-path configuration reliability | IP-address, route, and neighbor-entry changes on test links do not fail connections when the path remains usable; if a change breaks connectivity, retries and recovery meet the scenario contract. | Before/after configuration state, operation results, connection failures/retries, traffic stalls, recovery, system/verifier health, and post-churn verification. |

## Documents

Each topic has one authoritative document. Other documents summarize and link
to it; if they disagree, the authoritative document wins and the summary must
be corrected.

| Topic | Authoritative document |
| --- | --- |
| Goals, promises, scenario families, IO models, address families, verification mode, CSV policy, fault types, acceptance matrix | [Goals and scenario catalog](goals-and-scenarios.md) |
| Run inputs, phase steps, and result bundle for any run | [Run lifecycle](run-lifecycle.md) |
| Build order, gates, phase measurement IDs (SET/EXE/CLN) | [End-to-end readiness plan](end-to-end-readiness-plan.md) |
| Network layout, addressing, churn scope | [Initial topology](topologies.md) |
| Leak detection method | [Leak detection workflow](leak-detection.md) |
| Crash dumps, hang detection, debugger, failure buckets | [Crash capture and triage](crash-capture-and-triage.md) |
| Application Verifier and Driver Verifier settings | [Verifier profiles](verifier-profiles.md) |
| Debugger and Application Verifier package: host cache and guest install | [Debugging tools](debugging-tools.md) |
| Host-to-guest control channels, unattended run rules, guest firewall | [Remote control and firewall](remote-control-and-firewall.md) |
| Values chosen for the lab: host paths, guest image, VM shape, management path, credential source | [Lab configuration](lab-configuration.md) |
| Scenario and device record formats | [Scenario template](scenario-template.md), [device profile template](device-profile-template.md) |
| First concrete scenario | [G1B-CLEAN-BASELINE](scenarios/goal1b-clean-baseline.md) |
| Cross-cutting open decisions | This README ([open decisions](#open-decisions)); topic-specific open items stay in their own documents |

## Planning guardrails

- A run definition is a versioned input; do not compare runs with different
  workload, topology, verifier, IP family, IO model, or OS/driver configuration
  as if they were equivalent.
- Preserve separate results for verifier-enabled diagnostics and
  verifier-disabled work-over-time measurements.
- Record injected faults and expected outcomes explicitly. Do not treat
  expected connection teardown as a system-health failure, or suppress
  unexplained errors as expected.
- Preserve raw logs and measurements alongside summaries. A summary must link
  to the run configuration and collected artifacts.
- Treat bugchecks, verifier findings, incomplete cleanup, and a failed
  post-stress health check as incidents requiring triage, not successful runs.
- Do not enable verifiers or inject faults on a shared or production system;
  run only on explicitly assigned, recoverable test VMs.
- Treat the Hyper-V host as part of the test environment. Record its build
  (including Insider/pre-release builds), architecture, and load; a host
  failure or host-side noise is a lab issue, not a guest product failure.
  Do not run verifier or stress campaigns on a host that is also someone's
  primary workstation without accepting that risk explicitly.
- Every test VM always writes a complete memory dump on bugcheck; see
  [crash capture and triage](crash-capture-and-triage.md).

## Open decisions

Before turning this plan into an executable runbook, confirm:

1. Remote control channel: result of the PowerShell Direct versus WinRM
   evaluation in [remote control and firewall](remote-control-and-firewall.md).
2. Per-system load-sizing signals and safe resource envelope for later
   configurations, and the approved work-rate tolerance and leak-analysis
   thresholds defined in [leak detection](leak-detection.md) (proposed from
   pilot series, then approved).
3. Exact settings for each [verifier profile](verifier-profiles.md), reboot
   policy, fault-injection boundaries, and recovery/reset policy for Goal 2.
4. IO model coverage/priority and the post-stress verification workload.
   UDP is deferred until the TCP workflow works end to end.
5. Control-path operation counts/rates, per-scenario requirements for existing
   versus replacement connections after recovery, and the allowed rare-stall
   threshold.
6. Whether a campaign opts in to an attached KDNET debugger.

Resolved for the first lab and recorded in
[lab configuration](lab-configuration.md) or the owning topic document:
test addressing ([topologies](topologies.md#address-and-adapter-inventory)),
WinRM authentication and credential source, pool-tag tool
([leak detection](leak-detection.md#scope-and-preconditions)), hang timeout
([crash capture](crash-capture-and-triage.md#hang-detection)), and guest
image and build.
