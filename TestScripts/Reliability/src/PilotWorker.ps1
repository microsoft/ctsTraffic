# One worker owns one session and guardian; this is not a pilot controller.

function Invoke-CtsPilotEndpointMonitor {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)]$Launch,
        [Parameter(Mandatory)][ValidateRange(1, 60)][int]$PollSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    try {
        while ($true) {
            $status = Invoke-CtsPilotProcessAction -GuestSession $GuestSession -Action Status -Token $Launch.Token `
                -ExpectedProcessId $Launch.ProcessId -TimeoutSeconds $TimeoutSeconds
            if ($status.Exited) {
                $completion = Invoke-CtsPilotProcessAction -GuestSession $GuestSession -Action Complete -Token $Launch.Token `
                    -ExpectedProcessId $Launch.ProcessId -TimeoutSeconds $TimeoutSeconds
                $evidence = Get-CtsPilotGuardianResult -GuestSession $GuestSession -Directory $Directory -Role $Role `
                    -Launch $Launch -Completion $completion -TimeoutSeconds $TimeoutSeconds
                return [pscustomobject]@{
                    Outcome = if ($evidence.Reason -ceq 'Exited') { 'Completed' } else { 'Incomplete' }
                    Completion = $completion; Evidence = $evidence; Error = $null
                }
            }
            $null = Write-CtsPilotHeartbeat -GuestSession $GuestSession -Directory $Directory -Action Pulse -TimeoutSeconds $TimeoutSeconds
            Start-Sleep -Seconds $PollSeconds
        }
    }
    catch { [pscustomobject]@{ Outcome = 'Unknown'; Completion = $null; Evidence = $null; Error = $_.Exception.Message } }
}

function Start-CtsPilotEndpointWorker {
    param(
        [Parameter(Mandatory)][string]$VMName,
        [Parameter(Mandatory)][pscredential]$Credential,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][string]$Token,
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$PowerShellPath,
        [Parameter(Mandatory)][ValidateRange(1, [long]::MaxValue)][long]$MinimumFreeMemoryBytes,
        [Parameter(Mandatory)][ValidateRange(1, [long]::MaxValue)][long]$MinimumFreeDiskBytes,
        [Parameter(Mandatory)][ValidateRange(2, 600)][int]$HeartbeatTimeoutSeconds,
        [Parameter(Mandatory)][ValidateRange(100, 5000)][int]$GuardianPollMilliseconds,
        [Parameter(Mandatory)][ValidateRange(1, 60)][int]$PollSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$StartupTimeoutSeconds,
        [string]$ServerAddress = '',
        [ValidateRange(0, 65535)][int]$ServerPort = 0,
        [string]$LogPath = ''
    )
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    if ([string]::IsNullOrWhiteSpace($VMName) -or $Token -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid worker VM or token.' }
    if ($Role -eq 'Server') {
        Assert-CtsPilotListenerAddress -Address $ServerAddress
        if ($ServerPort -eq 0) { throw 'Server port is required.' }
    }
    if ($GuardianPollMilliseconds -ge ($HeartbeatTimeoutSeconds * 1000) -or
        (5 * $TimeoutSeconds + $PollSeconds) -ge $HeartbeatTimeoutSeconds) {
        throw 'Heartbeat timeout is too short for the configured polling and waits.'
    }
    $modulePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'CtsReliability.psm1'
    $options = @{
        VMName = $VMName; Role = $Role; Directory = $Directory; Token = $Token
        Executable = $Executable; Arguments = $Arguments; PowerShellPath = $PowerShellPath
        MinimumFreeMemoryBytes = $MinimumFreeMemoryBytes; MinimumFreeDiskBytes = $MinimumFreeDiskBytes
        HeartbeatTimeoutSeconds = $HeartbeatTimeoutSeconds; GuardianPollMilliseconds = $GuardianPollMilliseconds
        PollSeconds = $PollSeconds; TimeoutSeconds = $TimeoutSeconds; StartupTimeoutSeconds = $StartupTimeoutSeconds
        ServerAddress = $ServerAddress; ServerPort = $ServerPort; LogPath = $LogPath
    }
    Start-Job -ScriptBlock {
        param($modulePath, $credential, $options)
        $ErrorActionPreference = 'Stop'
        try {
            Import-Module $modulePath -Force -ErrorAction Stop
            $guest = New-CtsGuestSession -VMName $options.VMName -Channel Direct -Credential $credential -LogPath $options.LogPath
            $credential = $null
            try {
                & (Get-Module CtsReliability) {
                    param($guest, $options)
                    $ErrorActionPreference = 'Stop'
                    try {
                        $null = New-CtsPilotGuardianStage -GuestSession $guest -Directory $options.Directory -Role $options.Role `
                            -Executable $options.Executable -Arguments $options.Arguments `
                            -MinimumFreeMemoryBytes $options.MinimumFreeMemoryBytes -MinimumFreeDiskBytes $options.MinimumFreeDiskBytes `
                            -HeartbeatTimeoutSeconds $options.HeartbeatTimeoutSeconds -PollMilliseconds $options.GuardianPollMilliseconds
                        $launch = Start-CtsPilotGuardian -GuestSession $guest -Directory $options.Directory -Role $options.Role `
                            -Token $options.Token -PowerShellPath $options.PowerShellPath -TimeoutSeconds $options.TimeoutSeconds
                        [pscustomobject]@{ Event = 'Launched'; Role = $options.Role; Launch = $launch }
                        if ($options.Role -eq 'Server') {
                            $startup = Wait-CtsPilotServerReadiness -GuestSession $guest -Directory $options.Directory -Launch $launch `
                                -Address $options.ServerAddress -Port $options.ServerPort -StartupTimeoutSeconds $options.StartupTimeoutSeconds `
                                -ListenerTimeoutSeconds $options.StartupTimeoutSeconds -PollSeconds $options.PollSeconds -TimeoutSeconds $options.TimeoutSeconds
                        }
                        else {
                            $startup = Wait-CtsPilotGuardianStartup -GuestSession $guest -Directory $options.Directory -Role Client -Launch $launch `
                                -StartupTimeoutSeconds $options.StartupTimeoutSeconds -PollSeconds $options.PollSeconds -TimeoutSeconds $options.TimeoutSeconds
                        }
                        [pscustomobject]@{ Event = 'Startup'; Role = $options.Role; Result = $startup }
                        if ($startup.Outcome -eq 'Unknown') { throw "Startup monitoring is unknown: $($startup.Error)" }
                        $result = Invoke-CtsPilotEndpointMonitor -GuestSession $guest -Directory $options.Directory -Role $options.Role `
                            -Launch $launch -PollSeconds $options.PollSeconds -TimeoutSeconds $options.TimeoutSeconds
                        [pscustomobject]@{ Event = 'Finished'; Role = $options.Role; Result = $result }
                    }
                    catch {
                        [pscustomobject]@{ Event = 'Unknown'; Role = $options.Role; Error = $_.Exception.Message }
                    }
                } $guest $options
            }
            finally { Remove-PSSession -Session $guest.Session -ErrorAction SilentlyContinue }
        }
        catch { [pscustomobject]@{ Event = 'Unknown'; Role = $options.Role; Error = $_.Exception.Message } }
    } -ArgumentList @($modulePath, $Credential, $options)
}
