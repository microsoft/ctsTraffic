# End-to-End Reliability Run Readiness Plan

Status: **Implementation plan; execution remains gated**. Complete the
deliverables and validation below before attempting the first full end-to-end
scenario. This document does not authorize or perform a test run.

## Objective and user workflow

Provide two reusable user workflows:

1. **Capture a system snapshot.** Inventory the Hyper-V host and every
   participating test device, record the test topology and relevant current
   configuration, and produce a versioned snapshot that can be referenced by
   any scenario.
2. **Run a scenario.** Select a reviewed scenario definition, a system snapshot,
   and either an existing workload configuration or a system-specific
   configuration. Validate that the environment still matches, then setup,
   execute, monitor, cooldown, collect evidence, and report the outcome.

The scenario workflow must also offer an optional characterization mode that
helps users determine longhaul parameters for their system. Characterization
is not mandatory when an existing configuration is suitable, and does not
replace preflight or safety checks.

The system snapshot describes relatively stable device and network
configuration. Each scenario run separately records its volatile pre-run
baseline, execution measurements, and post-run measurements. Do not treat
machine class (`desktop` or `server`) as a load profile or infer connection
counts, duration, or safety limits from it.

The first end-to-end implementation is limited to
[G1B-CLEAN-BASELINE](scenarios/goal1b-clean-baseline.md): a clean,
uninstrumented TCP/IOCP duplex workload with `-Verify:data`, no injected
faults, connection CSVs, and memory/pool observations. Stress, verifier-enabled,
control-path mutation, additional IO models, and five-VM scale are later
extensions.

## Approved first-run policies

- **Provisioning:** create three dedicated VMs and their pairwise Private test
  switches from a user-supplied VHDX. Validate image compatibility and record
  the actual guest OS/build/architecture before running.
- **Reuse:** retain the VMs and switches after cleanup for later runs. Stop
  run-owned tools, drain activity, collect evidence, and verify configuration
  and health. Do not automatically delete, revert, or reimage these resources.
  Recovery-point restoration after a failure requires separate approval and
  must not precede evidence capture.
- **Management:** determine and document secure WinRM access during setup
  design, before any system changes. Authentication, privileges, guest
  bootstrap, management switch/addressing, and firewall rules must be reviewed
  as one deliverable; an unspecified management design blocks provisioning.
- **Workload selection:** support an existing versioned configuration or an
  optional system-specific characterization workflow. Both routes produce a
  resolved workload configuration and validate resource limits before use.
- **Pool evidence:** select and approve a pool-tag snapshot tool, validate its
  availability and output on the target architecture/build, and require it
  before any characterization pilot or scenario workload. Counters alone are
  not sufficient for this first scenario.

## Current starting point

The repository already has a lifecycle plan, topology plan, scenario template,
device-profile template, and first Goal 1B scenario draft. Two local PowerShell
scripts can capture host or device data into Markdown templates. These are
prototypes, not yet the two complete workflows described here: in particular,
the device script does not orchestrate a consistent multi-device snapshot,
capture the complete Hyper-V topology, or provide schema validation and drift
checks.

## Work plan

### Phase 1 — Close the first scenario contract

Turn the first scenario draft into a reviewed, runnable definition. Resolve and
record:

- Run owner, run ID, selected system class, guest roles, and the participating
  host and VM identities.
- User-supplied Windows 11 RTM VHDX path, guest edition/version/build/architecture,
  VM generation, vCPU/RAM allocation, and recovery-point policy. Create the
  dedicated lab initially; validate and reuse it on subsequent runs.
- The initial three-VM topology, private-switch and NIC mapping, management
  path isolation, static addresses, selected address family, and address
  restoration procedure. IPv4-only and IPv6-only remain separate runs.
- ctsTraffic build/hash, server/client role, exact supported command/config,
  IOCP duplex workload, transfer settings, connection/iteration target,
  duration, and repetitions. Confirm the semantics of CSV outcomes, retries,
  and successful completion before fixing acceptance calculations.
