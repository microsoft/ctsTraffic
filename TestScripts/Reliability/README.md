# CtsReliability lab scripts

PowerShell tooling for the Windows networking reliability lab described in
[documents/reliability](../../documents/reliability/README.md). The lab values
come from [`lab.v1.json`](lab.v1.json) and are explained in
[lab configuration](../../documents/reliability/lab-configuration.md).

Every script that changes the system **plans by default** and changes nothing
until you add `-Apply`.

| Script | Purpose | Needs elevation |
| --- | --- | --- |
| `Set-CtsLocalConfig.ps1 [-DebuggerSourceAmd64 <msi>] [-DebuggerSourceArm64 <msi>] [-Apply]` | Plan or write the internal debugger share paths to `C:\ctsReliability\lab.local.json`, outside the repository. Prompts for this host's architecture if no path is given | No |
| `Set-CtsGuestCredential.ps1` | Prompt for the guest `ctsadmin` password and store it in Windows Credential Manager | No |
| `Update-CtsDebuggerCache.ps1` | Refresh the host's cached debugger MSI when the share has a newer version | No |
| `Get-CtsWindowsIso.ps1 [-Source <url-or-path>] [-ExpectedSha256 <hash>] [-Apply]` | Plan or cache the Windows ISO under `C:\ctsReliability\iso`, verifying its SHA-256. A cached ISO is reused and only replaced with `-Refresh` | No |
| `New-CtsGoldenImage.ps1 [-IsoPath <iso>] [-Apply]` | Plan or build the read-only golden VHDX, from the cached ISO by default | For `-Apply` |
| `Invoke-CtsLabProvision.ps1 [-Apply]` | Plan or create switches, VMs and disks, configure guests, install the debugger, and take the clean checkpoint | For `-Apply` |
| `New-CtsSystemSnapshot.ps1 [-HostOnly]` | Capture a read-only host and guest snapshot | Hyper-V Administrators |
| `Compare-CtsSystemSnapshot.ps1 -Reference <dir> -Difference <dir>` | Report drift between two snapshots | No |
| `Get-CtsPoolTagSnapshot.ps1 [-Path <csv>]` | Capture kernel pool usage per tag | Yes |
| `Compare-CtsPoolTagSnapshot.ps1 -Reference <csv> -Difference <csv>` | Report pool growth per tag | No |

First-time setup order, run from an elevated session. Never commit internal
server names or paths to this repository; `Set-CtsLocalConfig.ps1` keeps them
in `C:\ctsReliability\lab.local.json`, outside it.

```powershell
.\TestScripts\Reliability\Set-CtsLocalConfig.ps1                                             # prompts; review the plan
.\TestScripts\Reliability\Set-CtsLocalConfig.ps1 -DebuggerSourceArm64 '<path-to-dbg_arm64.msi>' -Apply
.\TestScripts\Reliability\Set-CtsGuestCredential.ps1
.\TestScripts\Reliability\Update-CtsDebuggerCache.ps1
.\TestScripts\Reliability\Get-CtsWindowsIso.ps1 -Source '<url-or-path>' -ExpectedSha256 <hash>          # review the plan
.\TestScripts\Reliability\Get-CtsWindowsIso.ps1 -Source '<url-or-path>' -ExpectedSha256 <hash> -Apply
.\TestScripts\Reliability\New-CtsGoldenImage.ps1                                             # review the plan
.\TestScripts\Reliability\New-CtsGoldenImage.ps1 -Apply
.\TestScripts\Reliability\Invoke-CtsLabProvision.ps1                                         # review the plan
.\TestScripts\Reliability\Invoke-CtsLabProvision.ps1 -Apply
```

