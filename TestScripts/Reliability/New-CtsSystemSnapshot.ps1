<#
.SYNOPSIS
Captures a read-only system snapshot of the Hyper-V host and every lab guest.

.DESCRIPTION
Writes snapshot.json, report.md, and manifest.json into <OutputRoot>\<snapshotId> (default
<labRoot>\snapshots). Guests are reached with the stored guest credential unless -HostOnly is used.
Returns the snapshot path, the snapshot object, and any validation errors.

.EXAMPLE
.\TestScripts\Reliability\New-CtsSystemSnapshot.ps1 -HostOnly
.\TestScripts\Reliability\New-CtsSystemSnapshot.ps1 -Channel WinRM
#>
[CmdletBinding()]
param(
    [string]$LabDefinitionPath,
    [ValidateRange(2, 5)][int]$VmCount,
    [ValidateSet('Direct', 'WinRM')][string]$Channel,
    [string]$OutputRoot,
    [switch]$HostOnly,
    [string]$LogPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

$definition = if ($LabDefinitionPath) { Get-CtsLabDefinition -Path $LabDefinitionPath } else { Get-CtsLabDefinition }
$arguments = @{} + $PSBoundParameters
$arguments.Remove('LabDefinitionPath')
$arguments.Definition = $definition
if (-not $HostOnly) { $arguments.Credential = Get-CtsStoredCredential -Target $definition.guest.credentialTarget }
New-CtsSnapshot @arguments
