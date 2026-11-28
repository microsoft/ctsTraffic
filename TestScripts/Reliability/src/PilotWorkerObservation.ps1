# Collects worker events; it does not start, stop, or authorize a workload.

function New-CtsPilotWorkerObservation {
    param(
        [Parameter(Mandatory)]$Job,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][string]$Token
    )
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    if ($Token -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid worker observation token.' }
    [pscustomobject]@{
        Job = $Job; Role = $Role; Directory = $Directory; Token = $Token
        State = 'Starting'; Launch = $null; Startup = $null; Result = $null
        Error = $null; Events = [Collections.Generic.List[object]]::new()
    }
}

function Add-CtsPilotWorkerObservationEvent {
    param([Parameter(Mandatory)]$Observation, [Parameter(Mandatory)]$Event)

    $Observation.Events.Add($Event)
    if ($Observation.State -eq 'Unknown') { return }
    $name = Get-CtsProp $Event 'Event'
    $role = Get-CtsProp $Event 'Role'
    if ($role -cne $Observation.Role) {
        $Observation.State = 'Unknown'; $Observation.Error = 'Worker event role does not match.'
        return
    }
    switch -CaseSensitive ($name) {
        'Launched' {
            $launch = Get-CtsProp $Event 'Launch'
            $pidValue = Get-CtsProp $launch 'ProcessId'
            $prefix = 'g1b-pilot-' + $Observation.Role.ToLowerInvariant() + '-guardian-process'
            if ($Observation.State -cne 'Starting' -or
                (Get-CtsProp $launch 'Token') -cne $Observation.Token -or
                $pidValue -isnot [int] -or $pidValue -le 0 -or
                (Get-CtsProp $launch 'HeartbeatPath') -ine [IO.Path]::Combine($Observation.Directory, 'heartbeat.txt') -or
                (Get-CtsProp $launch 'StdoutPath') -ine [IO.Path]::Combine($Observation.Directory, "$prefix-stdout.txt") -or
                (Get-CtsProp $launch 'StderrPath') -ine [IO.Path]::Combine($Observation.Directory, "$prefix-stderr.txt")) {
                $Observation.State = 'Unknown'; $Observation.Error = 'Invalid or out-of-order worker launch event.'
                return
            }
            $Observation.Launch = $launch
            $Observation.State = 'Launched'
        }
        'Startup' {
            $startup = Get-CtsProp $Event 'Result'
            $outcome = Get-CtsProp $startup 'Outcome'
            $allowed = if ($Observation.Role -eq 'Server') {
                @('Listening', 'ListenerTimeout', 'StartupTimeout', 'GuardianExited', 'Unknown')
            }
            else { @('ChildStarted', 'Timeout', 'GuardianExited', 'Unknown') }
            if ($Observation.State -cne 'Launched' -or $outcome -cnotin $allowed) {
                $Observation.State = 'Unknown'; $Observation.Error = 'Invalid or out-of-order worker startup event.'
                return
            }
            $Observation.Startup = $startup
            if ($outcome -ceq 'Unknown') {
                $Observation.State = 'Unknown'; $Observation.Error = 'Worker startup monitoring is unknown.'
            }
            else { $Observation.State = 'Monitoring' }
        }
        'Finished' {
            $result = Get-CtsProp $Event 'Result'
            if ($Observation.State -cne 'Monitoring' -or
                (Get-CtsProp $result 'Outcome') -cnotin @('Completed', 'Incomplete', 'Unknown')) {
                $Observation.State = 'Unknown'; $Observation.Error = 'Invalid or out-of-order worker completion event.'
                return
            }
            $Observation.Result = $result
            if ($result.Outcome -ceq 'Unknown') {
                $Observation.State = 'Unknown'; $Observation.Error = 'Worker completion monitoring is unknown.'
            }
            else { $Observation.State = 'Finished' }
        }
        'Unknown' {
            $Observation.State = 'Unknown'
            $message = Get-CtsProp $Event 'Error'
            $Observation.Error = if ($message -is [string] -and -not [string]::IsNullOrWhiteSpace($message)) {
                $message
            } else { 'Worker reported an unknown outcome.' }
        }
        default {
            $Observation.State = 'Unknown'; $Observation.Error = 'Unrecognized worker event.'
        }
    }
}

function Update-CtsPilotWorkerObservation {
    param([Parameter(Mandatory)]$Observation)

    $receiveErrors = @()
    try {
        Receive-Job -Job $Observation.Job -ErrorAction SilentlyContinue -ErrorVariable +receiveErrors |
            ForEach-Object { Add-CtsPilotWorkerObservationEvent -Observation $Observation -Event $_ }
        $jobState = $Observation.Job.State
        if ($jobState -notin @('Running', 'NotStarted')) {
            # Once terminal, drain again to include output produced after the first receive.
            Receive-Job -Job $Observation.Job -ErrorAction SilentlyContinue -ErrorVariable +receiveErrors |
                ForEach-Object { Add-CtsPilotWorkerObservationEvent -Observation $Observation -Event $_ }
            if ($jobState -ne 'Completed' -or $Observation.State -ne 'Finished') {
                if ($Observation.State -ne 'Unknown') {
                    $Observation.State = 'Unknown'
                    $Observation.Error = "Worker ended in state '$jobState' without reliable completion."
                }
            }
        }
        if ($receiveErrors.Count -gt 0) {
            $Observation.State = 'Unknown'
            $Observation.Error = "Worker event collection failed: $($receiveErrors[0])"
        }
    }
    catch {
        $Observation.State = 'Unknown'
        $Observation.Error = "Worker event collection failed: $($_.Exception.Message)"
    }
}
