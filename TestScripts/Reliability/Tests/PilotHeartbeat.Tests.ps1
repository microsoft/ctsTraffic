#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
}

Describe 'pilot heartbeat file operations' {
    BeforeEach {
        $script:Directory = Join-Path $TestDrive ([guid]::NewGuid().ToString('N'))
        $null = New-Item -ItemType Directory -Path $script:Directory
    }

    It 'creates exclusively and advances the timestamp without overwriting content' {
        InModuleScope CtsReliability -Parameters @{ Directory = $script:Directory } {
            param($Directory)
            $created = & $script:CtsPilotHeartbeatScript $Directory 'Create'
            [IO.File]::WriteAllText($created.Path, 'preserve')
            [IO.File]::SetLastWriteTimeUtc($created.Path, [DateTime]::UtcNow.AddSeconds(-5))
            $previous = [IO.File]::GetLastWriteTimeUtc($created.Path)
            $pulse = & $script:CtsPilotHeartbeatScript $Directory 'Pulse'
            [IO.File]::GetLastWriteTimeUtc($pulse.Path) | Should -BeGreaterThan $previous
            [IO.File]::ReadAllText($pulse.Path) | Should -Be 'preserve'
            { & $script:CtsPilotHeartbeatScript $Directory 'Create' } | Should -Throw
            [IO.File]::ReadAllText($pulse.Path) | Should -Be 'preserve'
        }
    }

    It 'rejects missing files, future timestamps, and unsafe paths' {
        InModuleScope CtsReliability -Parameters @{ Directory = $script:Directory } {
            param($Directory)
            { & $script:CtsPilotHeartbeatScript $Directory 'Pulse' } | Should -Throw '*missing*'
            $file = & $script:CtsPilotHeartbeatScript $Directory 'Create'
            [IO.File]::SetLastWriteTimeUtc($file.Path, [DateTime]::UtcNow.AddMinutes(1))
            { & $script:CtsPilotHeartbeatScript $Directory 'Pulse' } | Should -Throw '*clock did not advance*'
            { & $script:CtsPilotHeartbeatScript 'C:\runs\..\other' 'Create' } | Should -Throw '*Heartbeat directory*'
            { & $script:CtsPilotHeartbeatScript $Directory 'Other' } | Should -Throw '*Invalid heartbeat action*'
        }
    }
}

Describe 'timed pilot heartbeat operations' {
    BeforeAll {
        $script:Guest = [pscustomobject]@{ Session = (New-MockObject -Type ([System.Management.Automation.Runspaces.PSSession])); Channel = 'Direct'; Target = 'vm'; LogPath = '' }
        $script:CompletedJob = Start-Job -ScriptBlock { }
        $null = Wait-Job -Job $script:CompletedJob
        $script:FailedJob = Start-Job -ScriptBlock { throw 'job failed' }
        $null = Wait-Job -Job $script:FailedJob
    }

    AfterAll {
        Remove-Job -Job $script:CompletedJob -Force -ErrorAction SilentlyContinue
        Remove-Job -Job $script:FailedJob -Force -ErrorAction SilentlyContinue
    }

    BeforeEach {
        Mock -ModuleName CtsReliability Invoke-Command { $script:CompletedJob }
        Mock -ModuleName CtsReliability Wait-Job { $script:CompletedJob }
        Mock -ModuleName CtsReliability Receive-Job {
            [pscustomobject]@{ Path = 'C:\runs\pilot\heartbeat.txt'; UpdatedUtc = [DateTime]::UtcNow.ToString('o') }
        }
        Mock -ModuleName CtsReliability Remove-Job { }
    }

    It 'passes each action to the guest and removes the completed job' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
            param($Guest)
            foreach ($action in 'Create', 'Pulse') {
                (Write-CtsPilotHeartbeat -GuestSession $Guest -Directory 'C:\runs\pilot' -Action $action -TimeoutSeconds 10).Path |
                    Should -Be 'C:\runs\pilot\heartbeat.txt'
            }
        }
        Should -Invoke -ModuleName CtsReliability Invoke-Command -Times 1 -Exactly -ParameterFilter {
            $AsJob -and $ArgumentList[0] -eq 'C:\runs\pilot' -and $ArgumentList[1] -ceq 'Create'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-Command -Times 1 -Exactly -ParameterFilter {
            $AsJob -and $ArgumentList[0] -eq 'C:\runs\pilot' -and $ArgumentList[1] -ceq 'Pulse'
        }
        Should -Invoke -ModuleName CtsReliability Remove-Job -Times 2 -Exactly
    }

    It 'reports timeout and failed jobs as unknown outcomes' {
        Mock -ModuleName CtsReliability Wait-Job { }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
            param($Guest)
            { Write-CtsPilotHeartbeat -GuestSession $Guest -Directory 'C:\runs\pilot' -Action Create -TimeoutSeconds 1 } |
                Should -Throw '*outcome is unknown*'
        }
        Mock -ModuleName CtsReliability Wait-Job { $script:FailedJob }
        Mock -ModuleName CtsReliability Invoke-Command { $script:FailedJob }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
            param($Guest)
            { Write-CtsPilotHeartbeat -GuestSession $Guest -Directory 'C:\runs\pilot' -Action Pulse -TimeoutSeconds 1 } |
                Should -Throw '*outcome is unknown*'
        }
        Should -Invoke -ModuleName CtsReliability Remove-Job -Times 2 -Exactly
    }

    It 'rejects malformed responses and treats logging failures as unknown outcomes' {
        Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Path = 'C:\other\heartbeat.txt'; UpdatedUtc = 'invalid' } }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
            param($Guest)
            { Write-CtsPilotHeartbeat -GuestSession $Guest -Directory 'C:\runs\pilot' -Action Create -TimeoutSeconds 1 } |
                Should -Throw '*expected path*'
        }
        Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Path = 'C:\runs\pilot\heartbeat.txt'; UpdatedUtc = 'invalid' } }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
            param($Guest)
            { Write-CtsPilotHeartbeat -GuestSession $Guest -Directory 'C:\runs\pilot' -Action Pulse -TimeoutSeconds 1 } |
                Should -Throw '*valid UTC timestamp*'
        }
        Mock -ModuleName CtsReliability Receive-Job {
            [pscustomobject]@{ Path = 'C:\runs\pilot\heartbeat.txt'; UpdatedUtc = [DateTime]::UtcNow.ToString('o') }
        }
        Mock -ModuleName CtsReliability Write-CtsLogEntry { throw 'simulated logging failure' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
            param($Guest)
            { Write-CtsPilotHeartbeat -GuestSession $Guest -Directory 'C:\runs\pilot' -Action Pulse -TimeoutSeconds 1 } |
                Should -Throw '*outcome is unknown*simulated logging failure*'
        }
    }
}
