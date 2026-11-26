<#
.SYNOPSIS
Captures kernel pool usage per tag on this machine (a poolsnap) and optionally writes it to CSV.

.DESCRIPTION
Requires an elevated session. Run inside a guest, or on the host. Use Compare-CtsPoolTagSnapshot.ps1 to
compare two captures. CSV usage columns are bytes; allocation and free columns are counts.

.EXAMPLE
.\TestScripts\Reliability\Get-CtsPoolTagSnapshot.ps1 -Path C:\ctsReliability\pool-before.csv
#>
[CmdletBinding()]
param([string]$Path)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

$arguments = @{}
if ($Path) { $arguments.Path = $Path }
Get-CtsPoolTagSnapshot @arguments
