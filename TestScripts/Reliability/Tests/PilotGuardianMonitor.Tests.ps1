#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Server = [pscustomobject]@{
        GuestSession = [pscustomobject]@{ Target = 'A' }; Directory = 'C:\runs\server'
        Launch = [pscustomobject]@{ Token = ('a' * 32); ProcessId = 100 }
    }
    $script:Client = [pscustomobject]@{
        GuestSession = [pscustomobject]@{ Target = 'B' }; Directory = 'C:\runs\client'
        Launch = [pscustomobject]@{ Token = ('b' * 32); ProcessId = 200 }
    }
}

Describe 'non-interrupting two-guardian monitor' {
    BeforeEach {
        $script:Counts = @{ A = 0; B = 0 }
        Mock -ModuleName CtsReliability Start-Sleep { }
        Mock -ModuleName CtsReliability Write-CtsPilotHeartbeat { }
        Mock -ModuleName CtsReliability Get-CtsPilotGuardianResult {
            [pscustomobject]@{ Reason = 'Exited'; ChildExitCode = 7; NormalChildExit = $false }
        }
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            switch ($Action) {
                'Status' {
                    $script:Counts[$GuestSession.Target]++
                    [pscustomobject]@{
                        ProcessId = $ExpectedProcessId
                        Exited = ($GuestSession.Target -eq 'A' -or $script:Counts[$GuestSession.Target] -gt 1)
                    }
                }
                'Complete' { [pscustomobject]@{ Token = $Token; ProcessId = $ExpectedProcessId; ExitCode = 0 } }
                default { throw "Unexpected process action: $Action" }
            }
        }
    }

    It 'continues observing the peer after a nonzero child exit' {
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = Invoke-CtsPilotGuardianMonitor -Server $Server -Client $Client -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Completed'
            $result.Endpoints[0].Evidence.ChildExitCode | Should -Be 7
            $result.Endpoints[1].Evidence.ChildExitCode | Should -Be 7
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 1 -Exactly -ParameterFilter { $GuestSession.Target -eq 'B' }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'continues the peer after a status failure without stopping either process' {
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            if ($Action -eq 'Status' -and $GuestSession.Target -eq 'A') { throw 'session lost' }
            if ($Action -eq 'Status') {
                $script:Counts.B++
                return [pscustomobject]@{ ProcessId = $ExpectedProcessId; Exited = ($script:Counts.B -gt 1) }
            }
            if ($Action -eq 'Complete') { return [pscustomobject]@{ Token = $Token; ProcessId = $ExpectedProcessId; ExitCode = 0 } }
            throw "Unexpected process action: $Action"
        }
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = Invoke-CtsPilotGuardianMonitor -Server $Server -Client $Client -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Incomplete'
            $result.Endpoints[0].Error | Should -Match 'session lost'
            $result.Endpoints[1].Evidence.Reason | Should -Be 'Exited'
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 1 -Exactly -ParameterFilter { $GuestSession.Target -eq 'B' }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'reports monitoring loss without stopping the peer' {
        Mock -ModuleName CtsReliability Get-CtsPilotGuardianResult {
            [pscustomobject]@{ Reason = $(if ($Role -eq 'Server') { 'MonitorLost' } else { 'Exited' }); ChildExitCode = 0 }
        }
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = Invoke-CtsPilotGuardianMonitor -Server $Server -Client $Client -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Incomplete'
            $result.Endpoints[0].Error | Should -Match 'incomplete'
            $result.Endpoints[1].Evidence.Reason | Should -Be 'Exited'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'stops pulsing on pulse failure without directly stopping processes' {
        Mock -ModuleName CtsReliability Write-CtsPilotHeartbeat { throw 'pulse failed' }
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            if ($Action -eq 'Status') { return [pscustomobject]@{ ProcessId = $ExpectedProcessId; Exited = $false } }
            throw "Unexpected process action: $Action"
        }
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = Invoke-CtsPilotGuardianMonitor -Server $Server -Client $Client -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Incomplete'
            @($result.Endpoints | Where-Object { $_.Error -match 'pulse failed' }).Count | Should -Be 2
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }
}