- Workload-configuration selection and an optional characterization procedure
  that sets target work, system-specific resource bounds, monitors, stop
  conditions, and duration without under-stressing or exceeding the approved
  envelope. Do not require fresh characterization for every preset-based run.
- Exact sources, units, sampling interval, and capture times for process
  memory, system memory, paged/nonpaged pool, pool-tag snapshots, connection
  outcomes, process status, and crash/reboot evidence.
- The threshold and comparison method for repeated work rate and persistent
  memory/pool growth, supplied by a reviewed configuration or established
  through optional characterization. Without approved thresholds, a workload
  can only be characterization, not a repeatability or leak pass.
- The full no-verifier state to check, failure handling, clean stop procedure,
  quiescence condition, artifacts, and outcome rules.

**Exit gate:** there are no unresolved `TBD` values that affect safe execution,
measurement interpretation, or pass/fail. Any intentionally exploratory
parameter is explicitly marked as pilot-only and cannot produce a pass.

### Phase 1A — Select parameters or characterize the system

Implement both routes without requiring users to perform characterization
when selecting a suitable existing configuration:

1. **Existing configuration:** inventory candidate configurations, assign each
   an ID/version, and document its scenario, topology, IO model, address family,
   resource assumptions, workload, measurement policy, and acceptance limits.
   Existing test scripts are candidates to review, not automatically approved
   clean-run presets. In particular, the randomized-failure reliability script
   must not be used unchanged for the no-fault G1B scenario.
2. **System characterization:** after the lab is provisioned and all setup
   gates pass, use bounded short workloads to measure capacity and derive
   candidate connection counts, transfer settings, longhaul duration,
   repetitions, warm-up/cooldown policy, resource limits, and comparison
   thresholds. Define initial safety bounds and stop conditions before the
   first workload, not from its results.
3. **Common output:** resolve either route into a versioned parameter record
   linked to the snapshot and scenario. Record whether values came from a
   preset, operator input, or characterization evidence. Review proposed
   system-specific values before saving them for later scenario runs.
4. **Common validation:** check compatibility, capacity, and configuration
   drift for every run. A preset is not permission to exceed device limits;
   reject incompatible presets rather than silently changing their load.
   Require both verifiers disabled and the approved pool-tag tool for G1B
   characterization as well as acceptance runs.

Characterization outputs describe observed workload and memory behavior;
they are not themselves a longhaul acceptance pass. Do not derive tolerances
solely to make an observed regression pass. Retain the derivation, reference
runs, and approval for each proposed threshold.

**Exit gate:** users can choose either route, inspect every resolved value,
and reuse the resulting configuration. Missing acceptance thresholds permit
only explicitly labeled characterization, never an acceptance pass.

### Phase 2 — Define the system snapshot contract

Define a versioned, machine-readable snapshot schema and a readable report.
Every value must carry its source and capture time; unavailable values must be
marked unavailable with a reason rather than silently omitted or defaulted.

The first schema should cover:

- **Identity:** snapshot ID/schema version, UTC capture interval, device ID,
  role (host, DUT, peer, controller), system class, and OS identity.
- **Compute and storage:** OS/build/architecture, processor model/architecture,
  physical/logical counts and reported cache, total/available RAM, fixed
  storage size/free space, and device model where reported.
- **Device networking:** adapter identity/index, link state/speed, MAC address,
  addresses/prefixes, gateways, DNS configuration, routes, and neighbor state.
- **Hyper-V:** host identity/version, VM IDs/names/state, guest-to-vNIC and
  vNIC-to-switch mappings, switch type and configuration, virtual hardware
  allocation, VHDX location and attachment details, checkpoint/recovery-point
  identity, and test-versus-management links. Record VM generation/config
  version, processor count, static/dynamic memory settings, firmware and
  Secure Boot settings, integration services, vNIC MAC/VLAN/QoS settings,
  switch extensions, host-vNIC settings, and physical-adapter bindings where
  applicable. For Private switches, record that no physical adapter is bound.
