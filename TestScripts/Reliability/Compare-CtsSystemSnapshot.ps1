<#
.SYNOPSIS
Reports configuration drift between two system snapshot directories.

.DESCRIPTION
Each path is a snapshot directory (containing snapshot.json and manifest.json) or a snapshot.json file.
Both snapshots are validated first; validation errors (including manifest hash mismatches) stop the
comparison. Volatile values are ignored by default; -IgnorePath adds more wildcard patterns.

.EXAMPLE
.\TestScripts\Reliability\Compare-CtsSystemSnapshot.ps1 -Reference C:\ctsReliability\snapshots\snap-A -Difference C:\ctsReliability\snapshots\snap-B
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Reference,
    [Parameter(Mandatory)][string]$Difference,
    [string[]]$IgnorePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

function Read-SnapshotDirectory([string]$Path) {
    $item = Get-Item -LiteralPath $Path
    $directory = if ($item.PSIsContainer) { $item.FullName } else { $item.DirectoryName }
    $errors = @(Test-CtsSnapshot -Path $directory)
    if ($errors.Count -gt 0) { throw "Snapshot '$directory' is not valid: $($errors -join '; ')" }
    Get-Content -LiteralPath (Join-Path $directory 'snapshot.json') -Raw | ConvertFrom-Json
}

$arguments = @{ Reference = (Read-SnapshotDirectory $Reference); Difference = (Read-SnapshotDirectory $Difference) }
if ($PSBoundParameters.ContainsKey('IgnorePath')) {
    $arguments.IgnorePath = @(& (Get-Module CtsReliability) { $script:CtsDefaultDriftIgnore }) + $IgnorePath
}
Compare-CtsSnapshot @arguments
