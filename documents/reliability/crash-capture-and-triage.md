# Crash Capture, Hang Detection, and Failure Triage

Status: **Draft; policy approved, settings TBD**. This document is the
authoritative definition of how test VMs capture crash and hang evidence and
how run failures are classified.

## Crash-capture policy

- **Always:** every test VM is configured to write a **complete memory dump**
  on bugcheck. Setup verifies the configuration and that the dump target has
  enough free space (at least VM memory plus margin) before any workload.
- **Optional:** a deployment may additionally attach a kernel debugger over
  **KDNET**. This is an opt-in per campaign, not a requirement. The dump
  configuration stays enabled when a debugger is attached.

### Complete memory dump (required)

Setup applies and records:

- Crash-dump type set to complete memory dump, a fixed dump file path, and
  automatic reboot after the dump is written so the runner can detect the
  bugcheck and recover the VM.
- A page file or dedicated dump file large enough for a complete dump.
- Injected-NMI crash dumps enabled, so the host can force a dump of a hung VM.
- Windows Error Reporting local dumps (full) for `ctsTraffic.exe` and other
  scenario-owned processes, written to a known folder.

Detection and collection:

- The runner detects a bugcheck by an unexpected change in guest boot time,
  the system's bugcheck event after reboot, or the presence of a new dump file.
- Before the next run, the runner copies the dump and the event-log evidence
  into the run's result bundle and records the bugcheck code.

### Kernel debugger over KDNET (optional)

| Consideration | Guidance |
| --- | --- |
| Transport | Use a dedicated debug vNIC on the management or a debug-only switch. Never use a test link, because debugger traffic would perturb the system under test. |
| When attached | A break-in freezes the VM. The runner must treat a VM that stops responding while a debugger is attached as **held at debugger**, not inject an NMI or reset it. |
| When not attached | Do not leave kernel debugging enabled without an attached debugger; a break-in could stall the VM waiting for one. |
| Setup check | Verify the debugger connects before the workload starts and record the connection details (no keys in artifacts). |
| Value | Live inspection of break-ins, assertions, and verifier stops that would otherwise only appear as a dump, and live analysis of hangs. |

## Hang detection

A VM can hang without bugchecking, which produces no dump on its own.

1. Monitor each VM with the Hyper-V heartbeat and a PowerShell Direct probe,
   every 60 seconds.
2. If a VM misses 5 consecutive probes (5 minutes) and no debugger is
   attached, the host injects an NMI to force a complete dump.
3. Record the trigger time, last good probe, and dump result. Classify as a
   hang (below), not as an ordinary bugcheck.

## Failure triage buckets

Every run failure is assigned exactly one primary bucket. Product-bug buckets
are kept separate from harness and lab problems.

| Bucket | Typical evidence | First response |
| --- | --- | --- |
| Bugcheck | Complete memory dump, bugcheck code, faulting module | Analyze the dump; bucket by bugcheck code and faulting module/stack. |
| Hang | NMI-forced dump, last-good-probe time | Analyze waits, locks, and stuck threads in the dump. |
| Kernel verifier violation | Driver Verifier bugcheck dump | Analyze as a bugcheck; identify the verified driver at fault. |
| User-mode verifier stop | Application Verifier log and process dump | Analyze the verifier stop and stack. |
| Process crash | WER dump of `ctsTraffic.exe` or a scenario helper | Determine whether ctsTraffic or the stack is at fault. |
| Unexpected connection or data error | ctsTraffic error output, connection CSV, status file | Correlate with time series, faults, and system events. |
| Suspected leak | [Leak detection](leak-detection.md) analysis | Run the confirmation workflow before filing. |
| Work-rate regression | Normalized work compared with the approved baseline | Rerun to rule out noise; compare configuration and host load. |
| Harness or lab failure | Runner logs, WinRM/PowerShell Direct errors, host issues | Fix the harness; the run is aborted, not a product failure. |
| Inconclusive | Missing or unreadable required evidence | Fix evidence collection; never counted as a pass. |

Deduplicate product failures by bucket plus signature (bugcheck code and
faulting module, verifier stop code, or top stack frames) so repeat hits are
counted against one issue.