- **Test prerequisites:** ctsTraffic and support-tool versions/hashes, relevant
  driver versions, verifier state, reboot/health indicators, time
  synchronization, and remote-management reachability.

Never capture credentials, access tokens, or other secrets. Document treatment
of identifying values such as hostnames and MAC addresses before sharing
snapshots outside the test team.

The capture workflow must produce a machine-readable authoritative record, a
human-readable report, and a manifest identifying files and hashes. Support
per-device capture locally and a coordinated host-plus-guest capture through
the approved remote-management mechanism. Record partial failures explicitly;
a partial snapshot must not be accepted as complete. Provide a comparison
report so users can see configuration drift since capture.

**Exit gate:** schema validation rejects missing required fields, malformed
values, duplicate device IDs, and inconsistent topology references; a sample
multi-device snapshot round-trips through machine and human-readable formats.

### Phase 3 — Implement and validate the snapshot workflow

1. Finalize snapshot command inputs, output directory/layout, device role
   identifiers, and privilege requirements.
2. Implement local device collection using the existing capture script as a
   starting point; add missing configuration such as routes, neighbors,
   verifier state, and tool/driver versions.
3. Implement host inventory of Hyper-V VMs and their effective configuration,
   virtual switches and their effective configuration, switch ports, VM
   vNIC-to-switch attachment, host vNICs, VHDX/recovery-point references, and
   physical-adapter bindings. Capture both the intended configuration and the
   actual configuration; do not infer a VM's network path from its name alone.
4. Add coordinated remote collection for every declared guest. Use the
   approved WinRM identity, authentication, firewall, and least-privilege
   policy; do not put credentials in command lines or artifacts.
5. Normalize and validate results against the snapshot schema. Preserve
   collection errors, source commands, timestamps, and per-device success
   status.
6. Generate the immutable snapshot package and drift comparison. Keep
   generated machine-specific artifacts outside the source tree by default.
7. Test complete, partial, unavailable, malformed, and changed-device cases.
   Verify the workflow is read-only and does not change host or guest state.

**Exit gate:** a user can capture a complete host-and-guest snapshot, identify
missing/inaccessible devices, review the report, and reproduce the same
machine-readable schema without editing generated files manually.

### Phase 3A — Provision the lab without assuming guests already exist

Resolve the initial provisioning/snapshot dependency explicitly:

1. Capture the host-only snapshot and desired lab definition. Mark guest
   inventory as not yet provisioned, not as a complete campaign snapshot.
2. Validate the user-supplied VHDX, image compatibility, storage space, resource
   allocations, management/bootstrap design, and the proposed change list
   without modifying the host or image.
3. After approval, create separate writable guest disks from the supplied image,
   the three dedicated VMs, and three pairwise Private switches. Preserve the
   source image. Journal stable IDs and ownership of every created resource;
   name collisions or unowned existing objects must stop provisioning.
4. Bootstrap guest management using the approved design, configure test vNICs
   and static addresses, stage tools, and establish recoverability. Verify each
   operation and record before/after state and partial failures.
5. Capture a complete host-plus-guest snapshot after provisioning. The workload
   must reference this complete snapshot and pass live setup checks.
6. On reuse, match the lab ownership record and IDs, report drift, and repair
   only approved differences. Do not duplicate VMs/switches or adopt unrelated
   resources automatically.

For a failed partial setup, stop only run-owned helpers, preserve logs, and
record retained incomplete resources and safe retry steps. Cleanup must not
silently delete those resources. VM deletion, reimage, checkpoint restoration,
or replacement is a separately approved operation.

**Exit gate:** initial provisioning and reuse are both tested; the source image
and unrelated host resources remain unchanged, and partial setup is recoverable
without losing evidence or duplicating resources.

### Phase 4 — Define the scenario input and result contracts

