# CtsReliability lab scripts

PowerShell tooling for the Windows networking reliability lab described in
[documents/reliability](../../documents/reliability/README.md). The lab values
come from [`lab.v1.json`](lab.v1.json) and are explained in
[lab configuration](../../documents/reliability/lab-configuration.md).

Every script that changes the system **plans by default** and changes nothing
until you add `-Apply`.

| Script | Purpose | Needs elevation |
| --- | --- | --- |
| `Set-CtsGuestCredential.ps1` | Prompt for the guest `ctsadmin` password and store it in Windows Credential Manager | No |
| `Update-CtsDebuggerCache.ps1` | Refresh the host's cached debugger MSI when the share has a newer version | No |
| `New-CtsGoldenImage.ps1 -IsoPath <iso> [-Apply]` | Plan or build the read-only golden VHDX | For `-Apply` |
| `Invoke-CtsLabProvision.ps1 [-Apply]` | Plan or create switches, VMs and disks, configure guests, install the debugger, and take the clean checkpoint | For `-Apply` |
| `New-CtsSystemSnapshot.ps1 [-HostOnly]` | Capture a read-only host and guest snapshot | Hyper-V Administrators |
| `Compare-CtsSystemSnapshot.ps1 -Reference <dir> -Difference <dir>` | Report drift between two snapshots | No |
| `Get-CtsPoolTagSnapshot.ps1 [-Path <csv>]` | Capture kernel pool usage per tag | Yes |
| `Compare-CtsPoolTagSnapshot.ps1 -Reference <csv> -Difference <csv>` | Report pool growth per tag | No |

First-time setup order, run from an elevated session:

```powershell
.\TestScripts\Reliability\Set-CtsGuestCredential.ps1
.\TestScripts\Reliability\Update-CtsDebuggerCache.ps1
.\TestScripts\Reliability\New-CtsGoldenImage.ps1 -IsoPath <path-to-Win11-ARM64.iso>          # review the plan
.\TestScripts\Reliability\New-CtsGoldenImage.ps1 -IsoPath <path-to-Win11-ARM64.iso> -Apply
.\TestScripts\Reliability\Invoke-CtsLabProvision.ps1                                         # review the plan
.\TestScripts\Reliability\Invoke-CtsLabProvision.ps1 -Apply
```

The implementation is the `CtsReliability` module: `CtsReliability.psm1`
loads `src\*.ps1`.

## Tests

Every script has Pester tests in `Tests\`. Run them under both PowerShell
editions with:

```powershell
.\TestScripts\Reliability\Tests\Invoke-CtsReliabilityTests.ps1
```

See [reliability tooling tests](../../documents/reliability/tooling-tests.md)
for why the tests exist, what each file proves, tags, recorded results, and
the manual elevated checks still to be done.
