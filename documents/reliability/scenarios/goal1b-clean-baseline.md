# G1B-CLEAN-BASELINE — Uninstrumented TCP Work and Memory Baseline

Status: **Draft scenario; execution remains gated**. This is the first concrete
Goal 1B scenario. Use a reviewed existing workload configuration or optional
system characterization to resolve parameters. It is not a repeatability or
leak pass until workload and memory thresholds are approved.

## Run definition

- **Goal:** 1B — repeatable workload and uninstrumented memory baseline.
- **System class:** `desktop` for the initial dry run; capture a device profile
  for every participating device.
- **Topology:** initial three-VM Hyper-V topology with pairwise private
  switches; finalize VM roles and address mapping in the run record.
- **VM/vSwitch configuration:** record expected VM IDs, generation/config
  version, vCPU and memory settings, VHDX/checkpoint references, vNIC IDs/MACs,
  switch type and IDs, vNIC-to-switch attachments, VLAN/QoS settings,
  extensions, and physical-adapter bindings. For each Private switch, verify
  that no physical adapter is bound. Compare live configuration before setup,
  after setup, and after cleanup.
- **Guest image:** Windows 11 RTM planned; record the actual edition, version,
  build, and architecture after installation. VHDX path is a campaign input.
- **Provisioning and cleanup:** the workflow creates the three dedicated VMs
  and test switches from the supplied VHDX. Retain them for reuse after
  cooldown; stop tools and verify health/configuration without automatically
  reverting, reimaging, or deleting resources.
- **Protocol / IO model / pattern:** TCP / IOCP / duplex.
- **Address family:** one family per run (IPv4-only or IPv6-only); record the
  selected family. Do not combine the families in one run.
- **Data verification:** explicitly pass ctsTraffic `-Verify:data`.
- **Application Verifier / Driver Verifier:** both disabled. Capture the
  resulting configuration before execution and verify it is still disabled
  after the run.
- **Fault injection:** none. Do not disconnect links, alter network
  configuration, or inject protocol/resource failures during this scenario.
- **Duration, connection count, transfer settings, repetitions:** resolve from
  a compatible versioned configuration or optional system characterization.
  Include intended load, resource bounds, and stop conditions before execution;
  configuration selection alone does not establish safety on this system.
- **ctsTraffic connection logging:** enable `-ConnectionFilename`; preserve the
  per-connection CSV and aggregate output.
- **Pool-tag evidence:** an approved, working pool-tag snapshot tool is
  mandatory before any workload, including characterization pilots.

## Measurement points

1. **Pre-execution baseline:** after setup and immediately before workload
   launch, record system memory, paged/nonpaged pool usage, and a required
   pool-tag snapshot using the approved tool. Start time-stamped performance
   sampling before launching ctsTraffic.
2. **Execution:** collect ctsTraffic status and per-connection outcomes;
   sample ctsTraffic process private bytes and system memory/pool counters
   throughout the run. Preserve the final sample before process exit, if
   available, and record the sampling interval and any missing samples.
   If ctsTraffic cannot be held idle before load, use its first live sample as
   the process baseline and label the workload stage; do not invent a
   pre-launch process-memory value.
3. **Post-execution / cooldown:** record system memory and pool counters when
   the workload ends, then stop tools cleanly and wait for the declared
   quiescent condition. Capture another system memory/pool snapshot after
   cooldown. A process-memory sample is not applicable after ctsTraffic exits;
   use its time-series and final live-process sample instead.

## Measurements and acceptance

| Criterion ID | Measurement | Source / unit / capture points | Success criterion |
| --- | --- | --- | --- |
| G1B-00 | Environment/configuration conformance | Approved snapshot compared with live Hyper-V VM, vSwitch/switch-port/vNIC and guest OS/network configuration; before setup, after setup, and after cleanup | Actual configuration matches the reviewed scenario before execution; owned VMs/switches remain reusable with no unapproved drift after cleanup. Compare final state to the approved post-setup state; account explicitly for dynamic neighbor/cache changes. |
| G1B-01 | Connection attempts, successes, failures, retries, and success rate | ctsTraffic aggregate output and `-ConnectionFilename` CSV; counts and percent (successful completions / attempts); entire execution | Every failure is accounted for; zero unexpected connection failures in this no-fault run. Report retries separately; they do not erase failed attempts. |
| G1B-02 | Payload correctness | ctsTraffic `-Verify:data` result and error output; verified transfers and error count; entire execution | All completed transfers pass data verification; zero data-integrity errors. |
| G1B-03 | Completed work and work rate | ctsTraffic output/CSV; completed connections and transferred bytes over elapsed time; entire execution | Meet the resolved target for the declared fixed duration. Target and repeated-run variance must come from an approved configuration or characterization output; without them, execution is characterization only. |
| G1B-04 | User-mode memory | Time-stamped Windows process/performance-counter capture for the ctsTraffic process; Private Bytes in bytes; pre-load, execution samples/peak, and final live sample before process exit | Record baseline, peak, and final live sample. Acceptable growth after warm-up is TBD; do not claim a leak pass until a threshold and repeat-comparison method are approved. |
| G1B-05 | System memory and kernel pool usage | Windows system performance-counter capture for available/committed memory and paged/nonpaged pool; bytes; pre-execution, execution, execution-end, and post-cooldown | Record deltas, peaks, and post-cooldown values. Acceptable transient and persistent growth are TBD. |
| G1B-06 | Pool allocation detail | Approved poolsnap or equivalent pool-tag snapshot; capture time and per-tag allocation/byte counts; required pre-run and post-cooldown snapshots, plus approved execution points | Retain comparable required snapshots. Tag-level leak threshold and attribution method must be approved for acceptance. Missing tool capability blocks all workloads; missing required output prevents pass. |
| G1B-07 | Process/system health | ctsTraffic exit status, process monitor, Windows event/crash evidence; status and event counts; setup through cleanup | ctsTraffic exits successfully; no bugcheck, unexpected reboot, unexplained process failure, or required-monitor loss. |
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
  incomplete target, nonzero/unexpected process failure, bugcheck, unexpected
  verifier activation, or memory/pool threshold violation.
- Preserve raw ctsTraffic CSVs, aggregate logs, time-series counter data,
  before/after pool snapshots, device profiles, verifier-state evidence, and
  the run configuration. Do not summarize expected failures: this scenario
  injects none.

Use the [readiness plan](../end-to-end-readiness-plan.md) for phase tests,
measurement IDs, parameter-selection routes, and implementation gates.
