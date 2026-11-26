<#
.SYNOPSIS
Reports per-tag kernel pool growth between two pool tag CSV captures, largest growth first.
Usage deltas are bytes; outstanding allocation deltas are counts. Accepts both older and unit-labeled CSVs.

.EXAMPLE
.\TestScripts\Reliability\Compare-CtsPoolTagSnapshot.ps1 -Reference pool-before.csv -Difference pool-after.csv -MinimumBytes 65536
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Reference,
    [Parameter(Mandatory)][string]$Difference,
    [int64]$MinimumBytes = 0
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

foreach ($p in $Reference, $Difference) {
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Pool tag capture not found: $p" }
}
Compare-CtsPoolTagSnapshot -Reference $Reference -Difference $Difference -MinimumBytes $MinimumBytes |
    Select-Object Tag,
        @{ Name = 'PagedUsedDeltaBytes'; Expression = { $_.PagedUsedDelta } },
        @{ Name = 'NonPagedUsedDeltaBytes'; Expression = { $_.NonPagedUsedDelta } },
        @{ Name = 'TotalUsedDeltaBytes'; Expression = { $_.TotalUsedDelta } },
        @{ Name = 'OutstandingAllocsDeltaCount'; Expression = { $_.OutstandingAllocsDelta } },
        OnlyIn