Create a versioned scenario definition consumed by the run workflow. Retain
Markdown for human review; use a structured format for execution inputs.
Choose and document the format before implementing the runner.

The definition must include:

- Scenario ID/version, goal, system class, required snapshot fields, topology,
  supported OS/architecture, and expected device roles.
- Exact workload settings and command construction, expected connection and
  work counts, duration, repetitions, verifier state, and fault model.
- Every measurement's source, unit, interval, capture points, acceptance
  threshold, missing-data behavior, and artifact path.
- Resource bounds, monitor-loss behavior, stop conditions, cleanup/recovery
  operations, and allowed operator actions.
- Required and optional artifacts, summary calculations, outcome rules, and
  explicit handling for pilot/inconclusive results.

For G1B, require verifier-disabled configuration, no fault injection,
`-Verify:data`, and ctsTraffic connection CSVs. Define pre-execution, execution,
execution-end, and post-cooldown observations for connection results, user-mode
memory, kernel pool, process health, and system health.

**Exit gate:** a scenario file cannot be accepted if a required command,
measurement source, unit, or safety bound is missing. Acceptance mode also
requires every pass/fail threshold; characterization mode explicitly marks
the thresholds it is investigating and cannot report an acceptance pass.

### Phase 5 — Implement preflight and safe scenario execution

Build the scenario workflow in gated stages; preflight must finish successfully
before any state-changing action:

1. **Load and validate inputs:** scenario definition, snapshot ID, current
   system class, target allowlist, and output directory.
2. **Revalidate the environment:** confirm device identities, VM states,
   topology, addresses, management isolation, verifier state, image/tool
   versions, clock synchronization, recovery point, free disk/resources, and
   remoting health. Compare with the referenced snapshot and block on
   unexplained drift.
3. **Show a no-mutation plan:** list target devices, planned configuration
   changes, exact ctsTraffic commands, resource limits, stop conditions,
   artifact paths, and recovery actions. Require explicit operator approval
   before setup.
4. **Setup:** provision the dedicated lab using Phase 3A or verify the retained
   VM configuration, test links and addresses, stage and verify tools/scripts,
   establish remote operations,
   apply only scenario-declared OS settings, and verify the required verifier
   state. Capture the complete post-provisioning snapshot before workload
   execution. Do not proceed partially if any participant fails its setup gate.
5. **Pre-run baseline:** capture health, verifier state, connection/network
   state, memory and pool counters/snapshots, event-log position, and
   performance-sampling readiness.
6. **Execution:** coordinate listener/client start, persist exact commands and
   timestamps, collect ctsTraffic outputs/CSVs and time-series measurements,
   monitor every device, and stop/preserve evidence on a declared stop
   condition.
7. **Cooldown:** stop tools cleanly, record forced termination, wait for
   measured quiescence, collect post-execution and post-cooldown memory/pool
   data, confirm system health, and restore only the configuration declared by
   the scenario. Retain VMs and switches for reuse; do not revert a checkpoint
   before measuring post-run state or collecting evidence.
8. **Closeout:** collect and verify artifacts, write a manifest and summary,
   calculate criteria, classify the result as passed, failed, inconclusive, or
   aborted, and retain deviations/incidents.

The first runner must not enable verifiers or inject faults. Those paths need
separate reviewed scenario definitions and safety gates.

**Exit gate:** preflight performs no mutation; invalid inputs, wrong targets,
drift, missing tools, insufficient resources, inaccessible monitoring, or
failed recovery checks block setup with a clear reason. Once approved, every
phase records its result and cleanup is safe after both success and failure.

### Phase 5A — Test setup, execution, and cleanup with traceable measurements

