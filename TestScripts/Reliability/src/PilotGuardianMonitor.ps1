# Observes already-started guardians; monitoring loss stops their heartbeats.

function Invoke-CtsPilotGuardianMonitor {
    param(
        [Parameter(Mandatory)]$Server,
        [Parameter(Mandatory)]$Client,
        [Parameter(Mandatory)][ValidateRange(1, 60)][int]$PollSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    $states = @(
        @{ Role = 'Server'; Endpoint = $Server; Completion = $null; Evidence = $null; Done = $false; Error = $null }
        @{ Role = 'Client'; Endpoint = $Client; Completion = $null; Evidence = $null; Done = $false; Error = $null }
    )
    foreach ($state in $states) {
        $endpoint = $state.Endpoint
        $directory = Get-CtsProp $endpoint 'Directory'
        $guest = Get-CtsProp $endpoint 'GuestSession'
        $launch = Get-CtsProp $endpoint 'Launch'
        $token = Get-CtsProp $launch 'Token'
        $pidValue = Get-CtsProp $launch 'ProcessId'
        if ($directory -isnot [string] -or $null -eq $guest -or $null -eq $launch -or
            $token -isnot [string] -or $token -cnotmatch '^[0-9a-f]{32}$' -or
            $pidValue -isnot [int] -or $pidValue -le 0 -or
            [string]::IsNullOrWhiteSpace((Get-CtsProp $guest 'Target'))) {
            throw 'Invalid guardian monitor endpoint.'
        }
        Assert-CtsPilotGuardianDirectory -Directory $directory
    }
    if ($Server.Launch.Token -ceq $Client.Launch.Token -or $Server.GuestSession.Target -ieq $Client.GuestSession.Target) {
        throw 'Guardian monitor endpoints must have distinct tokens and targets.'
    }

    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (@($states | Where-Object { -not $_.Done }).Count -gt 0) {
        foreach ($state in $states) {
            if ($state.Done) { continue }
            $endpoint = $state.Endpoint
            try {
                $status = Invoke-CtsPilotProcessAction -GuestSession $endpoint.GuestSession -Action Status `
                    -Token $endpoint.Launch.Token -ExpectedProcessId $endpoint.Launch.ProcessId -TimeoutSeconds $TimeoutSeconds
                if ($status.Exited) {
                    $state.Completion = Invoke-CtsPilotProcessAction -GuestSession $endpoint.GuestSession -Action Complete `
                        -Token $endpoint.Launch.Token -ExpectedProcessId $endpoint.Launch.ProcessId -TimeoutSeconds $TimeoutSeconds
                    $state.Evidence = Get-CtsPilotGuardianResult -GuestSession $endpoint.GuestSession -Directory $endpoint.Directory `
                        -Role $state.Role -Launch $endpoint.Launch -Completion $state.Completion -TimeoutSeconds $TimeoutSeconds
                    if ($state.Evidence.Reason -cne 'Exited') {
                        $state.Error = 'Guardian observation was incomplete.'
                    }
                    $state.Done = $true
                }
            }
            catch { $state.Error = $_.Exception.Message; $state.Done = $true }
        }
        foreach ($state in $states) {
            if ($state.Done) { continue }
            $endpoint = $state.Endpoint
            try {
                $null = Write-CtsPilotHeartbeat -GuestSession $endpoint.GuestSession -Directory $endpoint.Directory `
                    -Action Pulse -TimeoutSeconds $TimeoutSeconds
            }
            catch { $state.Error = $_.Exception.Message; $state.Done = $true }
        }
        if (@($states | Where-Object { -not $_.Done }).Count -gt 0) {
            Start-Sleep -Seconds $PollSeconds
        }
    }
    [pscustomobject]@{
        Outcome = if (@($states | Where-Object { $null -ne $_.Error }).Count -eq 0) { 'Completed' } else { 'Incomplete' }
        ElapsedSeconds = $clock.Elapsed.TotalSeconds
        Endpoints = @($states | ForEach-Object {
            [pscustomobject]@{
                Role = $_.Role; Launch = $_.Endpoint.Launch; Completion = $_.Completion
                Evidence = $_.Evidence; Error = $_.Error
            }
        })
    }
}
