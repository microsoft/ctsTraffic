<#
.SYNOPSIS
Runs the CtsReliability Pester tests under PowerShell 7 and Windows PowerShell 5.1 and records the results.

.DESCRIPTION
Each edition runs in a clean child process (-NoProfile) so module state and mocks cannot leak between runs.
Writes, per edition, an NUnit XML file and a JSON summary, plus summary.md for the whole run, into
-OutputDirectory\<utc>. Returns exit code 1 when any test fails or an edition cannot run.

Tags:
  Live      reads the real local machine (CIM, Credential Manager, Hyper-V module); included by default.
  Elevated  needs an elevated session; these tests skip themselves when not elevated.
  Share     reads the real debugger MSI from the internal share set in lab.local.json; excluded unless -IncludeShare.

.EXAMPLE
.\TestScripts\Reliability\Tests\Invoke-CtsReliabilityTests.ps1
.\TestScripts\Reliability\Tests\Invoke-CtsReliabilityTests.ps1 -IncludeShare -Edition pwsh
#>
[CmdletBinding()]
param(
    [ValidateSet('pwsh', 'powershell')][string[]]$Edition = @('pwsh', 'powershell'),
    [switch]$IncludeShare,
    [switch]$ExcludeLive,
    [string[]]$Path = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.Tests.ps1' | Sort-Object Name | ForEach-Object FullName),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'results')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$runDirectory = Join-Path $OutputDirectory $runId
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
$exclude = @()
if (-not $IncludeShare) { $exclude += 'Share' }
if ($ExcludeLive) { $exclude += 'Live' }
$isElevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

function ConvertTo-PsLiteral([string[]]$Values) { '@(' + (($Values | ForEach-Object { "'" + ($_ -replace "'", "''") + "'" }) -join ',') + ')' }

$summaries = foreach ($exe in $Edition) {
    $xml = Join-Path $runDirectory "$exe.nunit.xml"
    $json = Join-Path $runDirectory "$exe.summary.json"
    $child = @"
`$ErrorActionPreference = 'Stop'
Import-Module Pester -MinimumVersion 5.0
`$c = New-PesterConfiguration
`$c.Run.Path = $(ConvertTo-PsLiteral $Path)
`$c.Run.PassThru = `$true
`$c.Filter.ExcludeTag = $(if ($exclude) { ConvertTo-PsLiteral $exclude } else { '@()' })
`$c.Output.Verbosity = 'Normal'
`$c.TestResult.Enabled = `$true
`$c.TestResult.OutputFormat = 'NUnitXml'
`$c.TestResult.OutputPath = '$($xml -replace "'", "''")'
`$r = Invoke-Pester -Configuration `$c
[ordered]@{
    edition = '$exe'
    psVersion = `$PSVersionTable.PSVersion.ToString()
    pesterVersion = (Get-Module Pester).Version.ToString()
    passed = `$r.PassedCount; failed = `$r.FailedCount; skipped = `$r.SkippedCount; notRun = `$r.NotRunCount
    durationSeconds = [math]::Round(`$r.Duration.TotalSeconds, 1)
    failedTests = @(`$r.Failed | ForEach-Object { `$_.ExpandedPath }) + @(
        if (`$r.Result -ne 'Passed') {
            'Pester run result: {0}; failed containers: {1}; failed blocks: {2}' -f `$r.Result, `$r.FailedContainersCount, `$r.FailedBlocksCount
        }
    )
    files = @(`$r.Tests | Group-Object { Split-Path `$_.ScriptBlock.File -Leaf } | Sort-Object Name | ForEach-Object {
        [ordered]@{ file = `$_.Name; total = `$_.Count
            passed = @(`$_.Group | Where-Object Result -eq 'Passed').Count
            failed = @(`$_.Group | Where-Object Result -eq 'Failed').Count
            skipped = @(`$_.Group | Where-Object Result -eq 'Skipped').Count
            notRun = @(`$_.Group | Where-Object Result -eq 'NotRun').Count }
    })
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath '$($json -replace "'", "''")' -Encoding UTF8
"@
    Write-Host "=== $exe ===" -ForegroundColor Cyan
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) {
        Write-Warning "$exe was not found; skipping."
        [pscustomobject]@{ edition = $exe; psVersion = 'n/a'; pesterVersion = 'n/a'; passed = 0; failed = 0; skipped = 0; notRun = 0; durationSeconds = 0; failedTests = @("$exe not found"); files = @() }
        continue
    }
    & $exe -NoProfile -NonInteractive -Command $child | Out-Host
    if (Test-Path -LiteralPath $json) { Get-Content -LiteralPath $json -Raw | ConvertFrom-Json }
    else { [pscustomobject]@{ edition = $exe; psVersion = 'n/a'; pesterVersion = 'n/a'; passed = 0; failed = 0; skipped = 0; notRun = 0; durationSeconds = 0; failedTests = @("$exe run produced no summary (exit $LASTEXITCODE)"); files = @() } }
}

$md = [System.Text.StringBuilder]::new()
$null = $md.AppendLine("# CtsReliability test run $runId").AppendLine()
$null = $md.AppendLine("- Host: $env:COMPUTERNAME ($env:PROCESSOR_ARCHITECTURE), elevated: $isElevated")
$null = $md.AppendLine("- Excluded tags: $(if ($exclude) { $exclude -join ', ' } else { 'none' })").AppendLine()
$null = $md.AppendLine('| Edition | PowerShell | Pester | Passed | Failed | Skipped | NotRun | Seconds |')
$null = $md.AppendLine('| --- | --- | --- | --- | --- | --- | --- | --- |')
foreach ($s in $summaries) {
    $null = $md.AppendLine("| $($s.edition) | $($s.psVersion) | $($s.pesterVersion) | $($s.passed) | $($s.failed) | $($s.skipped) | $($s.notRun) | $($s.durationSeconds) |")
}
foreach ($s in $summaries) {
    if (@($s.files).Count -eq 0) { continue }
    $null = $md.AppendLine().AppendLine("## Per file: $($s.edition)").AppendLine()
    $null = $md.AppendLine('| File | Total | Passed | Failed | Skipped | NotRun |').AppendLine('| --- | --- | --- | --- | --- | --- |')
    foreach ($f in $s.files) { $null = $md.AppendLine("| $($f.file) | $($f.total) | $($f.passed) | $($f.failed) | $($f.skipped) | $($f.notRun) |") }
}
foreach ($s in $summaries) {
    if (@($s.failedTests).Count -gt 0) {
        $null = $md.AppendLine().AppendLine("## Failures: $($s.edition)").AppendLine()
        foreach ($t in $s.failedTests) { $null = $md.AppendLine("- $t") }
    }
}
$summaryPath = Join-Path $runDirectory 'summary.md'
[System.IO.File]::WriteAllText($summaryPath, $md.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host $md.ToString()
Write-Host "Results: $runDirectory"

$failed = @($summaries | Where-Object { $_.failed -gt 0 -or @($_.failedTests).Count -gt 0 }).Count -gt 0
exit $(if ($failed) { 1 } else { 0 })
