<#
.SYNOPSIS
Plans (default) or fills (-Apply) the host's Windows ISO cache used to build the golden image.

.DESCRIPTION
The ISO is cached at <labRoot>\iso\<goldenImage.iso.fileName>, with {arch} replaced by amd64 or arm64 for this host, with a metadata JSON file. A source is needed
only when nothing is cached or -Refresh is given. The source can be a share or local path, or an https URL.
For microsoft.com, copy the time-limited download link from
https://www.microsoft.com/software-download/windows11arm64 and the SHA-256 that Microsoft publishes for it.
The URL query string (its token) is never recorded.

The downloaded file must match -ExpectedSha256 or goldenImage.iso.sha256.<arch> in the lab definition, unless
-AllowUnverified is given. A cached ISO is never replaced without -Refresh. No elevation is needed.

.EXAMPLE
.\TestScripts\Reliability\Get-CtsWindowsIso.ps1 -Source '<download URL>' -ExpectedSha256 <hash>
.\TestScripts\Reliability\Get-CtsWindowsIso.ps1 -Source '<download URL>' -ExpectedSha256 <hash> -Apply
.\TestScripts\Reliability\Get-CtsWindowsIso.ps1 -Source D:\iso\Win11_ARM64.iso -AllowUnverified -Apply
#>
[CmdletBinding()]
param(
    [string]$Source,
    [string]$ExpectedSha256,
    [switch]$AllowUnverified,
    [switch]$Refresh,
    [switch]$Apply,
    [string]$LabDefinitionPath,
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
Get-CtsWindowsIso @arguments
