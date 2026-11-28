#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Guest = [pscustomobject]@{ Target = 'A' }
    $script:Launch = [pscustomobject]@{ Token = ('a' * 32); ProcessId = 123 }
}

Describe 'independent endpoint monitor' {
    BeforeEach {
        $script:Count = 0
        Mock -ModuleName CtsReliability Start-Sleep { }
        Mock -ModuleName CtsReliability Write-CtsPilotHeartbeat { }
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            if ($Action -eq 'Status') {
                $script:Count++
                return [pscustomobject]@{ Exited = ($script:Count -gt 1) }
            }
            if ($Action -eq 'Complete') { return [pscustomobject]@{ ExitCode = 0 } }
            throw "Unexpected process action: $Action"
        }
        Mock -ModuleName CtsReliability Get-CtsPilotGuardianResult {
            [pscustomobject]@{ Reason = 'Exited'; ChildExitCode = 7 }
        }
    }

    It 'keeps pulsing until completion and retains a nonzero child exit' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Invoke-CtsPilotEndpointMonitor -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client `
                -Launch $Launch -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Completed'
            $result.Evidence.ChildExitCode | Should -Be 7
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 1 -Exactly -ParameterFilter { $Action -eq 'Pulse' }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'reports monitoring loss without directly stopping the child' {
        Mock -ModuleName CtsReliability Write-CtsPilotHeartbeat { throw 'heartbeat failed' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Invoke-CtsPilotEndpointMonitor -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client `
                -Launch $Launch -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Unknown'
            $result.Error | Should -Match 'heartbeat failed'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'reports an unknown status without directly stopping the child' {
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction { throw 'status failed' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Invoke-CtsPilotEndpointMonitor -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client `
                -Launch $Launch -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Unknown'
            $result.Error | Should -Match 'status failed'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'does not classify a guardian monitoring failure as a normal completion' {
        Mock -ModuleName CtsReliability Get-CtsPilotGuardianResult { [pscustomobject]@{ Reason = 'MonitorLost'; ChildExitCode = 1 } }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Invoke-CtsPilotEndpointMonitor -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Server `
                -Launch $Launch -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Incomplete'
            $result.Evidence.Reason | Should -Be 'MonitorLost'
        }
    }
}

Describe 'endpoint worker validation' {
    BeforeEach {
        Mock -ModuleName CtsReliability Start-Job { throw 'Job must not start' }
    }

    It 'rejects a heartbeat timeout too short for the configured waits' {
        InModuleScope CtsReliability {
            $credential = [pscredential]::new('user', (ConvertTo-SecureString 'fixture' -AsPlainText -Force))
            { Start-CtsPilotEndpointWorker -VMName 'A' -Credential $credential -Role Client -Directory 'C:\runs\pilot' `
                -Token ('a' * 32) -Executable 'C:\Windows\powershell.exe' -Arguments @('-NoProfile') `
                -PowerShellPath 'C:\Windows\powershell.exe' -MinimumFreeMemoryBytes 1 -MinimumFreeDiskBytes 1 `
                -HeartbeatTimeoutSeconds 10 -GuardianPollMilliseconds 200 -PollSeconds 1 -TimeoutSeconds 2 `
                -StartupTimeoutSeconds 10 } | Should -Throw '*too short*'
        }
    }

    It 'rejects invalid tokens and server listener addresses before starting a job' {
        InModuleScope CtsReliability {
            $credential = [pscredential]::new('user', (ConvertTo-SecureString 'fixture' -AsPlainText -Force))
            $workerArgs = @{
                VMName = 'A'; Credential = $credential; Role = 'Server'; Directory = 'C:\runs\pilot'
                Token = 'invalid'; Executable = 'C:\Windows\powershell.exe'; Arguments = @('-NoProfile')
                PowerShellPath = 'C:\Windows\powershell.exe'; MinimumFreeMemoryBytes = 1; MinimumFreeDiskBytes = 1
                HeartbeatTimeoutSeconds = 60; GuardianPollMilliseconds = 200; PollSeconds = 1
                TimeoutSeconds = 2; StartupTimeoutSeconds = 10; ServerAddress = '127.0.0.1'; ServerPort = 4444
            }
            { Start-CtsPilotEndpointWorker @workerArgs } | Should -Throw '*token*'
            $workerArgs.Token = 'a' * 32
            $workerArgs.ServerAddress = '0.0.0.0'
            { Start-CtsPilotEndpointWorker @workerArgs } | Should -Throw '*address*'
        }
        Should -Invoke -ModuleName CtsReliability Start-Job -Times 0
    }
}
