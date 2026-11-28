# Reads completion evidence; it does not determine the traffic outcome.

$script:CtsPilotGuardianResultScript = {
    param($Path)

    $ErrorActionPreference = 'Stop'
    $file = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($file.Length -le 0 -or $file.Length -gt 65536) { throw 'Guardian result has an invalid length.' }
        $reader = [IO.StreamReader]::new($file, [Text.UTF8Encoding]::new($false, $true), $false)
        try {
            $buffer = [char[]]::new(65537)
            $count = $reader.ReadBlock($buffer, 0, $buffer.Length)
            if ($count -eq 0 -or $count -gt 65536) { throw 'Guardian result has an invalid length.' }
            $content = [string]::new($buffer, 0, $count)
        }
        finally { $reader.Dispose() }
        [pscustomobject]@{ Path = $Path; Content = $content }
    }
    finally { $file.Dispose() }
}

function ConvertFrom-CtsPilotGuardianResult {
    param([Parameter(Mandatory)][string]$Json, [Parameter(Mandatory)][int]$GuardianExitCode)

    if (-not $Json.TrimStart().StartsWith('{')) { throw 'Guardian result must contain one object.' }
    $values = @(ConvertFrom-Json -InputObject $Json -ErrorAction Stop)
    if ($values.Count -ne 1 -or $null -eq $values[0]) { throw 'Guardian result must contain one object.' }
    $result = $values[0]
    foreach ($name in 'reason', 'detail', 'processId', 'exitCode') {
        if (-not (Test-CtsHasProp $result $name)) { throw "Guardian result is missing '$name'." }
    }
    $reason = Get-CtsProp $result 'reason'
    $detail = Get-CtsProp $result 'detail'
    $childId = Get-CtsProp $result 'processId'
    $childExit = Get-CtsProp $result 'exitCode'
    if ($reason -isnot [string] -or $reason -cnotin @('Exited', 'MonitorLost', 'ResourceLimit', 'Watchdog', 'GuardianError')) {
        throw 'Guardian result has an invalid reason.'
    }
    if ($null -ne $detail -and $detail -isnot [string]) { throw 'Guardian result has an invalid detail.' }
    if ($null -ne $childId -and (($childId -isnot [int] -and $childId -isnot [long]) -or $childId -le 0 -or $childId -gt [int]::MaxValue)) {
        throw 'Guardian result has an invalid child process ID.'
    }
    if ($null -ne $childExit -and (($childExit -isnot [int] -and $childExit -isnot [long]) -or
            $childExit -lt [int]::MinValue -or $childExit -gt [int]::MaxValue)) {
        throw 'Guardian result has an invalid child exit code.'
    }
    if (($null -eq $childId -and $null -ne $childExit) -or
        ($reason -ceq 'Exited' -and ($GuardianExitCode -ne 0 -or $null -eq $childId -or $null -eq $childExit)) -or
        ($reason -cne 'Exited' -and $GuardianExitCode -eq 0)) {
        throw 'Guardian result is inconsistent with process completion.'
    }
    [pscustomobject]@{
        Reason = $reason
        Detail = $detail
        ChildProcessId = $childId
        ChildExitCode = $childExit
        NormalChildExit = ($reason -ceq 'Exited' -and $childExit -eq 0)
    }
}

function Get-CtsPilotGuardianResult {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)]$Launch,
        [Parameter(Mandatory)]$Completion,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    $token = Get-CtsProp $Launch 'Token'
    $guardianId = Get-CtsProp $Launch 'ProcessId'
    $completedId = Get-CtsProp $Completion 'ProcessId'
    $exitCode = Get-CtsProp $Completion 'ExitCode'
    $prefix = 'g1b-pilot-' + $Role.ToLowerInvariant() + '-guardian-process'
    if ($token -isnot [string] -or $token -cnotmatch '^[0-9a-f]{32}$' -or
        ($guardianId -isnot [int] -and $guardianId -isnot [long]) -or $guardianId -le 0 -or
        (Get-CtsProp $Completion 'Token') -cne $token -or
        ($completedId -isnot [int] -and $completedId -isnot [long]) -or $completedId -ne $guardianId -or
        $exitCode -isnot [int] -or
        (Get-CtsProp $Launch 'HeartbeatPath') -ine [IO.Path]::Combine($Directory, 'heartbeat.txt') -or
        (Get-CtsProp $Launch 'StdoutPath') -ine [IO.Path]::Combine($Directory, "$prefix-stdout.txt") -or
        (Get-CtsProp $Launch 'StderrPath') -ine [IO.Path]::Combine($Directory, "$prefix-stderr.txt")) {
        throw 'Guardian launch and completion receipts do not match.'
    }

    $resultPath = [IO.Path]::Combine($Directory, ('g1b-pilot-' + $Role.ToLowerInvariant() + '-guardian-result.json'))
    try {
        Invoke-CtsLogged -Operation 'PilotGuardian:Result' -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
            $job = $null
            try {
                $job = Invoke-Command -Session $GuestSession.Session -ScriptBlock $script:CtsPilotGuardianResultScript `
                    -ArgumentList @($resultPath) -AsJob -ErrorAction Stop
                if ($null -eq (Wait-Job -Job $job -Timeout $TimeoutSeconds -ErrorAction Stop)) { throw 'Guardian result wait timed out.' }
                if ($job.State -ne 'Completed') { throw "Guardian result job ended in state '$($job.State)'." }
                $values = @(Receive-Job -Job $job -ErrorAction Stop)
                if ($values.Count -ne 1 -or (Get-CtsProp $values[0] 'Path') -ine $resultPath) {
                    throw 'Guardian result did not return the expected path.'
                }
                $json = Get-CtsProp $values[0] 'Content'
                if ($json -isnot [string] -or $json.Length -eq 0 -or $json.Length -gt 65536) {
                    throw 'Guardian result content is invalid.'
                }
                $parsed = ConvertFrom-CtsPilotGuardianResult -Json $json -GuardianExitCode $exitCode
                [pscustomobject]@{
                    Path = $resultPath
                    GuardianProcessId = $guardianId
                    GuardianExitCode = $exitCode
                    Reason = $parsed.Reason
                    Detail = $parsed.Detail
                    ChildProcessId = $parsed.ChildProcessId
                    ChildExitCode = $parsed.ChildExitCode
                    NormalChildExit = $parsed.NormalChildExit
                    RawJson = $json
                }
            }
            finally {
                if ($null -ne $job) { Remove-Job -Job $job -Force -ErrorAction SilentlyContinue }
            }
        }
    }
    catch {
        throw "Guardian completion evidence is unknown: $($_.Exception.Message)"
    }
}
