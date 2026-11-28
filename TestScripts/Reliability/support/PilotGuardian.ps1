# Dedicated guardian process; only loss of monitoring stops a running child.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$SpecPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$directory = Split-Path -Parent $SpecPath
$spec = Get-Content -LiteralPath $SpecPath -Raw | ConvertFrom-Json
if ($directory -notmatch '^[Cc]:\\[^\\]' -or $directory -match '[\s"<>|?*\x00-\x1f]' -or
    $directory.Substring(2) -match ':' -or
    @($directory.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
    throw 'Guardian requires a non-root C: run directory without whitespace or dot segments.'
}
if ($spec.role -notin 'server', 'client' -or $spec.executable -notmatch '^[A-Za-z]:\\' -or
    $spec.executable -match '[\s"\x00]' -or
    ($spec.minimumFreeMemoryBytes -isnot [long] -and $spec.minimumFreeMemoryBytes -isnot [int]) -or
    ($spec.minimumFreeDiskBytes -isnot [long] -and $spec.minimumFreeDiskBytes -isnot [int]) -or
    $spec.minimumFreeMemoryBytes -le 0 -or $spec.minimumFreeDiskBytes -le 0 -or
    $spec.heartbeatTimeoutSeconds -isnot [int] -or $spec.heartbeatTimeoutSeconds -lt 2 -or
    $spec.pollMilliseconds -isnot [int] -or $spec.pollMilliseconds -lt 100 -or
    $spec.pollMilliseconds -gt 5000 -or $spec.pollMilliseconds -ge ($spec.heartbeatTimeoutSeconds * 1000)) {
    throw 'Invalid guardian specification.'
}
foreach ($argument in $spec.arguments) {
    if ($argument -isnot [string] -or $argument -eq '' -or $argument -match '[\s"\x00]') {
        throw 'Guardian arguments must be nonempty unquoted tokens without whitespace.'
    }
}

$prefix = "g1b-pilot-$($spec.role)"
$heartbeatPath = Join-Path $directory 'heartbeat.txt'
$stdoutPath = Join-Path $directory "$prefix-stdout.txt"
$stderrPath = Join-Path $directory "$prefix-stderr.txt"
$eventsPath = Join-Path $directory "$prefix-guardian.jsonl"
$resultPath = Join-Path $directory "$prefix-guardian-result.json"
$events = $null
$resultFile = $null
$stdout = $null
$stderr = $null
$process = $null
$processHandle = $null
$processPinned = $false
$stdoutTask = $null
$stderrTask = $null
$reason = 'GuardianError'
$detail = $null
$exitCode = $null
$processId = $null
$heartbeatTicks = [long]0
$heartbeatObservedAgeSeconds = 0.0
$heartbeatAge = [Diagnostics.Stopwatch]::StartNew()
$resourceWarning = $false

function Write-GuardianEvent($Value) {
    $events.WriteLine(($Value | ConvertTo-Json -Compress))
    $events.Flush()
}

function Read-GuardianSample {
    if (-not [IO.File]::Exists($heartbeatPath)) { throw 'MonitorLost: heartbeat is missing.' }
    $ticks = [IO.File]::GetLastWriteTimeUtc($heartbeatPath).Ticks
    if ($script:heartbeatTicks -ne 0 -and
        ($script:heartbeatObservedAgeSeconds + $script:heartbeatAge.Elapsed.TotalSeconds) -ge $spec.heartbeatTimeoutSeconds) {
        throw 'MonitorLost: heartbeat timed out.'
    }
    if ($script:heartbeatTicks -eq 0 -or $ticks -gt $script:heartbeatTicks) {
        $heartbeatUtc = [DateTime]::new($ticks, [DateTimeKind]::Utc)
        $age = ([DateTime]::UtcNow - $heartbeatUtc).TotalSeconds
        if ($age -lt -2 -or $age -ge $spec.heartbeatTimeoutSeconds) {
            throw 'MonitorLost: heartbeat is stale or in the future.'
        }
        $script:heartbeatTicks = $ticks
        $script:heartbeatObservedAgeSeconds = [Math]::Max(0, $age)
        $script:heartbeatAge.Restart()
    }
    elseif ($ticks -lt $script:heartbeatTicks) {
        throw 'MonitorLost: heartbeat timestamp moved backwards.'
    }
    $memory = [CtsReliability.PilotResources]::AvailableMemoryBytes()
    $disk = [CtsReliability.PilotResources]::AvailableDiskBytes([IO.Path]::GetPathRoot($directory))
    Write-GuardianEvent ([ordered]@{
        type = 'sample'; utc = [DateTime]::UtcNow.ToString('o')
        freeMemoryBytes = $memory; freeDiskBytes = $disk
    })
    $below = $memory -lt [uint64]$spec.minimumFreeMemoryBytes -or $disk -lt [uint64]$spec.minimumFreeDiskBytes
    if ($below -and -not $script:resourceWarning) {
        Write-GuardianEvent ([ordered]@{ type = 'warning'; utc = [DateTime]::UtcNow.ToString('o'); detail = 'Resource threshold breached.' })
    }
    $script:resourceWarning = $below
}

try {
    $eventsStream = [IO.File]::Open($eventsPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $events = [IO.StreamWriter]::new($eventsStream, [Text.UTF8Encoding]::new($false))
    $resultFile = [IO.File]::Open($resultPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $stdout = [IO.File]::Open($stdoutPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $stderr = [IO.File]::Open($stderrPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    Add-Type -Path (Join-Path $PSScriptRoot 'PilotSelfJob.cs')
    [CtsReliability.PilotSelfJob]::Enter()
    Read-GuardianSample
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $spec.executable
    $info.Arguments = $spec.arguments -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'Could not start child process.' }
    $processId = $process.Id
    $processHandle = $process.SafeHandle
    $processHandle.DangerousAddRef([ref]$processPinned)
    if (-not $processPinned) { throw 'Could not pin child process handle.' }
    $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
    $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderr)
    Write-GuardianEvent ([ordered]@{ type = 'started'; utc = [DateTime]::UtcNow.ToString('o'); processId = $processId })
    while ($true) {
        $exited = $process.WaitForExit($spec.pollMilliseconds)
        if ($stdoutTask.IsFaulted -or $stderrTask.IsFaulted -or $stdoutTask.IsCanceled -or $stderrTask.IsCanceled) {
            throw 'MonitorLost: output capture failed.'
        }
        Read-GuardianSample
        if ($exited) { $reason = 'Exited'; break }
    }
}
catch {
    $detail = $_.Exception.Message
    if ($detail -like 'MonitorLost:*') { $reason = 'MonitorLost' }
}
finally {
    if ($null -ne $process -and $processPinned) {
        try {
            $process.Refresh()
            if (-not $process.HasExited) { Stop-Process -Id $processId -ErrorAction Stop }
            if (-not $process.WaitForExit(10000)) { throw 'Child did not exit after stop.' }
            $exitCode = $process.ExitCode
        }
        catch { $reason = 'GuardianError'; $detail = $_.Exception.Message }
    }
    $tasks = [Threading.Tasks.Task[]]@(@($stdoutTask, $stderrTask) | Where-Object { $null -ne $_ })
    try {
        if (-not [Threading.Tasks.Task]::WaitAll($tasks, 10000)) { throw 'Output capture did not drain.' }
        if ($null -ne $processId -and $tasks.Count -ne 2) { throw 'Output capture did not start.' }
    }
    catch { $reason = 'GuardianError'; $detail = $_.Exception.Message }
    if ($null -ne $events) {
        try { Write-GuardianEvent ([ordered]@{ type = 'finished'; utc = [DateTime]::UtcNow.ToString('o'); reason = $reason; detail = $detail; exitCode = $exitCode }) }
        catch { $reason = 'GuardianError'; $detail = $_.Exception.Message }
        $events.Dispose()
    }
    if ($null -ne $resultFile) {
        $result = [ordered]@{ reason = $reason; detail = $detail; processId = $processId; exitCode = $exitCode }
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($result | ConvertTo-Json -Compress))
        $resultFile.Write($bytes, 0, $bytes.Length)
        $resultFile.Dispose()
    }
    if ($null -ne $stdout) { $stdout.Dispose() }
    if ($null -ne $stderr) { $stderr.Dispose() }
    # Keep the child PID pinned until this dedicated guardian process exits.
    if ($null -ne $process) { $process.Dispose() }
}

if ($reason -ne 'Exited') { exit 1 }