Treat workflow-phase gates and scenario success criteria as separate but
linked results. A failed setup gate prevents workload execution; it is not a
scenario pass or a measured workload failure. Give every measurement a stable
ID and record its scenario/run ID, device ID, source, UTC timestamp, unit,
observed value, expected value or limit, result, and artifact reference. Map
each scenario criterion to all required phase measurements. Missing or
unreadable required evidence must prevent a pass. Report an observed scenario
failure as failed even if other evidence is missing; otherwise use inconclusive
or aborted as appropriate. Record workload outcome, evidence completeness,
and cleanup status separately so one cannot hide another.

#### Setup phase

Capture measurements before any setup mutation, after setup, and immediately
before execution. Compare the observed state with both the approved snapshot
and the scenario's desired state.

| Measurement ID | What to verify and record | Source / unit / timing | Setup gate |
| --- | --- | --- | --- |
| SET-01 Device identity and OS | Host and guest IDs, VM IDs, OS edition/version/build/architecture, boot/reboot state, and clock offset | Snapshot and live host/guest inventory; identifiers, version strings, UTC timestamps, clock offset | Every expected participant is present, uniquely identified, healthy, and within the approved clock tolerance. |
| SET-02 VM configuration | VM state, generation/config version, vCPU count, static/dynamic memory settings, firmware/Secure Boot, integration services, attached disks and recovery point | Hyper-V host configuration query/export compared with approved desired VM configuration | Every owned lab VM matches the approved scenario; missing or unexplained lab resources block execution. Inventory unrelated VMs for capacity but do not modify them. |
| SET-03 vSwitch and port configuration | Switch ID/name/type, isolation mode, physical-adapter binding, management-OS sharing, extensions, switch ports, VLAN/QoS settings, and attached vNICs | Hyper-V switch/port configuration query/export; record each switch and attachment | Each test link maps to the planned isolated switch and expected endpoints. No test mutation can reach the management path. No unexpected attachment or setting is allowed on owned test switches; unrelated switches are not modified. |
| SET-04 Guest vNIC and IP configuration | vNIC ID/name/MAC, connected switch, link state, VLAN/QoS, guest interface index, address/prefix, routes, neighbors, DNS, and gateways | Hyper-V vNIC inventory plus guest network configuration; values/counts and before/after comparison | Every endpoint has the exact planned test configuration, no duplicate address or unintended route exists, and the separate management connection is reachable. |
| SET-05 Capacity and recovery readiness | Assigned and available CPU/RAM/storage, recovery-point identity, free artifact space, and declared resource envelope | Host/guest inventory and live capacity counters; counts, bytes, percentages | Available capacity meets the calibrated minimum and remains within the approved envelope; recovery path and artifact space are available. |
| SET-06 Tools, remote operations, verifier state | WinRM/reachability, ctsTraffic/support-tool versions and hashes, driver versions, verifier configuration, monitor/counter availability | Remote command result, version/hash records, verifier-state capture; status and exact identifiers | Every device is controllable and has the approved tool/configuration; both verifiers are disabled for G1B; all required monitors can collect data. |
| SET-07 Baseline evidence readiness | Pre-run system memory, paged/nonpaged pool, required pool-tag snapshots, event-log position, network state, counter sample timestamps, and process-sampler readiness | Counter/snapshot outputs; bytes/counts and capture timestamps | Required baseline artifacts exist for every applicable device and have valid timestamps/units; sampling begins before workload launch. An absent ctsTraffic process has no process-memory baseline; capture its first live sample and label its workload stage. |
| SET-08 Setup mutation audit | Actual changes made by setup versus the approved plan, including VM, vSwitch, vNIC, IP, route, and verifier changes | Before/after configuration diff and setup operation log | Every change is declared and applied successfully; no unapproved change or unexplained drift remains. |

#### Execution phase

Capture a timestamped event and measurement stream from coordinated launch
through workload completion or abort. Record actual sample interval and any
missed samples; counters without timestamps or device attribution are not
acceptable evidence.

