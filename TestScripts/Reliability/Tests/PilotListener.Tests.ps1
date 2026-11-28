#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Guest = [pscustomobject]@{ Channel = 'Direct'; Target = 'A'; LogPath = ''; Session = $null }
    $script:Launch = [pscustomobject]@{
        Token = ('a' * 32); ProcessId = 100
        HeartbeatPath = 'C:\runs\pilot\heartbeat.txt'
        StdoutPath = 'C:\runs\pilot\g1b-pilot-server-guardian-process-stdout.txt'
        StderrPath = 'C:\runs\pilot\g1b-pilot-server-guardian-process-stderr.txt'
    }
    $script:Startup = [pscustomobject]@{
        Outcome = 'ChildStarted'; ChildProcessId = 123; Token = ('a' * 32)
        GuardianProcessId = 100; Directory = 'C:\runs\pilot'; Role = 'Server'; Target = 'A'
    }
}

Describe 'listener observation' {
    BeforeEach {
        $script:StatusCount = 0
        Mock -ModuleName CtsReliability Write-CtsPilotHeartbeat { }
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            $script:StatusCount++
            [pscustomobject]@{ Exited = $false }
        }
        Mock -ModuleName CtsReliability Invoke-CtsLogged {
            [pscustomobject]@{ Address = '192.168.1.1'; Port = 4444; ChildProcessId = 123; Listening = $true }
        }
    }

    It 'pulses and checks the tracked guardian on both sides of an observation' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Startup = $script:Startup } {
            param($Guest, $Launch, $Startup)
            $result = Get-CtsPilotListenerObservation -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Startup $Startup -Address '192.168.1.1' -Port 4444 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Listening'
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 1 -Exactly -ParameterFilter { $Action -eq 'Pulse' }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 2 -Exactly -ParameterFilter {
            $Action -eq 'Status' -and $Token -eq ('a' * 32) -and $ExpectedProcessId -eq 100
        }
    }

    It 'does not report a listener when the guardian exited during the observation' {
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            $script:StatusCount++
            [pscustomobject]@{ Exited = ($script:StatusCount -gt 1) }
        }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Startup = $script:Startup } {
            param($Guest, $Launch, $Startup)
            (Get-CtsPilotListenerObservation -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Startup $Startup -Address '192.168.1.1' -Port 4444 -TimeoutSeconds 2).Outcome | Should -Be 'GuardianExited'
        }
    }

    It 'distinguishes no listener from a failed observation' {
        Mock -ModuleName CtsReliability Invoke-CtsLogged {
            [pscustomobject]@{ Address = '192.168.1.1'; Port = 4444; ChildProcessId = 123; Listening = $false }
        }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Startup = $script:Startup } {
            param($Guest, $Launch, $Startup)
            (Get-CtsPilotListenerObservation -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Startup $Startup -Address '192.168.1.1' -Port 4444 -TimeoutSeconds 2).Outcome | Should -Be 'NotObserved'
        }
    }

    It 'returns unknown for a failed or mismatched observation without stopping the child' {
        Mock -ModuleName CtsReliability Invoke-CtsLogged { throw 'query failed' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Startup = $script:Startup } {
            param($Guest, $Launch, $Startup)
            $result = Get-CtsPilotListenerObservation -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Startup $Startup -Address '192.168.1.1' -Port 4444 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Unknown'
            $result.Error | Should -Match 'query failed'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'rejects a response with the wrong owner as unknown' {
        Mock -ModuleName CtsReliability Invoke-CtsLogged {
            [pscustomobject]@{ Address = '192.168.1.1'; Port = 4444; ChildProcessId = 999; Listening = $true }
        }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Startup = $script:Startup } {
            param($Guest, $Launch, $Startup)
            (Get-CtsPilotListenerObservation -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Startup $Startup -Address '192.168.1.1' -Port 4444 -TimeoutSeconds 2).Outcome | Should -Be 'Unknown'
        }
    }

    It 'rejects mismatched receipts and wildcard addresses before pulsing' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Startup = $script:Startup } {
            param($Guest, $Launch, $Startup)
            { Get-CtsPilotListenerObservation -GuestSession $Guest -Directory 'C:\runs\other' -Launch $Launch `
                -Startup $Startup -Address '192.168.1.1' -Port 4444 -TimeoutSeconds 2 } | Should -Throw '*receipts*'
            { Get-CtsPilotListenerObservation -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Startup $Startup -Address '0.0.0.0' -Port 4444 -TimeoutSeconds 2 } | Should -Throw '*address*'
            $other = [pscustomobject]@{ Target = 'B' }
            { Get-CtsPilotListenerObservation -GuestSession $other -Directory 'C:\runs\pilot' -Launch $Launch `
                -Startup $Startup -Address '192.168.1.1' -Port 4444 -TimeoutSeconds 2 } | Should -Throw '*receipts*'
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 0
    }
}

