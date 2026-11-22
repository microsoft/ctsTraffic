# Windows Networking Reliability Test Plan

Status: **Draft**. This document set defines the plan and run-record contract;
it is not yet an executable VM orchestration runbook.

## Purpose

Define repeatable, evidence-backed reliability runs for the Windows networking
stack using ctsTraffic. Each run must identify its topology, workload, verifier
configuration, duration, fault model, acceptance criteria, and collected
artifacts so that results can be reproduced and compared.

The initial target is three Windows VMs on Hyper-V, with each VM-to-VM link
carried directly over its own isolated Hyper-V Private virtual switch. Guest
configuration, remote control, and artifact collection will use PowerShell
Remoting/WinRM. Initial scenarios cover TCP; the Windows version/build and
architecture are selected when each run is scheduled. There are no custom
drivers in the initial VM scope. Bare-metal runs, including their driver
installation references, are a later extension.

The initial scenarios start with three VMs; the topology/runbook should support
up to five. WinRM authentication, identity/privilege, and guest firewall policy
are not yet specified.

The initial address-family matrix requires IPv4-only and IPv6-only runs.
Dual-stack socket coverage is also required, but ctsTraffic needs dual-stack
functionality before those runs can be scheduled. Start with the duplex IO
pattern, then vary connection count and transfer duration, including many
short connections, fewer long transfers, and combinations. Set load levels
per system under test: avoid both inadequate stress and exceeding an approved
resource/safety envelope. Every TCP reliability scenario must explicitly use
ctsTraffic `-Verify:data`.

Calibrate the load profile per system before each campaign. Start IO-model
coverage with IOCP, then expand to the other currently implemented models.

## Test promises

| Goal | Customer promise | Evidence |
| --- | --- | --- |
| 1 — Repeatable workload reliability | For a declared IO pattern and configuration, socket applications complete the expected work reliably and consistently. | Instrumented correctness runs plus separate uninstrumented, fixed-duration work and memory observations. |
| 2 — Stress recovery | After resource pressure or connectivity/protocol faults, systems do not bugcheck and return to a healthy, usable state. | Stress-run evidence, host/guest health, and a post-stress verification workload. |
| 3 — Control-path configuration reliability | IP-address, route, and neighbor-entry changes on test links do not fail connections when the path remains usable; if a change breaks connectivity, retries and recovery meet the scenario contract. | Before/after configuration state, operation results, connection failures/retries, traffic stalls, recovery, system/verifier health, and post-churn verification. |

The IO/data path and the control/configuration path are separate scenario
families. Control-path churn scenarios cover both changing objects used by
active traffic and changing unrelated entries while traffic continues over
the test link. A change that leaves the path usable must not interrupt
connections; brief no-progress stalls may be acceptable but should be rare and
have a campaign-defined limit. A change that deletes or makes a required
object unusable may cause expected connection failures. ctsTraffic immediately
attempts replacement connections until its configured `connections * iterations`
target is met; record those failures/retries and
verify the scenario's recovery contract.
Verify configuration state independently of payload correctness.

Collect ctsTraffic per-connection CSVs with `-ConnectionFilename` when
individual connection outcomes or success rates are acceptance metrics (for
example, Goal 1 and control-path recovery). Omit per-connection CSVs from
intentional-failure stress scenarios when expected errors make them noisy and
uninformative; retain the aggregate status and system-health evidence those
scenarios require.
Verifier-enabled runs prioritize bug detection and correctness evidence; they
are not used as the throughput or work-over-time baseline. Uninstrumented runs
provide that baseline and the memory observations. Stress and churn tests may
intentionally produce connection errors, so each scenario must distinguish
expected effects from unexpected failures.

## Documents

- [Goals and scenario catalog](goals-and-scenarios.md) — IO-path and
  control-path promises, run variants, scenario matrix, and acceptance
  criteria to finalize.
- [Scenario template](scenario-template.md) — simple per-campaign definition
  with required `desktop` or `server` system class and measurements.
- [Device profile template](device-profile-template.md) — reusable per-device
  inventory and network configuration captured by a local script.
- [First uninstrumented baseline scenario](scenarios/goal1b-clean-baseline.md) —
  initial clean TCP workload with connection and memory/pool measurements.
- [Run lifecycle](run-lifecycle.md) — setup, execution, cooldown, evidence,
  and run completion gates.
- [Initial topology](topologies.md) — proposed all-on-link, pairwise-private-
  switch VM network.

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

## Open decisions

Before turning this plan into an executable runbook, confirm:

1. Procedure for selecting static routable test addresses in
   [topologies.md](topologies.md).
2. WinRM identity, privilege, authentication, and guest firewall policy.
3. Per-system load-sizing signals and safe resource envelope, run durations,
   repetitions, acceptable work-rate variance, and memory leak thresholds.
4. Approved verifier settings, reboot policy, fault-injection boundaries, and
   recovery/reset policy for Goal 2.
5. IO model coverage/priority and the post-stress verification workload;
   decide later when UDP enters scope.
6. Control-path operation counts/rates, per-scenario requirements for existing
   versus replacement connections after recovery, and the allowed rare-stall
   threshold.
