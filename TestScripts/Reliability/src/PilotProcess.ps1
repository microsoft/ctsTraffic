# Session-local process tracking for a future pilot controller.
# These helpers do not implement preflight, monitoring, or a run workflow.

$script:CtsPilotProcessScript = {
    param($Action, $Token, $FilePath, $Arguments, $StdoutPath, $StderrPath, $WaitMilliseconds)

    $ErrorActionPreference = 'Stop'
    if ($Token -notmatch '^[0-9a-f]{32}$') { throw 'Invalid process token.' }
    $registry = Get-Variable -Name CtsReliabilityPilotProcesses -Scope Global -ValueOnly -ErrorAction SilentlyContinue
    if ($null -eq $registry) {
        $registry = @{}
        Set-Variable -Name CtsReliabilityPilotProcesses -Scope Global -Value $registry
    }
    if ($registry -isnot [hashtable]) { throw 'Invalid pilot process registry.' }

    if ($Action -eq 'Start') {
        if ($registry.ContainsKey($Token)) { throw 'Process token already exists.' }
        if ($FilePath -notmatch '^[A-Za-z]:\\' -or
            $StdoutPath -notmatch '^[A-Za-z]:\\' -or
            $StderrPath -notmatch '^[A-Za-z]:\\' -or
            $StdoutPath -ieq $StderrPath) {
            throw 'Executable and distinct output paths must be absolute.'
        }
        foreach ($argument in $Arguments) {
            if ($argument -isnot [string] -or $argument -eq '' -or $argument -match '[\s"\x00]') {
                throw 'Process arguments must be nonempty unquoted tokens without whitespace.'
            }
        }
        $stdout = $null
        $stderr = $null
        $process = $null
        $registered = $false
        try {
            $stdout = [System.IO.File]::Open($StdoutPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
            $stderr = [System.IO.File]::Open($StderrPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
            $info = [System.Diagnostics.ProcessStartInfo]::new()
            $info.FileName = $FilePath
            $info.Arguments = $Arguments -join ' '
            $info.UseShellExecute = $false
            $info.CreateNoWindow = $true
            $info.RedirectStandardOutput = $true
            $info.RedirectStandardError = $true
            $process = [System.Diagnostics.Process]::new()
            $process.StartInfo = $info
            if (-not $process.Start()) { throw 'Process failed to start.' }
            $record = @{
                Process = $process
                Stdout = $stdout
                Stderr = $stderr
                StdoutTask = $null
                StderrTask = $null
            }
            $registry[$Token] = $record
            $registered = $true
            $record.StdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
            $record.StderrTask = $process.StandardError.BaseStream.CopyToAsync($stderr)
            return [pscustomobject]@{ Token = $Token; ProcessId = $process.Id; StartedUtc = $process.StartTime.ToUniversalTime().ToString('o') }
        }
        finally {
            if (-not $registered) {
                if ($null -ne $process) { $process.Dispose() }
                if ($null -ne $stderr) { $stderr.Dispose() }
                if ($null -ne $stdout) { $stdout.Dispose() }
            }
        }
    }

    if (-not $registry.ContainsKey($Token)) { throw 'Unknown process token in this session.' }
    $record = $registry[$Token]
    $process = $record.Process
    switch ($Action) {
        'Status' {
            $process.Refresh()
            return [pscustomobject]@{
                Token = $Token
                ProcessId = $process.Id
                Exited = $process.HasExited
                ExitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
            }
        }
        'Complete' {
            if (-not $process.WaitForExit($WaitMilliseconds)) { throw 'Tracked process has not exited.' }
            $tasks = [System.Threading.Tasks.Task[]]@(@($record.StdoutTask, $record.StderrTask) | Where-Object { $null -ne $_ })
            $drained = $false
            try {
                $drained = [System.Threading.Tasks.Task]::WaitAll($tasks, $WaitMilliseconds)
                if (-not $drained) { throw 'Output capture did not finish within the timeout.' }
                if ($tasks.Count -ne 2) { throw 'Output capture did not start.' }
                return [pscustomobject]@{ Token = $Token; ProcessId = $process.Id; ExitCode = $process.ExitCode }
            }
            finally {
                if ($drained -or @($tasks | Where-Object { -not $_.IsCompleted }).Count -eq 0) {
                    $record.Stdout.Dispose()
                    $record.Stderr.Dispose()
                    $process.Dispose()
                    $registry.Remove($Token)
                }
            }
        }
        default { throw "Unsupported process action '$Action'." }
    }
}

function Invoke-CtsPilotProcessAction {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][ValidateSet('Start', 'Status', 'Complete')][string]$Action,
        [Parameter(Mandatory)][string]$Token,
        [string]$FilePath = '',
        [string[]]$Arguments = @(),
        [string]$StdoutPath = '',
        [string]$StderrPath = '',
        [ValidateRange(1, 60000)][int]$WaitMilliseconds = 5000
    )
    Invoke-CtsGuestCommand -GuestSession $GuestSession -ScriptBlock $script:CtsPilotProcessScript `
        -ArgumentList @($Action, $Token, $FilePath, $Arguments, $StdoutPath, $StderrPath, $WaitMilliseconds) `
        -Operation "PilotProcess:$Action"
}
