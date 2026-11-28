#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Guest = [pscustomobject]@{ Channel = 'Direct'; Target = 'vm'; LogPath = '' }
    $script:Token = 'a' * 32
}

Describe 'experimental guardian launch' {
    BeforeEach {
        Mock -ModuleName CtsReliability Write-CtsPilotHeartbeat { [pscustomobject]@{ Path = 'C:\runs\pilot\heartbeat.txt' } }
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction {
            [pscustomobject]@{ ProcessId = 123; StartedUtc = '2026-01-01T00:00:00Z' }
        }
    }

    It 'creates a heartbeat then starts the dedicated guardian with distinct process output paths' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Token = $script:Token } {
            param($Guest, $Token)
            $result = Start-CtsPilotGuardian -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Server -Token $Token `
                -PowerShellPath 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -TimeoutSeconds 10
            $result.ProcessId | Should -Be 123
            $result.HeartbeatPath | Should -Be 'C:\runs\pilot\heartbeat.txt'
            $result.StdoutPath | Should -Be 'C:\runs\pilot\g1b-pilot-server-guardian-process-stdout.txt'
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 1 -Exactly -ParameterFilter {
            $Action -eq 'Create' -and $TimeoutSeconds -eq 10
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 1 -Exactly -ParameterFilter {
            $Action -eq 'Start' -and $Token -eq ('a' * 32) -and $TimeoutSeconds -eq 10 -and
            ($Arguments -join '|') -ceq '-NoProfile|-NonInteractive|-ExecutionPolicy|RemoteSigned|-File|C:\runs\pilot\PilotGuardian.ps1|-SpecPath|C:\runs\pilot\spec.json' -and
            $StdoutPath -eq 'C:\runs\pilot\g1b-pilot-server-guardian-process-stdout.txt' -and
            $StderrPath -eq 'C:\runs\pilot\g1b-pilot-server-guardian-process-stderr.txt'
        }
    }

    It 'does not launch if heartbeat creation fails' {
        Mock -ModuleName CtsReliability Write-CtsPilotHeartbeat { throw 'heartbeat outcome is unknown' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Token = $script:Token } {
            param($Guest, $Token)
            { Start-CtsPilotGuardian -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client -Token $Token `
                    -PowerShellPath 'C:\Windows\powershell.exe' -TimeoutSeconds 1 } | Should -Throw '*outcome is unknown*'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 0
    }

    It 'propagates unknown launch outcomes without a second start' {
        Mock -ModuleName CtsReliability Invoke-CtsPilotProcessAction { throw 'start outcome is unknown' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Token = $script:Token } {
            param($Guest, $Token)
            { Start-CtsPilotGuardian -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client -Token $Token `
                    -PowerShellPath 'C:\Windows\powershell.exe' -TimeoutSeconds 1 } | Should -Throw '*outcome is unknown*'
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsPilotProcessAction -Times 1 -Exactly
    }

    It 'rejects unsafe inputs before creating a heartbeat' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Token = $script:Token } {
            param($Guest, $Token)
            { Start-CtsPilotGuardian -GuestSession $Guest -Directory 'C:\runs\..\other' -Role Client -Token $Token -PowerShellPath 'C:\Windows\powershell.exe' -TimeoutSeconds 1 } | Should -Throw
            { Start-CtsPilotGuardian -GuestSession $Guest -Directory 'C:\' -Role Client -Token $Token -PowerShellPath 'C:\Windows\powershell.exe' -TimeoutSeconds 1 } | Should -Throw
            { Start-CtsPilotGuardian -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client -Token $Token -PowerShellPath 'C:\Windows\cmd.exe' -TimeoutSeconds 1 } | Should -Throw
            { Start-CtsPilotGuardian -GuestSession $Guest -Directory 'C:\runs\pilot' -Role Client -Token 'invalid' -PowerShellPath 'C:\Windows\powershell.exe' -TimeoutSeconds 1 } | Should -Throw
        }
        Should -Invoke -ModuleName CtsReliability Write-CtsPilotHeartbeat -Times 0
    }
}
