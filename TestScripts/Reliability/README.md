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
