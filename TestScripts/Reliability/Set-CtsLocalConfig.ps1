<#
.SYNOPSIS
Plans (default) or writes (-Apply) the host-local lab.local.json with the internal debugger share paths.

.DESCRIPTION
The debugger MSIs come from an internal share whose path must never be committed to this public repository.
This script stores the paths in <labRoot>\lab.local.json (C:\ctsReliability\lab.local.json), outside the
repository. Values in that file override lab.v1.json. Other values already in the file are kept.

With no source parameters, it prompts for this host's architecture (amd64 or arm64). No elevation is needed.

.EXAMPLE
.\TestScripts\Reliability\Set-CtsLocalConfig.ps1
.\TestScripts\Reliability\Set-CtsLocalConfig.ps1 -DebuggerSourceArm64 '\\<server>\<path>\dbg_arm64.msi' -Apply
#>
[CmdletBinding()]
param(
    [string]$DebuggerSourceAmd64,
    [string]$DebuggerSourceArm64,
    [string]$LocalPath,
    [switch]$Apply,
    [string]$LabDefinitionPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

$definition = if ($LabDefinitionPath) { Get-CtsLabDefinition -Path $LabDefinitionPath } else { Get-CtsLabDefinition }
$arguments = @{} + $PSBoundParameters
$arguments.Remove('LabDefinitionPath')
if (-not $DebuggerSourceAmd64 -and -not $DebuggerSourceArm64) {
    $arch = Get-CtsHostArchitecture
    $value = Read-Host "Full path to dbg_$arch.msi on the internal debugger share"
    $name = if ($arch -eq 'amd64') { 'DebuggerSourceAmd64' } else { 'DebuggerSourceArm64' }
    $arguments[$name] = $value.Trim().Trim('"')
}
$arguments.Definition = $definition
Set-CtsLocalConfig @arguments
