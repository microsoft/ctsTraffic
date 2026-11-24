<#
.SYNOPSIS
Plans (default) or builds (-Apply) the read-only golden VHDX from a Windows ISO.

.DESCRIPTION
Without -Apply, returns the plan and its blocking reasons; nothing changes. With -Apply (elevated), applies
the configured edition to a new GPT VHDX, makes it bootable, writes the metadata JSON, and marks the VHDX
read-only.

.EXAMPLE
.\TestScripts\Reliability\New-CtsGoldenImage.ps1 -IsoPath D:\iso\Win11_ARM64.iso
.\TestScripts\Reliability\New-CtsGoldenImage.ps1 -IsoPath D:\iso\Win11_ARM64.iso -Apply
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$IsoPath,
    [string]$LabDefinitionPath,
    [switch]$Apply,
    [string]$LogPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

$definition = if ($LabDefinitionPath) { Get-CtsLabDefinition -Path $LabDefinitionPath } else { Get-CtsLabDefinition }
$arguments = @{ Definition = $definition; IsoPath = $IsoPath; Apply = $Apply }
if ($LogPath) { $arguments.LogPath = $LogPath }
New-CtsGoldenImage @arguments