Describe 'listener CIM probe' -Tag 'Live' {
    It 'observes only the owner and address of a local listener' {
        InModuleScope CtsReliability {
            $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
            try {
                $listener.Start()
                $port = $listener.LocalEndpoint.Port
                $result = & $script:CtsPilotListenerScript '127.0.0.1' $port $PID
                $result.Listening | Should -BeTrue
                (& $script:CtsPilotListenerScript '127.0.0.1' $port 0).Listening | Should -BeFalse
                (& $script:CtsPilotListenerScript '127.0.0.2' $port $PID).Listening | Should -BeFalse
            }
            finally { $listener.Stop() }
        }
    }
}

Describe 'server readiness gate' {
    BeforeEach {
        $script:Observations = 0
        Mock -ModuleName CtsReliability Wait-CtsPilotGuardianStartup {
            [pscustomobject]@{ Outcome = 'ChildStarted'; ChildProcessId = 123; Error = $null }
        }
        Mock -ModuleName CtsReliability Get-CtsPilotListenerObservation {
            $script:Observations++
            [pscustomobject]@{
                Outcome = if ($script:Observations -gt 1) { 'Listening' } else { 'NotObserved' }
                Error = $null
            }
        }
    }

    It 'waits for the child-start event and then retries listener observations' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Wait-CtsPilotServerReadiness -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Address '192.168.1.1' -Port 4444 -StartupTimeoutSeconds 10 -ListenerTimeoutSeconds 10 `
                -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Listening'
            $result.Startup.ChildProcessId | Should -Be 123
        }
        Should -Invoke -ModuleName CtsReliability Wait-CtsPilotGuardianStartup -Times 1 -Exactly -ParameterFilter { $Role -eq 'Server' }
        Should -Invoke -ModuleName CtsReliability Get-CtsPilotListenerObservation -Times 2 -Exactly
    }

    It 'does not query the listener when startup is unknown' {
        Mock -ModuleName CtsReliability Wait-CtsPilotGuardianStartup { [pscustomobject]@{ Outcome = 'Unknown'; Error = 'session lost' } }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Wait-CtsPilotServerReadiness -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Address '192.168.1.1' -Port 4444 -StartupTimeoutSeconds 2 -ListenerTimeoutSeconds 2 `
                -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Unknown'
            $result.Error | Should -Be 'session lost'
        }
        Should -Invoke -ModuleName CtsReliability Get-CtsPilotListenerObservation -Times 0
    }

    It 'reports listener timeout without stopping a process' {
        Mock -ModuleName CtsReliability Get-CtsPilotListenerObservation { [pscustomobject]@{ Outcome = 'NotObserved'; Error = $null } }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Wait-CtsPilotServerReadiness -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Address '192.168.1.1' -Port 4444 -StartupTimeoutSeconds 2 -ListenerTimeoutSeconds 1 `
                -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'ListenerTimeout'
        }
    }

    It 'propagates guardian exit during listener observation' {
        Mock -ModuleName CtsReliability Get-CtsPilotListenerObservation { [pscustomobject]@{ Outcome = 'GuardianExited'; Error = $null } }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            (Wait-CtsPilotServerReadiness -GuestSession $Guest -Directory 'C:\runs\pilot' -Launch $Launch `
                -Address '192.168.1.1' -Port 4444 -StartupTimeoutSeconds 2 -ListenerTimeoutSeconds 2 `
                -PollSeconds 1 -TimeoutSeconds 2).Outcome | Should -Be 'GuardianExited'
        }
    }
}