| Measurement ID | What to verify and record | Source / unit / timing | Scenario criterion |
| --- | --- | --- | --- |
| EXE-01 Invocation and process state | Exact command/config hash, target, process ID, launch/readiness/exit times, exit status, and listener/client roles | Runner event log and per-device process status; UTC timestamps/status | Required processes start on all assigned devices, execute the approved command, and exit as expected. Unexplained or nonzero exit fails G1B-07. |
| EXE-02 Connection outcomes | Attempted, established, completed, failed, retried/replaced connections and success rate | ctsTraffic aggregate output and per-connection CSV; counts and percent, from first attempt through final completion | Every failure is counted; zero unexpected failures for this no-fault run. Retries do not erase earlier failures (G1B-01). |
| EXE-03 Payload correctness | Data-verification successes/errors and completed transfers | ctsTraffic `-Verify:data` output; counts and bytes, over the complete run | All completed transfers verify; zero data-integrity errors (G1B-02). |
| EXE-04 Work and elapsed time | Completed connections, transferred bytes, actual workload duration, and work rate | ctsTraffic output/CSV plus coordinated timestamps; counts, bytes, seconds, rate | Meets the calibrated target and approved duration; repeated-run variance is assessed only after a reference/tolerance is approved (G1B-03). |
| EXE-05 User-mode memory | ctsTraffic process Private Bytes baseline, samples, peak, final live sample, and missing samples | Time-series process/performance-counter capture; bytes and UTC timestamps | Complete trace with defined sampling interval; compare warm-up-adjusted peak/final growth to approved threshold (G1B-04). |
| EXE-06 System memory and kernel pool | Available/committed memory and paged/nonpaged pool samples, peaks, and deltas | Per-device Windows performance-counter capture; bytes, timestamps, sample interval | No missing required windows; compare to the approved transient and persistent bounds (G1B-05). |
| EXE-07 Pool allocations | Required pre-run and post-cooldown pool-tag snapshots, plus any approved execution-time captures | Approved poolsnap/equivalent output; per-tag counts/bytes and UTC time | Snapshot capability is mandatory before any workload. Preserve attributable output; any leak conclusion follows the approved comparison method (G1B-06). |
| EXE-08 Device and verifier health | Process liveness, system uptime/reboot/bugcheck, relevant event-log additions, and verifier state | Per-device health monitor and event/crash logs; status/counts/timestamps | No bugcheck, unexpected reboot, unexplained process failure, monitor loss, or verifier activation (G1B-07, G1B-08). |
| EXE-09 Monitor completeness and stop decisions | Expected/actual sample counts, gaps, monitor process status, threshold crossings, and stop trigger | Monitor-controller log; counts, timestamps, threshold values | Missing critical monitoring or a stop-condition breach triggers safe abort and cannot produce a pass. |

#### Cleanup and cooldown phase

Cleanup is itself a tested phase, not an unmeasured epilogue. Capture state
after workload completion, after tool shutdown, after quiescence, and after
any declared restore action. For G1B, where no fault or configuration churn is
allowed, the expected final network/VM configuration is the approved baseline.

