# Leak Detection Workflow

Status: **Draft; approved approach, thresholds TBD**. This document is the
authoritative definition of how Goal 1B detects user-mode memory and kernel
pool leaks. Scenarios reference it rather than restating it.

## Why a single before/after snapshot is not enough

Windows networking components grow caches, lookaside lists, and per-connection
structures on first use and retain them afterwards. A single before/after
comparison cannot distinguish that one-time warm-up growth from a leak. A leak
is growth that **keeps happening in proportion to work** across repeated,
identical workloads on the same boot.

## Scope and preconditions

- Applies to verifier-disabled runs (Goal 1B). Verifiers change allocation
  behavior and are not used for leak or work-rate measurements.
- All iterations run on the same boot of every VM. A reboot, crash, or
  verifier change invalidates the series; start over.
- The approved pool-tag snapshot tool must work on the target build and
  architecture before the first iteration (see
  [open decisions](README.md#open-decisions)).
- Use the same workload configuration, `-TimeLimit`, topology, and address
  family for every iteration.

## Workflow

1. **Start persistent servers.** Start the ctsTraffic server (listener) on each
   server VM once and keep it running across the warm-up and all measured
   iterations. Its process memory, handle count, and thread count across
   iterations are the primary user-mode leak signal.
2. **Measure the noise floor.** With servers listening and no client traffic,
   capture `K` snapshots (default 3) at a fixed interval. The per-metric spread
   is the noise floor used by the analysis.
3. **Warm-up iteration.** Run one full client iteration. Retain its artifacts
   but exclude it from the trend; it absorbs first-use cache growth.
4. **Quiesce, then snapshot `S0`.** Apply the quiescence gate below and take
   the baseline snapshot.
5. **Measured iterations.** Run `N` identical client iterations (approved
   default `N = 5`). After each, apply the quiescence gate and take snapshot
   `S1` … `SN`. During each iteration, sample the client process time series
   (private bytes, handles, threads) and the system pool counters.
6. **Analyze** each metric as described below.
7. **Confirm suspects.** A flagged metric triggers a confirmation run (more
   iterations and/or a longer series) with targeted pool-allocation tracing for
   the flagged tag. Do not run confirmation tracing during the measured series,
   because tracing changes allocation behavior.

### Quiescence gate

Take a snapshot only after all of these hold, and record the elapsed wait:

- Every client ctsTraffic process has exited and its exit code is recorded.
- No test-port TCP connections remain on the test interfaces other than the
  server listeners, including `TIME_WAIT` (Windows default is 120 seconds; use a
  bounded wait and record the residual count if it expires).
- CPU use is below the declared idle threshold for the declared settle
  interval.

If the gate times out, take the snapshot anyway, mark it `not quiescent`, and
treat the iteration as inconclusive for leak analysis.

## What each snapshot captures (per VM)

| Category | Metrics |
| --- | --- |
| Kernel pool by tag | Paged and nonpaged allocation count and bytes per tag (approved tool) |
| Kernel pool totals | `Memory\Pool Paged Bytes`, `Memory\Pool Nonpaged Bytes` |
| System memory | `Memory\Committed Bytes`, `Memory\Available Bytes` |
| System objects | Total handle count, process count, thread count |
| Persistent server process | Private bytes, working set, handle count, thread count |
| Network state | TCP connection counts by state on the test interfaces |
| Work completed | Cumulative completed connections and bytes from ctsTraffic output |

The client process exits each iteration, so its leak signal is the
per-iteration time series: its steady-state and peak private bytes and
handle counts should be flat across iterations, not rising.

## Analysis

For each metric, using `S0` … `SN`:

1. Compute interval deltas `d1` … `dN` and the least-squares slope.
2. Normalize growth by work done: bytes (or count) per 1,000 completed
   connections. Fixed-time iterations complete slightly different amounts of
   work, so normalization is required for comparison.
3. Flag the metric as a **suspected leak** only if both hold:
   - It grew in at least `N - 1` of the `N` intervals.
   - Total growth `SN - S0` exceeds the larger of: the noise-floor multiple,
     the absolute floor, and the relative floor for that metric class.
4. Report every metric with its deltas, slope, normalized growth, and
   flag state, sorted by normalized growth.

Thresholds (noise-floor multiple, absolute and relative floors per metric
class) are **TBD** and must be calibrated from pilot runs and approved before
a leak criterion can pass. Until then, the analysis output is characterization
only.

## Outcomes

| Result | Meaning |
| --- | --- |
| No leak detected | No metric flagged, all snapshots quiescent, thresholds approved. |
| Suspected leak | One or more metrics flagged; requires a confirmation run before filing. |
| Confirmed leak | Growth continues in proportion to work in the confirmation run; file with tag, rate per 1,000 connections, and trace evidence. |
| Inconclusive | Missing snapshots, a non-quiescent snapshot, a reboot during the series, or unapproved thresholds. |
