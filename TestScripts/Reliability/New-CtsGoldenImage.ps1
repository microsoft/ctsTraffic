<#
.SYNOPSIS
Plans (default) or builds (-Apply) the read-only golden VHDX from a Windows ISO.

.DESCRIPTION
Without -Apply, returns the plan and its blocking reasons; nothing changes. With -Apply (elevated), applies
the configured edition to a new GPT VHDX, makes it bootable, writes the metadata JSON, and marks the VHDX
read-only.

-IsoPath defaults to the ISO cached by Get-CtsWindowsIso.ps1.

.EXAMPLE
.\TestScripts\Reliability\New-CtsGoldenImage.ps1
.\TestScripts\Reliability\New-CtsGoldenImage.ps1 -IsoPath D:\iso\Win11_ARM64.iso -Apply
#>
[CmdletBinding()]
param(
    [string]$IsoPath,
    [string]$LabDefinitionPath,
    [switch]$Apply,
    [string]$LogPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

$definition = if ($LabDefinitionPath) { Get-CtsLabDefinition -Path $LabDefinitionPath } else { Get-CtsLabDefinition }
if (-not $IsoPath) {
    $cached = Get-CtsIsoCacheEntry -Definition $definition
    if (-not $cached) { throw 'No ISO is cached. Run Get-CtsWindowsIso.ps1 first, or pass -IsoPath.' }
    $IsoPath = $cached.isoPath
}
$arguments = @{ Definition = $definition; IsoPath = $IsoPath; Apply = $Apply }
if ($LogPath) { $arguments.LogPath = $LogPath }
New-CtsGoldenImage @arguments
