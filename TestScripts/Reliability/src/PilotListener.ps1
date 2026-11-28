# Observes a listener without opening a connection or starting traffic.

$script:CtsPilotListenerScript = {
    param($Address, $Port, $ChildProcessId)
    $ErrorActionPreference = 'Stop'
    $connections = @(Get-CimInstance -Namespace 'root\StandardCimv2' -ClassName 'MSFT_NetTCPConnection' `
        -Filter "State = 2 AND LocalPort = $Port" -ErrorAction Stop)
    $matches = @($connections | Where-Object {
        $_.LocalAddress -ceq $Address -and $_.OwningProcess -eq $ChildProcessId
    })
    [pscustomobject]@{
        Address = $Address; Port = $Port; ChildProcessId = $ChildProcessId
        Listening = ($matches.Count -gt 0)
    }
}

function Assert-CtsPilotListenerAddress {
    param([Parameter(Mandatory)][string]$Address)
    $ip = $null
    if (-not [Net.IPAddress]::TryParse($Address, [ref]$ip) -or
        $ip.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork -or
        $Address -notmatch '^\d{1,3}(\.\d{1,3}){3}$' -or
        $Address -cne $ip.ToString() -or $Address -eq '0.0.0.0') {
        throw 'Listener address must be a canonical non-wildcard IPv4 address.'
    }
}

function Get-CtsPilotListenerObservation {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)]$Launch,
        [Parameter(Mandatory)]$Startup,
        [Parameter(Mandatory)][string]$Address,
        [Parameter(Mandatory)][ValidateRange(1, 65535)][int]$Port,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    Assert-CtsPilotListenerAddress -Address $Address
    $token = Get-CtsProp $Launch 'Token'
    $guardianId = Get-CtsProp $Launch 'ProcessId'
    $childId = Get-CtsProp $Startup 'ChildProcessId'
    $prefix = 'g1b-pilot-server-guardian-process'
    if ($token -isnot [string] -or $token -cnotmatch '^[0-9a-f]{32}$' -or
        $guardianId -isnot [int] -or $guardianId -le 0 -or
        (Get-CtsProp $Launch 'HeartbeatPath') -ine [IO.Path]::Combine($Directory, 'heartbeat.txt') -or
        (Get-CtsProp $Launch 'StdoutPath') -ine [IO.Path]::Combine($Directory, "$prefix-stdout.txt") -or
        (Get-CtsProp $Launch 'StderrPath') -ine [IO.Path]::Combine($Directory, "$prefix-stderr.txt") -or
        (Get-CtsProp $Startup 'Outcome') -cne 'ChildStarted' -or
        (Get-CtsProp $Startup 'Token') -cne $token -or
        (Get-CtsProp $Startup 'GuardianProcessId') -ne $guardianId -or
        (Get-CtsProp $Startup 'Directory') -ine $Directory -or
        (Get-CtsProp $Startup 'Role') -ine 'Server' -or
        [string]::IsNullOrWhiteSpace((Get-CtsProp $GuestSession 'Target')) -or
        (Get-CtsProp $Startup 'Target') -ine (Get-CtsProp $GuestSession 'Target') -or
        $childId -isnot [int] -or $childId -le 0) {
        throw 'Listener observation requires matching server launch and child-start receipts.'
    }
    try {
        $null = Write-CtsPilotHeartbeat -GuestSession $GuestSession -Directory $Directory -Action Pulse -TimeoutSeconds $TimeoutSeconds
        $status = Invoke-CtsPilotProcessAction -GuestSession $GuestSession -Action Status -Token $token `
            -ExpectedProcessId $guardianId -TimeoutSeconds $TimeoutSeconds
        if ($status.Exited) { return [pscustomobject]@{ Outcome = 'GuardianExited'; Error = $null } }
        $values = @(Invoke-CtsLogged -Operation 'PilotGuardian:Listener' -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
            $job = $null
            try {
                $job = Invoke-Command -Session $GuestSession.Session -ScriptBlock $script:CtsPilotListenerScript `
                    -ArgumentList @($Address, $Port, $childId) -AsJob -ErrorAction Stop
                if ($null -eq (Wait-Job -Job $job -Timeout $TimeoutSeconds -ErrorAction Stop)) { throw 'Listener observation wait timed out.' }
                if ($job.State -ne 'Completed') { throw "Listener observation job ended in state '$($job.State)'." }
                Receive-Job -Job $job -ErrorAction Stop
            }
            finally { if ($null -ne $job) { Remove-Job -Job $job -Force -ErrorAction SilentlyContinue } }
        })
        if ($values.Count -ne 1 -or (Get-CtsProp $values[0] 'Address') -cne $Address -or
            (Get-CtsProp $values[0] 'Port') -ne $Port -or (Get-CtsProp $values[0] 'ChildProcessId') -ne $childId -or
            (Get-CtsProp $values[0] 'Listening') -isnot [bool]) {
            throw 'Invalid listener observation.'
        }
        $status = Invoke-CtsPilotProcessAction -GuestSession $GuestSession -Action Status -Token $token `
            -ExpectedProcessId $guardianId -TimeoutSeconds $TimeoutSeconds
        if ($status.Exited) { return [pscustomobject]@{ Outcome = 'GuardianExited'; Error = $null } }
        [pscustomobject]@{
            Outcome = if ($values[0].Listening) { 'Listening' } else { 'NotObserved' }
            Address = $Address; Port = $Port; ChildProcessId = $childId; Error = $null
        }
    }
    catch { [pscustomobject]@{ Outcome = 'Unknown'; Error = $_.Exception.Message } }
}

function Wait-CtsPilotServerReadiness {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)]$Launch,
        [Parameter(Mandatory)][string]$Address,
        [Parameter(Mandatory)][ValidateRange(1, 65535)][int]$Port,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$StartupTimeoutSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$ListenerTimeoutSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 60)][int]$PollSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    Assert-CtsPilotListenerAddress -Address $Address
    $startup = Wait-CtsPilotGuardianStartup -GuestSession $GuestSession -Directory $Directory -Role Server -Launch $Launch `
        -StartupTimeoutSeconds $StartupTimeoutSeconds -PollSeconds $PollSeconds -TimeoutSeconds $TimeoutSeconds
    if ($startup.Outcome -ne 'ChildStarted') {
        return [pscustomobject]@{
            Outcome = if ($startup.Outcome -eq 'Timeout') { 'StartupTimeout' } else { $startup.Outcome }
            Startup = $startup; Listener = $null; Error = $startup.Error
        }
    }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        $listener = Get-CtsPilotListenerObservation -GuestSession $GuestSession -Directory $Directory -Launch $Launch `
            -Startup $startup -Address $Address -Port $Port -TimeoutSeconds $TimeoutSeconds
        if ($listener.Outcome -ne 'NotObserved') {
            return [pscustomobject]@{ Outcome = $listener.Outcome; Startup = $startup; Listener = $listener; Error = $listener.Error }
        }
        if ($clock.Elapsed.TotalSeconds -ge $ListenerTimeoutSeconds) { break }
        Start-Sleep -Seconds ([Math]::Min($PollSeconds, [Math]::Max(1, [Math]::Ceiling($ListenerTimeoutSeconds - $clock.Elapsed.TotalSeconds))))
    } while ($true)
    [pscustomobject]@{
        Outcome = 'ListenerTimeout'; Startup = $startup; Listener = $listener
        Error = 'Server listener was not observed.'
    }
}