| Measurement ID | What to verify and record | Source / unit / timing | Cleanup gate / scenario criterion |
| --- | --- | --- | --- |
| CLN-01 Process and connection shutdown | Graceful stop request, process exit, forced terminations, residual listeners/connections, and drain duration | Runner/process/network status; statuses, counts, seconds | All helpers stop as declared; no unexplained residual process/connection remains. Forced termination is recorded and prevents a clean pass unless explicitly allowed. |
| CLN-02 VM/vSwitch/network state | VM state/configuration, vSwitch/switch-port/vNIC mappings, VLAN and adapter binding, guest addresses/routes/neighbors, management reachability | Re-query Hyper-V host and guests; diff against approved post-setup state after cleanup | Retain owned VMs/switches and verify they remain reusable. No unapproved configuration drift; management remains reachable. Treat dynamic neighbor/cache changes under an explicit comparison policy, not exact equality. Any unexplained mismatch is a cleanup failure. |
| CLN-03 Quiescence and post-run memory/pool | Residual activity, quiescence evidence, system memory/paged/nonpaged pool, and final pool-tag snapshot | Per-device counters/snapshots after stop and after cooldown; bytes/counts/elapsed time | Quiescence condition is met within the declared window; post-cooldown deltas satisfy approved limits (G1B-05, G1B-06). |
| CLN-04 User-mode memory closeout | Last live ctsTraffic sample and process exit time; explicitly mark post-exit process memory as not applicable | Execution time-series and process monitor; bytes/timestamps/status | Required live samples exist before exit and are linked to G1B-04; do not treat a vanished process as a zero-byte sample. |
| CLN-05 Health and verifier closeout | Final VM/host health, uptime/boot changes, event/crash review, verifier state | Health and verifier queries after cooldown; status/counts | No new bugcheck, unexpected reboot, or verifier activation; system remains usable (G1B-07, G1B-08). |
| CLN-06 Artifact completeness | Expected logs, CSVs, counter files, pool snapshots, configuration diffs, and manifests | Artifact inventory with file size/hash and source device | Every required artifact exists, is readable, time-correlated, and maps to a measurement ID. Missing critical evidence prevents pass and does not erase an already observed failure. |
| CLN-07 Outcome traceability | Each G1B criterion, its measured value, threshold, evidence reference, and final disposition | Generated run summary; criterion IDs and pass/fail/inconclusive result | The summary derives from recorded measurements; every criterion has evidence and an explicit disposition before the overall outcome is assigned. |

Link SET-01 through SET-08 and CLN-02 to configuration criterion G1B-00.
Link baseline SET-07 to G1B-04 through G1B-06 where applicable, and verifier
SET-06 to G1B-08. Require CLN-01 and CLN-05 for health criterion G1B-07.
CLN-06 and CLN-07 apply to every criterion and the overall run disposition.
Preserve separate per-device and per-link results; aggregates must not conceal
a failing endpoint or double-count both ends of a connection.

Before a full workload, exercise each phase's failure path on disposable or
mocked test targets: setup must refuse a bad VM/switch mapping, inaccessible
guest, unexpected verifier state, or insufficient space; execution must
preserve evidence and stop safely on a synthetic process/monitor failure; and
cleanup must report a deliberately non-stopping helper or failed restore.
These workflow-failure tests must not inject faults into the actual
G1B-CLEAN-BASELINE scenario.

### Phase 6 — Validate without a full workload

Run the following checks before the first full scenario:

1. Unit-test schema parsing, validation, calculations, output naming, and
   manifest generation with representative valid and invalid inputs.
2. Test snapshot capture against the local host and each guest individually;
   verify source values against Windows/Hyper-V inspection tools.
3. Test remote collection and artifact transfer with unavailable-device,
   access-denied, timeout, and partial-result cases.
4. Run scenario preflight in no-mutation mode; verify all intended targets and
   commands are displayed and no VM/network/OS state changes.
5. Exercise cleanup and recovery using a harmless synthetic failure before
   testing real ctsTraffic failure handling.
6. Complete a setup-only rehearsal with dedicated VMs, then a short
   operator-approved validation run using a selected configuration or optional
   characterization mode. Require the approved pool-tag tool before either.
   Confirm counters, CSVs, stop behavior, cooldown, and artifact collection work
   end to end. This validates the runner, not longhaul acceptance.
7. Test a second run against the retained lab. Verify stable VM/switch IDs,
   no duplicate provisioning, fresh baseline collection, and separate result
   directories. Test safe retry after each phase's partial failure.

**Exit gate:** all expected negative cases fail safely; no test mutates an
unapproved device or management path; evidence is complete and traceable.

### Phase 7 — Conduct the first full end-to-end scenario

After every earlier gate passes:

1. Provision the lab if necessary using the approved host-only preflight and
   Phase 3A; otherwise verify the retained lab. Capture and review a fresh
   complete system snapshot.
