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

Internal pilot-process helpers retain process handles in a guest session and
create stdout/stderr files exclusively. Forced stop targets the tracked PID
while its original handle is pinned. Loss of the session also loses the
handles. An optional job wait timeout reports an unknown outcome on timeout;
it does not bound job submission or cleanup, or establish that a timed-out
start did not execute. These helpers are not a complete pilot controller.

`support\PilotGuardian.ps1` is an experimental standalone guardian. It confines
itself and its child to a kill-on-close job and watches a heartbeat, local
memory and disk, and a separate lifetime limit. It is not integrated into a
pilot controller; continuous host heartbeats, coordinated start, recovery, and
preflight are still required. Sampling cannot prevent a resource breach
between checks, and a stalled guardian cannot be assumed to enforce its limit.
Heartbeat age includes time elapsed before each observation; an overdue
observation fails closed even if a newer heartbeat is present.

An internal heartbeat helper creates and updates a guardian heartbeat through
a guest session. A timeout, remoting error, or logging failure leaves the
outcome unknown; the wait timeout does not bound submission or job cleanup.
The helper is not a continuous monitor or a pilot controller.

An internal experimental guardian launcher creates a heartbeat and starts an
already-staged guardian using the session-owned process helper. A successful
start does not prove that the guardian initialized or that its child started.
An uncertain start must not be retried as if no process was launched; the
launcher neither stages or verifies files nor implements coordinated recovery.

The internal pilot command resolver produces the documented finite-pilot
argument vectors and artifact paths without starting traffic. It does not
create or reserve output directories, verify targets, or authorize a run.
An internal helper can reserve a new guest run directory exclusively; it does
not validate the parent directory's access controls or authorize a run.

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
