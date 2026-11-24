<#
.SYNOPSIS
Refreshes the host's cached debugger MSI (cdb, ntsd, Application Verifier) when the share has a newer one.

.DESCRIPTION
Refreshes only when the source share is reachable and its MSI ProductVersion is newer than the cached
version. Never downgrades; an unreachable share keeps the cache. -Architecture defaults to the host's
architecture; pass both values to refresh both caches.

.EXAMPLE
.\TestScripts\Reliability\Update-CtsDebuggerCache.ps1
.\TestScripts\Reliability\Update-CtsDebuggerCache.ps1 -Architecture amd64, arm64
#>
[CmdletBinding()]
param(
    [string]$LabDefinitionPath,
    [ValidateSet('amd64', 'arm64')][string[]]$Architecture,
    [string]$LogPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

$definition = if ($LabDefinitionPath) { Get-CtsLabDefinition -Path $LabDefinitionPath } else { Get-CtsLabDefinition }
if (-not $Architecture) { $Architecture = @(Get-CtsHostArchitecture) }
foreach ($arch in $Architecture) {
    $arguments = @{ Definition = $definition; Architecture = $arch }
    if ($LogPath) { $arguments.LogPath = $LogPath }
    Update-CtsDebuggerCache @arguments
}
