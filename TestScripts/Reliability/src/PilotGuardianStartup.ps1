# Observes a guardian's child-start event, not network readiness.

$script:CtsPilotGuardianStartupScript = {
    param($Path)
    $ErrorActionPreference = 'Stop'
    if (-not [IO.File]::Exists($Path)) {
        return [pscustomobject]@{ Path = $Path; Content = '' }
    }
    $file = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $bytes = [byte[]]::new(65537)
        $count = 0
        while ($count -lt $bytes.Length) {
            $read = $file.Read($bytes, $count, $bytes.Length - $count)
            if ($read -eq 0) { break }
            $count += $read
        }
        if ($count -gt 65536) { throw 'Guardian startup evidence exceeds the read limit.' }
        while ($count -gt 0 -and $bytes[$count - 1] -ne 10) { $count-- }
        $content = [Text.UTF8Encoding]::new($false, $true).GetString($bytes, 0, $count)
        [pscustomobject]@{ Path = $Path; Content = $content }
    }
    finally { $file.Dispose() }
}

function ConvertFrom-CtsPilotGuardianStartup {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Content)

    $lines = $Content -split "`n"
    $started = $null
    for ($i = 0; $i -lt $lines.Count - 1; $i++) {
        $line = $lines[$i].TrimEnd("`r")
        if (-not $line.StartsWith('{')) { throw 'Invalid guardian event.' }
        $values = @(ConvertFrom-Json -InputObject $line -ErrorAction Stop)
        if ($values.Count -ne 1 -or $null -eq $values[0]) { throw 'Invalid guardian event.' }
        $type = Get-CtsProp $values[0] 'type'
        if ($type -ceq 'started') {
            $id = Get-CtsProp $values[0] 'processId'
            if ($null -ne $started -or ($id -isnot [int] -and $id -isnot [long]) -or
                $id -le 0 -or $id -gt [int]::MaxValue) {
                throw 'Invalid guardian child-start event.'
            }
            $started = [int]$id
        }
        elseif ($type -cnotin @('sample', 'warning', 'finished')) { throw 'Invalid guardian event type.' }
    }
    return [pscustomobject]@{ ChildProcessId = $started }
}

function Wait-CtsPilotGuardianStartup {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)]$Launch,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$StartupTimeoutSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 60)][int]$PollSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    $token = Get-CtsProp $Launch 'Token'
    $guardianId = Get-CtsProp $Launch 'ProcessId'
    $prefix = 'g1b-pilot-' + $Role.ToLowerInvariant() + '-guardian-process'
    if ($token -isnot [string] -or $token -cnotmatch '^[0-9a-f]{32}$' -or
        $guardianId -isnot [int] -or $guardianId -le 0 -or
        (Get-CtsProp $Launch 'HeartbeatPath') -ine [IO.Path]::Combine($Directory, 'heartbeat.txt') -or
        (Get-CtsProp $Launch 'StdoutPath') -ine [IO.Path]::Combine($Directory, "$prefix-stdout.txt") -or
        (Get-CtsProp $Launch 'StderrPath') -ine [IO.Path]::Combine($Directory, "$prefix-stderr.txt")) {
        throw 'Invalid guardian launch receipt.'
    }
    $path = [IO.Path]::Combine($Directory, ('g1b-pilot-' + $Role.ToLowerInvariant() + '-guardian.jsonl'))
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        do {
            $null = Write-CtsPilotHeartbeat -GuestSession $GuestSession -Directory $Directory -Action Pulse -TimeoutSeconds $TimeoutSeconds
            $status = Invoke-CtsPilotProcessAction -GuestSession $GuestSession -Action Status -Token $token `
                -ExpectedProcessId $guardianId -TimeoutSeconds $TimeoutSeconds
            if ($status.Exited) {
                return [pscustomobject]@{ Outcome = 'GuardianExited'; ChildProcessId = $null; Error = $null }
            }
            $values = @(Invoke-CtsLogged -Operation 'PilotGuardian:Startup' -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
                $job = $null
                try {
                    $job = Invoke-Command -Session $GuestSession.Session -ScriptBlock $script:CtsPilotGuardianStartupScript `
                        -ArgumentList @($path) -AsJob -ErrorAction Stop
                    if ($null -eq (Wait-Job -Job $job -Timeout $TimeoutSeconds -ErrorAction Stop)) { throw 'Startup evidence wait timed out.' }
                    if ($job.State -ne 'Completed') { throw "Startup evidence job ended in state '$($job.State)'." }
                    Receive-Job -Job $job -ErrorAction Stop
                }
                finally { if ($null -ne $job) { Remove-Job -Job $job -Force -ErrorAction SilentlyContinue } }
            })
            if ($values.Count -ne 1 -or (Get-CtsProp $values[0] 'Path') -ine $path) { throw 'Invalid startup evidence path.' }
            $content = Get-CtsProp $values[0] 'Content'
            if ($content -isnot [string] -or $content.Length -gt 65536) { throw 'Invalid startup evidence content.' }
            $event = ConvertFrom-CtsPilotGuardianStartup -Content $content
            if ($null -ne $event.ChildProcessId) {
                $status = Invoke-CtsPilotProcessAction -GuestSession $GuestSession -Action Status -Token $token `
                    -ExpectedProcessId $guardianId -TimeoutSeconds $TimeoutSeconds
                if ($status.Exited) {
                    return [pscustomobject]@{ Outcome = 'GuardianExited'; ChildProcessId = $event.ChildProcessId; Error = $null }
                }
                return [pscustomobject]@{
                    Outcome = 'ChildStarted'; ChildProcessId = $event.ChildProcessId; Error = $null
                    Token = $token; GuardianProcessId = $guardianId; Directory = $Directory; Role = $Role
                    Target = $GuestSession.Target
                }
            }
            if ($clock.Elapsed.TotalSeconds -ge $StartupTimeoutSeconds) { break }
            Start-Sleep -Seconds ([Math]::Min($PollSeconds, [Math]::Max(1, [Math]::Ceiling($StartupTimeoutSeconds - $clock.Elapsed.TotalSeconds))))
        } while ($true)
        [pscustomobject]@{ Outcome = 'Timeout'; ChildProcessId = $null; Error = 'Child startup was not observed.' }
    }
    catch {
        [pscustomobject]@{ Outcome = 'Unknown'; ChildProcessId = $null; Error = $_.Exception.Message }
    }
}
