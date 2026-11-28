#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Guest = [pscustomobject]@{ Session = (New-MockObject -Type ([System.Management.Automation.Runspaces.PSSession])); Channel = 'Direct'; Target = 'vm'; LogPath = '' }
    $script:Token = 'a' * 32
    $script:Launch = [pscustomobject]@{
        Token = $script:Token; ProcessId = 123
        HeartbeatPath = 'C:\runs\pilot\heartbeat.txt'
        StdoutPath = 'C:\runs\pilot\g1b-pilot-client-guardian-process-stdout.txt'
        StderrPath = 'C:\runs\pilot\g1b-pilot-client-guardian-process-stderr.txt'
    }
    $script:Completion = [pscustomobject]@{ Token = $script:Token; ProcessId = 123; ExitCode = 0 }
}

Describe 'guardian result content' {
    It 'distinguishes a normal child exit from a nonzero child exit' {
        InModuleScope CtsReliability {
            $ok = ConvertFrom-CtsPilotGuardianResult -Json '{"reason":"Exited","detail":null,"processId":42,"exitCode":0}' -GuardianExitCode 0
            $ok.NormalChildExit | Should -BeTrue
            $failed = ConvertFrom-CtsPilotGuardianResult -Json '{"reason":"Exited","detail":null,"processId":42,"exitCode":7}' -GuardianExitCode 0
            $failed.NormalChildExit | Should -BeFalse
            $failed.ChildExitCode | Should -Be 7
        }
    }

    It 'retains a failed monitoring outcome without claiming the child exited' {
        InModuleScope CtsReliability {
            $result = ConvertFrom-CtsPilotGuardianResult -Json '{"reason":"MonitorLost","detail":"timeout","processId":42,"exitCode":null}' -GuardianExitCode 1
            $result.Reason | Should -Be 'MonitorLost'
            $result.ChildExitCode | Should -BeNullOrEmpty
            $result.NormalChildExit | Should -BeFalse
        }
    }

    It 'rejects malformed, incomplete, or contradictory evidence' {
        InModuleScope CtsReliability {
            foreach ($json in @(
                '[{"reason":"Exited","detail":null,"processId":42,"exitCode":0}]',
                '{"reason":"Exited","detail":null,"processId":42}',
                '{"reason":"unknown","detail":null,"processId":42,"exitCode":0}',
                '{"reason":"Exited","detail":null,"processId":42,"exitCode":null}',
                '{"reason":"MonitorLost","detail":null,"processId":null,"exitCode":0}',
                '{"reason":"Exited","detail":null,"processId":0,"exitCode":0}',
                '{"reason":"Exited","detail":null,"processId":42,"exitCode":"0"}',
                '{"reason":"Exited","detail":null,"processId":42,"exitCode":9999999999}',
                '{"reason":"Exited"'
            )) {
                { ConvertFrom-CtsPilotGuardianResult -Json $json -GuardianExitCode 0 } | Should -Throw
            }
            { ConvertFrom-CtsPilotGuardianResult -Json '{"reason":"Exited","detail":null,"processId":42,"exitCode":0}' -GuardianExitCode 1 } | Should -Throw '*inconsistent*'
            { ConvertFrom-CtsPilotGuardianResult -Json '{"reason":"Watchdog","detail":null,"processId":42,"exitCode":-1}' -GuardianExitCode 0 } | Should -Throw '*inconsistent*'
        }
    }

    It 'reads only nonempty bounded result files' {
        $path = Join-Path $TestDrive 'result.json'
        InModuleScope CtsReliability -Parameters @{ Path = $path } {
            param($Path)
            { & $script:CtsPilotGuardianResultScript $Path } | Should -Throw
            [IO.File]::WriteAllText($Path, '')
            { & $script:CtsPilotGuardianResultScript $Path } | Should -Throw '*length*'
            [IO.File]::WriteAllText($Path, ('x' * 65537))
            { & $script:CtsPilotGuardianResultScript $Path } | Should -Throw '*length*'
            [IO.File]::WriteAllText($Path, '{"reason":"Exited"}')
            (& $script:CtsPilotGuardianResultScript $Path).Content | Should -Be '{"reason":"Exited"}'
        }
    }
}

