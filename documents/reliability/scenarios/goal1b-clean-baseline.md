# G1B-CLEAN-BASELINE — Uninstrumented TCP Work and Memory Baseline

Status: **Draft for pilot**. This is the first concrete Goal 1B scenario. The
initial run establishes measurements; it is not a repeatability or leak pass
until the TBD workload and memory thresholds are calibrated and approved.

## Run definition

- **Goal:** 1B — repeatable workload and uninstrumented memory baseline.
- **System class:** `desktop` for the initial dry run; capture a device profile
  for every participating device.
- **Topology:** initial three-VM Hyper-V topology with pairwise private
  switches; finalize VM roles and address mapping in the run record.
- **Guest image:** Windows 11 RTM planned; record the actual edition, version,
  build, and architecture after installation. VHDX path is a campaign input.
- **Protocol / IO model / pattern:** TCP / IOCP / duplex.
- **Address family:** one family per run (IPv4-only or IPv6-only); record the
  selected family. Do not combine the families in one run.
- **Data verification:** explicitly pass ctsTraffic `-Verify:data`.
- **Application Verifier / Driver Verifier:** both disabled. Capture the
  resulting configuration before execution and verify it is still disabled
  after the run.
- **Fault injection:** none. Do not disconnect links, alter network
  configuration, or inject protocol/resource failures during this scenario.
- **Duration, connection count, transfer settings, repetitions:** TBD; set
  after per-device load calibration. Include the intended load, resource
  bounds, and stop conditions before execution.
- **ctsTraffic connection logging:** enable `-ConnectionFilename`; preserve the
  per-connection CSV and aggregate output.

## Measurement points

1. **Pre-execution baseline:** after setup and immediately before workload
   launch, record system memory, paged/nonpaged pool usage, and a pool snapshot
   where the approved tool is available. Start time-stamped performance
   sampling before launching ctsTraffic.
2. **Execution:** collect ctsTraffic status and per-connection outcomes;
   sample ctsTraffic process private bytes and system memory/pool counters
   throughout the run. Preserve the final sample before process exit, if
   available, and record the sampling interval and any missing samples.
3. **Post-execution / cooldown:** record system memory and pool counters when
   the workload ends, then stop tools cleanly and wait for the declared
   quiescent condition. Capture another system memory/pool snapshot after
   cooldown. A process-memory sample is not applicable after ctsTraffic exits;
   use its time-series and final live-process sample instead.

## Measurements and acceptance

| Measurement | Source / unit | Success criterion |
| --- | --- | --- |
| Connection attempts, successes, failures, retries, and success rate | ctsTraffic aggregate output and `-ConnectionFilename` CSV; counts and percent (successful completions / attempts) | Every failure is accounted for; zero unexpected connection failures in this no-fault run. Report retries separately; they do not erase failed attempts. |
| Payload correctness | ctsTraffic `-Verify:data` result and error output; verified transfers and error count | All completed transfers pass data verification; zero data-integrity errors. |
| Completed work and work rate | ctsTraffic output/CSV; completed connections and transferred bytes over elapsed time | Meet the calibrated target for the declared fixed duration. Target and acceptable repeated-run variance are TBD; until set, this pilot is characterization only. |
| User-mode memory | Time-stamped Windows process/performance-counter capture for the ctsTraffic process; Private Bytes in bytes, including pre-load/live-end/peak samples | Record baseline, peak, and final live sample. Acceptable growth after warm-up is TBD; do not claim a leak pass until a threshold and repeat-comparison method are approved. |
| Kernel pool usage | Windows system performance-counter capture for paged and nonpaged pool; bytes, pre-execution, execution end, and post-cooldown | Record deltas and post-cooldown values; acceptable transient and persistent growth are TBD. |
| Pool allocation detail | Approved poolsnap or equivalent pool-tag snapshot; capture time and per-tag allocation/byte counts | Retain comparable pre-run and post-cooldown snapshots when available. Any tag-level leak threshold and attribution method are TBD. |
| Process/system health | ctsTraffic exit status, process monitor, Windows event/crash evidence; status and event counts | ctsTraffic exits successfully; no bugcheck, unexpected reboot, or unexplained process failure. |
| Verifier state | Captured Application Verifier and Driver Verifier configuration; enabled/disabled state | Both remain disabled for the full run; any unexpected enabled verifier invalidates the run. |

## Outcome rules

- **Pass:** only after calibrated duration/work targets and user-mode and
  kernel-pool thresholds are set, all required evidence is present, and every
  criterion above is met.
- **Pilot / inconclusive:** the clean workload completes but one or more
  numeric calibration or memory thresholds are still TBD, or required
  measurement evidence is missing.
- **Fail:** any unexpected connection failure, data-integrity error,
  incomplete target, nonzero/unexpected process failure, bugcheck, unexpected
  verifier activation, or memory/pool threshold violation.
- Preserve raw ctsTraffic CSVs, aggregate logs, time-series counter data,
  before/after pool snapshots, device profiles, verifier-state evidence, and
  the run configuration. Do not summarize expected failures: this scenario
  injects none.
