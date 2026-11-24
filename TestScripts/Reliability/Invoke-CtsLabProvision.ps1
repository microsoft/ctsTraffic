<#
.SYNOPSIS
Plans (default) or applies (-Apply) lab provisioning: switches, host address, differencing disks, VMs,
guest configuration, debugger install, clean checkpoint, and a post-provision snapshot.

.DESCRIPTION
Without -Apply, returns the plan (Create, Keep, Drift, Collision) and its blocking reasons. Apply requires an
elevated session, an unblocked plan, the golden image, and the stored guest credential. Rerunning continues
from where a failed run stopped.

.EXAMPLE
.\TestScripts\Reliability\Invoke-CtsLabProvision.ps1
.\TestScripts\Reliability\Invoke-CtsLabProvision.ps1 -Apply
#>
[CmdletBinding()]
param(
    [string]$LabDefinitionPath,
    [ValidateRange(2, 5)][int]$VmCount,
    [switch]$Apply,
    [switch]$SkipGuestConfiguration,
    [switch]$SkipSnapshot,
    [int]$GuestReadyTimeoutSeconds
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

$definition = if ($LabDefinitionPath) { Get-CtsLabDefinition -Path $LabDefinitionPath } else { Get-CtsLabDefinition }
$arguments = @{} + $PSBoundParameters
$arguments.Remove('LabDefinitionPath')
$arguments.Definition = $definition
Invoke-CtsLabProvision @arguments