Describe 'tracked guardian completion evidence' {
    BeforeAll {
        $script:CompletedJob = Start-Job -ScriptBlock { }
        $null = Wait-Job -Job $script:CompletedJob
    }

    AfterAll { Remove-Job -Job $script:CompletedJob -Force -ErrorAction SilentlyContinue }

    BeforeEach {
        Mock -ModuleName CtsReliability Invoke-Command { $script:CompletedJob }
        Mock -ModuleName CtsReliability Wait-Job { $script:CompletedJob }
        Mock -ModuleName CtsReliability Remove-Job { }
        Mock -ModuleName CtsReliability Receive-Job {
            [pscustomobject]@{
                Path = 'C:\runs\pilot\g1b-pilot-client-guardian-result.json'
                Content = '{"reason":"Exited","detail":null,"processId":42,"exitCode":0}'
            }
        }
    }

    It 'returns validated evidence for matching launch and completion receipts' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Completion = $script:Completion } {
            param($Guest, $Launch, $Completion)
            $result = Get-CtsPilotGuardianResult -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client `
                -Launch $Launch -Completion $Completion -TimeoutSeconds 10
            $result.GuardianProcessId | Should -Be 123
            $result.ChildProcessId | Should -Be 42
            $result.NormalChildExit | Should -BeTrue
            $result.RawJson | Should -Match '"reason":"Exited"'
        }
        Should -Invoke -ModuleName CtsReliability Remove-Job -Times 1 -Exactly
    }

    It 'rejects mismatched receipts before reading files' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $completion = [pscustomobject]@{ Token = ('b' * 32); ProcessId = 123; ExitCode = 0 }
            { Get-CtsPilotGuardianResult -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client `
                -Launch $Launch -Completion $completion -TimeoutSeconds 1 } | Should -Throw '*receipts do not match*'
            $completion.Token = $Launch.Token
            { Get-CtsPilotGuardianResult -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Server `
                -Launch $Launch -Completion $completion -TimeoutSeconds 1 } | Should -Throw '*receipts do not match*'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-Command -Times 0
    }

    It 'reports timeout, bad content, and logging failures as unknown evidence' {
        Mock -ModuleName CtsReliability Wait-Job { }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Completion = $script:Completion } {
            param($Guest, $Launch, $Completion)
            { Get-CtsPilotGuardianResult -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client `
                -Launch $Launch -Completion $Completion -TimeoutSeconds 1 } | Should -Throw '*evidence is unknown*'
        }
        Mock -ModuleName CtsReliability Wait-Job { $script:CompletedJob }
        Mock -ModuleName CtsReliability Receive-Job {
            [pscustomobject]@{ Path = 'C:\runs\pilot\g1b-pilot-client-guardian-result.json'; Content = '{"reason":"Exited"}' }
        }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Completion = $script:Completion } {
            param($Guest, $Launch, $Completion)
            { Get-CtsPilotGuardianResult -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client `
                -Launch $Launch -Completion $Completion -TimeoutSeconds 1 } | Should -Throw '*evidence is unknown*'
        }
        Mock -ModuleName CtsReliability Receive-Job {
            [pscustomobject]@{ Path = 'C:\runs\pilot\g1b-pilot-client-guardian-result.json'; Content = '{"reason":"Exited","detail":null,"processId":42,"exitCode":0}' }
        }
        Mock -ModuleName CtsReliability Write-CtsLogEntry { throw 'simulated logging failure' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch; Completion = $script:Completion } {
            param($Guest, $Launch, $Completion)
            { Get-CtsPilotGuardianResult -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client `
                -Launch $Launch -Completion $Completion -TimeoutSeconds 1 } | Should -Throw '*evidence is unknown*simulated logging failure*'
        }
        Should -Invoke -ModuleName CtsReliability Remove-Job -Times 3 -Exactly
    }
}