2. Select a compatible existing configuration or approved characterization
   output, validate its safety envelope, and finalize the immutable G1B scenario
   instance; ensure no execution-critical `TBD` remains.
3. Verify the declared recovery point and dedicated three-VM topology.
4. Run preflight, review the no-mutation plan, and obtain operator approval.
5. Execute setup, capture baselines, run the fixed-duration clean workload,
   monitor all participants, and perform cooldown.
6. Verify all expected artifacts and analyze connection outcomes, payload
   verification, work rate, user-mode memory, pool deltas, process/system
   health, and verifier state against the declared criteria.
7. Evaluate all criteria with their resolved thresholds. A run still
   establishing thresholds belongs to characterization, not full acceptance;
   missing evidence never counts as success.
8. Retain the lab and repeat under the same snapshot/scenario conditions enough
   times to assess work-rate repeatability and persistent memory/pool growth.
   Preserve each run independently and summarize comparisons without
   overwriting raw data.

## Real-user acceptance

Have a second operator, who did not implement the workflow, complete the
snapshot and scenario workflows using only the published instructions. Record
whether they can:

- Select `desktop` or `server`, identify participating devices, and understand
  which values are measured versus manually supplied.
- Produce and review a complete snapshot, recognize drift, and resolve
  missing-device or permission errors.
- Understand the scenario plan and safety boundaries before approving it.
- Run the pilot, find each required artifact, interpret the summary, and
  distinguish pass, fail, inconclusive, and aborted outcomes.
- Recover from at least one injected workflow-level failure without losing
  first-failure evidence or leaving the lab in an unknown state.

Do not expand the scenario matrix until this first user can complete the
workflow repeatably and all feedback affecting safety, correctness, or
interpretability is resolved.

## Go/no-go checklist for a full run

A full run is **NO-GO** if any item is false:

- [ ] Scenario contract, parameters, measurements, thresholds, and stop
  conditions are approved and have no execution-critical TBDs.
- [ ] Dedicated, recoverable host and guests match a complete reviewed system
  snapshot; snapshot and scenario schemas are versioned.
- [ ] Hyper-V topology, test addresses, management isolation, remote access,
  time synchronization, and recovery procedures have been validated.
- [ ] Tools, scripts, guest OS/build, drivers, hashes, verifier state, and
  permissions match the run definition.
- [ ] Preflight and no-mutation plan pass; resources are within the approved
  preset or characterized envelope; required pool-tag capture, monitoring,
  and artifact storage are ready.
- [ ] Setup-only, short validation run, failure cleanup, retained-lab reuse,
  and a second-operator walkthrough using the rehearsal
  have passed.
- [ ] Operator explicitly approves the exact run plan and target devices.

## Implementation deliverables and run-time inputs still to resolve

The policies above are decided. The following are explicit work items, not
permission to invent settings during a run. Resolve and review them in the
relevant design stage; missing deliverables block provisioning or workload
execution at the specified gate, not documentation of this implementation plan.

Resolve the relevant decisions in the [plan index](README.md) and
[run lifecycle](run-lifecycle.md), especially:

- Snapshot serialization format, retention, redaction, and snapshot-vs-run
  versioning.
- Exact Hyper-V fields and supported configuration sources to use for VM,
  vSwitch, switch-port, vNIC, and host-adapter capture and comparison.
- WinRM identity, authentication, permissions, and firewall configuration.
- User-supplied Windows 11 RTM VHDX path, per-VM allocations, and recovery-point
  policy. Automated provisioning and retained-lab reuse are required; reimage
  and destructive recovery are not normal cleanup.
- Static address allocation, management-path isolation, and exact three-VM
  topology.
- Approved performance counters and required pool-tag tool, sampling interval,
  optional characterization procedure, reviewed preset inventory, repeat count,
  work-rate tolerance, and memory/pool thresholds.
- Exact ctsTraffic parameter semantics, CSV interpretation, command lifecycle,
  and supported stop behavior.
