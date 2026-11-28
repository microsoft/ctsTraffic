# Session-based heartbeat writes for the standalone pilot guardian.

$script:CtsPilotHeartbeatScript = {
    param($Directory, $Action)

    $ErrorActionPreference = 'Stop'
    if ($Directory -notmatch '^[Cc]:\\' -or $Directory -match '[\s"<>|?*\x00-\x1f]' -or
        $Directory.Substring(2) -match ':' -or
        @($Directory.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
        throw 'Heartbeat directory must be an absolute local C: path without whitespace or dot segments.'
    }
    $path = [IO.Path]::Combine($Directory, 'heartbeat.txt')
    if ($Action -eq 'Create') {
        $file = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $file.Dispose()
    }
    elseif ($Action -eq 'Pulse') {
        if (-not [IO.File]::Exists($path)) { throw 'Heartbeat file is missing.' }
        $previous = [IO.File]::GetLastWriteTimeUtc($path)
        $now = [DateTime]::UtcNow
        if ($now -le $previous) { throw 'Guest clock did not advance past the last heartbeat.' }
        [IO.File]::SetLastWriteTimeUtc($path, $now)
        if ([IO.File]::GetLastWriteTimeUtc($path) -le $previous) { throw 'Heartbeat timestamp did not advance.' }
    }
    else { throw 'Invalid heartbeat action.' }
    [pscustomobject]@{ Path = $path; UpdatedUtc = [IO.File]::GetLastWriteTimeUtc($path).ToString('o') }
}

function Write-CtsPilotHeartbeat {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][ValidateSet('Create', 'Pulse')][string]$Action,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    $operation = "PilotHeartbeat:$Action"
    $heartbeatAction = $Action
    try {
        Invoke-CtsLogged -Operation $operation -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
            $job = $null
            try {
                $job = Invoke-Command -Session $GuestSession.Session -ScriptBlock $script:CtsPilotHeartbeatScript `
                    -ArgumentList @($Directory, $heartbeatAction) -AsJob -ErrorAction Stop
                if ($null -eq (Wait-Job -Job $job -Timeout $TimeoutSeconds -ErrorAction Stop)) { throw 'Heartbeat wait timed out.' }
                if ($job.State -ne 'Completed') { throw "Heartbeat job ended in state '$($job.State)'." }
                $values = @(Receive-Job -Job $job -ErrorAction Stop)
                if ($values.Count -ne 1 -or (Get-CtsProp $values[0] 'Path') -ine [IO.Path]::Combine($Directory, 'heartbeat.txt')) {
                    throw 'Heartbeat did not return the expected path.'
                }
                $time = Get-CtsProp $values[0] 'UpdatedUtc'
                $parsed = [DateTimeOffset]::MinValue
                if ($time -isnot [string] -or $time -notmatch 'Z$' -or -not [DateTimeOffset]::TryParse($time, [ref]$parsed)) {
                    throw 'Heartbeat did not return a valid UTC timestamp.'
                }
                return $values[0]
            }
            finally {
                if ($null -ne $job) { Remove-Job -Job $job -Force -ErrorAction SilentlyContinue }
            }
        }
    }
    catch {
        throw "Heartbeat $Action did not complete reliably; its outcome is unknown: $($_.Exception.Message)"
    }
}
