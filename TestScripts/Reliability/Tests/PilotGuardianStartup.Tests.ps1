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
}

Describe 'guardian startup evidence' {
    It 'reads only complete lines while the guardian is writing' {
        InModuleScope CtsReliability -Parameters @{ Directory = $TestDrive } {
            param($Directory)
            $path = Join-Path $Directory 'events.jsonl'
            (& $script:CtsPilotGuardianStartupScript $path).Content | Should -Be ''
            $bytes = [Text.Encoding]::UTF8.GetBytes("{`"type`":`"sample`"}`n{`"type`":`"started`"")
            $writer = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
            try {
                $writer.Write($bytes, 0, $bytes.Length)
                $writer.Flush()
                $content = (& $script:CtsPilotGuardianStartupScript $path).Content
                $content | Should -Be "{`"type`":`"sample`"}`n"
                (ConvertFrom-CtsPilotGuardianStartup -Content $content).ChildProcessId | Should -BeNullOrEmpty
            }
            finally { $writer.Dispose() }
        }
    }

    It 'accepts a complete start event but rejects malformed or duplicated events' {
        InModuleScope CtsReliability {
            $event = ConvertFrom-CtsPilotGuardianStartup -Content "{`"type`":`"sample`"}`n{`"type`":`"started`",`"processId`":123}`n"
            $event.ChildProcessId | Should -Be 123
            { ConvertFrom-CtsPilotGuardianStartup -Content "{bad}`n" } | Should -Throw
            { ConvertFrom-CtsPilotGuardianStartup -Content "{`"type`":`"started`",`"processId`":0}`n" } | Should -Throw
            { ConvertFrom-CtsPilotGuardianStartup -Content ("{`"type`":`"started`",`"processId`":123}`n" * 2) } | Should -Throw
        }
    }
}

Describe 'guardian startup observer' {
    BeforeEach {
        $script:StatusCount = 0
        Mock -ModuleName CtsReliability Write-CtsPilotHeartbeat { }
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            $script:StatusCount++
            [pscustomobject]@{ Exited = $false }
        }

        Mock -ModuleName CtsReliability Invoke-CtsLogged {
            [pscustomobject]@{
                Path = 'C:\runs\pilot\g1b-pilot-server-guardian.jsonl'
                Content = "{`"type`":`"started`",`"processId`":123}`n"
            }
        }
    }

    It 'pulses and checks the tracked guardian before and after the child-start event' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Wait-CtsPilotGuardianStartup -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Server `
                -Launch $Launch -StartupTimeoutSeconds 2 -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'ChildStarted'
            $result.ChildProcessId | Should -Be 123
            $result.Token | Should -Be $Launch.Token
            $result.GuardianProcessId | Should -Be $Launch.ProcessId
            $result.Directory | Should -Be 'C:\runs\pilot'
            $result.Role | Should -Be 'Server'
            $result.Target | Should -Be 'A'
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 1 -Exactly -ParameterFilter { $Action -eq 'Pulse' }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 2 -Exactly -ParameterFilter {
            $Action -eq 'Status' -and $Token -eq ('a' * 32) -and $ExpectedProcessId -eq 100
        }
    }

    It 'does not report startup if the tracked guardian exited after the event' {
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            $script:StatusCount++
            [pscustomobject]@{ Exited = ($script:StatusCount -gt 1) }
        }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Wait-CtsPilotGuardianStartup -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Server `
                -Launch $Launch -StartupTimeoutSeconds 2 -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'GuardianExited'
            $result.ChildProcessId | Should -Be 123
        }
    }

    It 'reports unknown on monitoring failure without stopping the process' {
        Mock -ModuleName CtsReliability Invoke-CtsLogged { throw 'remote outcome unknown' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Wait-CtsPilotGuardianStartup -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Server `
                -Launch $Launch -StartupTimeoutSeconds 2 -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Unknown'
            $result.Error | Should -Match 'remote outcome unknown'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'reports a timeout if no complete start event appears' {
        Mock -ModuleName CtsReliability Invoke-CtsLogged {
            [pscustomobject]@{ Path = 'C:\runs\pilot\g1b-pilot-server-guardian.jsonl'; Content = '' }
        }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            $result = Wait-CtsPilotGuardianStartup -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Server `
                -Launch $Launch -StartupTimeoutSeconds 1 -PollSeconds 1 -TimeoutSeconds 2
            $result.Outcome | Should -Be 'Timeout'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0 -ParameterFilter { $Action -eq 'Stop' }
    }

    It 'rejects a receipt for a different endpoint before pulsing' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Launch = $script:Launch } {
            param($Guest, $Launch)
            { Wait-CtsPilotGuardianStartup -GuestSession $Guest -Directory 'C:\runs\other' -Role Server `
                -Launch $Launch -StartupTimeoutSeconds 2 -PollSeconds 1 -TimeoutSeconds 2 } | Should -Throw '*receipt*'
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 0
    }
}