Microsoft does not offer a stable download URL for the Windows ISO, so
`Get-CtsWindowsIso.ps1` does not scrape microsoft.com. Get a link from the
[Windows 11 Arm64 download page](https://www.microsoft.com/en-us/software-download/windows11arm64)
(it expires after about 24 hours) and pass it, quoted, with the SHA-256 the
page publishes. Only the URL without its query string is recorded. If no hash
is published, `-AllowUnverified` caches the ISO and records it as unverified.

The implementation is the `CtsReliability` module: `CtsReliability.psm1`
loads `src\*.ps1`.

The device inventory records both configured and active Driver Verifier query
output; a failed or empty query marks the verifier section unavailable.

The draft G1B scenario can be inspected with `Test-CtsScenario -Path
<scenario.json>` after importing the module. This preliminary check always
blocks execution; full scenario validation and a runner are not implemented.

`Get-CtsPilotGuestSample` reads guest boot time and free resources through an
existing session; `Test-CtsPilotGuestSample` reports resource or reboot
findings against supplied limits. These are individual observations, not a
monitor loop, complete preflight, or authorization to run traffic.

Internal network-sample helpers record the server address and the client's
selected source, route, and adapter without sending traffic. They can report
address, route, and adapter findings against supplied expectations. These
are point-in-time observations, not proof of firewall reachability, VM identity,
future route selection, complete preflight, or authorization to run.

An internal binary check compares one guest snapshot's recorded executable
size and SHA-256 with an expected hash. The snapshot is point-in-time evidence:
the check does not verify its provenance or prevent later binary replacement,
and does not authorize a run.

Internal pilot-process helpers retain process handles in a guest session and
create stdout/stderr files exclusively. Forced stop targets the tracked PID
while its original handle is pinned. Loss of the session also loses the
handles. An optional job wait timeout reports an unknown outcome on timeout;
it does not bound job submission or cleanup, or establish that a timed-out
start did not execute. These helpers are not a complete pilot controller.

`support\PilotGuardian.ps1` is an experimental guardian which starts its
child in a kill-on-close job. Loss of required monitoring, including heartbeat
loss or guardian termination, can stop that child. Resource threshold breaches
are recorded as warnings and do not themselves stop it. A child exit code,
including a nonzero code from connection errors, does not stop the peer.
There is no guardian lifetime cutoff; the configured ctsTraffic time limit
remains separate. Sampling can miss resource breaches, and a stalled guardian
cannot be assumed to enforce monitoring loss.

An internal heartbeat helper creates and updates a guardian heartbeat through
a guest session. A timeout, remoting error, or logging failure leaves the
outcome unknown; the wait timeout does not bound submission or job cleanup.
The helper is not a continuous monitor or a pilot controller.

An internal experimental guardian launcher creates a heartbeat and starts an
already-staged guardian using the session-owned process helper. It checks
guardian source hashes before launch, but does not prevent later modification.
A successful start does not prove that the guardian or child initialized.
An uncertain start must not be retried as if no guardian was launched.
Do not use previously staged guardians: older versions also stop children for
resource thresholds or their own lifetime cutoff.

An internal startup observer pulses one launched guardian while reading its
child-start event and checking the tracked guardian process. A start event is
historical evidence, not proof the child is still alive or listening on TCP;
the child PID must not be used to stop a process. It assumes the receipt and
event file belong to the same reserved run and does not prevent tampering.
The caller must immediately continue heartbeats for a running guardian after
the observer returns; otherwise heartbeat loss can stop its child. A timeout
or unknown result is not proof that no child started. Its timeout does not
bound remote submission, cleanup, or blocking calls, and it does not coordinate
or authorize a run.

An internal one-shot listener observation checks the server's exact IPv4
address, port, and child PID without making a connection, while pulsing and
checking the tracked guardian before and after the query. It requires matching
startup and launch receipts and assumes their event file is trustworthy. A
positive result is only a point-in-time observation, not proof of client
reachability or continuing readiness. The caller must maintain heartbeats
afterward and for any other running guardian. Remote calls may block beyond the
job wait timeout. This check is not a coordinated startup or authorization.
The guardian retains the child's process handle until it exits, preventing PID
reuse while its liveness checks bracket the observation. Older staged guardians
do not provide this guarantee and must not be used for this check.
An internal server-readiness gate combines the startup observer and repeated
listener observations; it does not start the client or maintain heartbeats after
returning. Both timeouts are polling deadlines, not hard remote-call bounds.

An internal staging helper exclusively creates the guardian script, native
source, and specification in a supplied guest directory and checks their
SHA-256 hashes after writing. A failed or interrupted staging operation can
leave partial files; it must not be blindly retried in that directory. It does
not validate directory access controls, prevent later modification, verify the
child executable, or authorize a run. Staging uses a synchronous guest call
without a hard timeout.

An internal completion-evidence reader checks matching guardian launch and
tracked-process completion receipts, then reads and validates the guardian
result. Missing, malformed, contradictory, or inaccessible evidence is
unknown. A normal child exit does not by itself establish the traffic outcome.
The reader assumes its receipts and guest files belong to the same reserved
run; it does not validate their provenance or protect them against changes.
Its job wait timeout does not bound submission or cleanup.

An experimental two-guardian monitor pulses and observes already-started
guardians. On a monitoring failure for one guardian, it stops pulsing that
guardian, which can stop its child on heartbeat loss; it continues observing
the other guardian. A nonzero child exit does not interrupt the peer. The
monitor does not handle coordinated startup, unknown starts, session-loss
recovery, complete preflight, or traffic classification. Calls can block
despite their job wait timeouts.

An experimental endpoint worker uses a separate host job and guest session
to stage, launch, and monitor one guardian. A failed or nonzero child does not
directly stop any process; loss of that worker's monitoring can stop its child.
The worker can start the supplied executable and is not authorization to run
traffic. Its parent must keep the job alive, consume and retain its events,
and never blindly retry an unknown start. This is not a coordinated controller,
does not validate preflight or evidence provenance, and its remote calls can
block despite wait timeouts.

An internal observation helper collects each worker's ordered job events and
marks invalid event ordering or launch identity, missing completion, or job
failures unknown without stopping its peer. It retains received events in
memory; the parent must persist them. It does not fully validate event contents,
detect a stalled worker, prove continued listener readiness, or authorize a
client launch.

The internal pilot command resolver produces the documented finite-pilot
argument vectors and artifact paths without starting traffic. It does not
create or reserve output directories, verify targets, or authorize a run.
An internal helper can reserve a new guest run directory exclusively; it does
not validate the parent directory's access controls or authorize a run. The
intended run layout retains logs and dumps in a unique directory under
`C:\ctsReliability` on the host and guests. Complete collection of dumps from
their configured shared or fixed paths into that directory is not implemented.
An internal layout helper reserves matching host, server, and client directories
under a supplied existing root. It records partial or unknown reservations and
never deletes them or retries. It does not check directory access controls,
prove guest identity, prevent later modification, bound remote calls, or
authorize a run.
Internal WER inventories compare dump names and metadata before and after a run,
flagging changed or missing pre-existing files and guest reboots. New or changed
files are candidates, not proof of attribution or completed writes; unchanged
metadata does not prove unchanged content. A separate internal helper can copy
a candidate into a reserved guest run directory with exclusive creation and
hash verification, without deleting the original. A separate helper transfers
that copy into a newly reserved host run subdirectory and verifies its hash and
size. These helpers do not prove attribution, WER completion, directory ACLs,
or protection against later modification. Failed transfers may leave partial
files and must not be blindly retried in the same directory. System-dump
collection is not implemented. Remote calls lack hard deadlines.

An internal endpoint WER cleanup helper takes a pre-run inventory, inventories
again after the workload, and attempts one verified guest copy and host transfer
per new or changed candidate. It records reboots, ambiguous or missing files,
and per-copy unknown outcomes, without deleting originals or retrying uncertain
copies. Its caller must ensure the workload has ended and persist the returned
evidence. Completion describes these operations only, not attribution, WER
write completion, or full run cleanup.

`Get-CtsTrafficTcpSummary -Path <stdout-file>` reads an already-collected
stdout file and reports whether a complete TCP summary is present. It does not
run ctsTraffic, collect files, classify the result, or replace the original
stdout and exit code.

`Get-CtsTrafficTcpConnectionResults -Path <connection-csv>` counts the result
strings in a collected TCP connection CSV. It does not prove the file is
complete, count every attempt, or determine the run outcome. The reader has
only been checked against synthetic CSVs, not an actual traffic run.

`Get-CtsTrafficTcpStatus -Path <status-csv>` reads the last recorded TCP
status sample from a collected file. Rates are interval bytes per second;
completed and error counts are cumulative. It does not prove completeness or
classify the run, and has only been checked against synthetic CSVs.

Pool-tag CSV usage values and comparison deltas are **bytes**; allocation,
free, and outstanding-allocation values are counts. New CSV headings include
the units. The comparison script also accepts older CSVs with unlabeled
headings; module-returned PowerShell objects retain their original names.

## Tests

Every script has Pester tests in `Tests\`. Run them under both PowerShell
editions with:

```powershell
.\TestScripts\Reliability\Tests\Invoke-CtsReliabilityTests.ps1
```

See [reliability tooling tests](../../documents/reliability/tooling-tests.md)
for why the tests exist, what each file proves, tags, recorded results, and
the manual elevated checks still to be done.
