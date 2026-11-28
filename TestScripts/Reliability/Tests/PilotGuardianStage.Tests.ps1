#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Guest = [pscustomobject]@{}
}

Describe 'experimental guardian staging' {
    BeforeEach {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { & $ScriptBlock @ArgumentList }
        $script:Directory = Join-Path $TestDrive ([guid]::NewGuid().ToString('N'))
        $null = New-Item -ItemType Directory -Path $script:Directory
    }

    It 'stages exact source files and the requested specification with verified hashes' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Directory = $script:Directory } {
            param($Guest, $Directory)
            $files = @(New-CtsPilotGuardianStage -GuestSession $Guest -Directory $Directory -Role Client `
                -Executable 'C:\Windows\powershell.exe' -Arguments @('-NoProfile', '-NonInteractive') `
                -MinimumFreeMemoryBytes 104857600 -MinimumFreeDiskBytes 10737418240 `
                -HeartbeatTimeoutSeconds 10 -MaximumLifetimeSeconds 30 -PollMilliseconds 200)
            $files.Count | Should -Be 3
            foreach ($file in $files) {
                (Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash | Should -Be $file.Sha256
            }
            foreach ($name in 'PilotGuardian.ps1', 'PilotSelfJob.cs') {
                (Get-FileHash -LiteralPath (Join-Path $Directory $name)).Hash |
                    Should -Be (Get-FileHash -LiteralPath (Join-Path $script:CtsPilotGuardianSupportDirectory $name)).Hash
            }
            $spec = Get-Content -LiteralPath (Join-Path $Directory 'spec.json') -Raw | ConvertFrom-Json
            $spec.role | Should -Be 'client'
            $spec.executable | Should -Be 'C:\Windows\powershell.exe'
            $spec.arguments | Should -Be @('-NoProfile', '-NonInteractive')
            $spec.minimumFreeMemoryBytes | Should -Be 104857600
            $spec.minimumFreeDiskBytes | Should -Be 10737418240
            $spec.heartbeatTimeoutSeconds | Should -Be 10
            $spec.maximumLifetimeSeconds | Should -Be 30
            $spec.pollMilliseconds | Should -Be 200
        }
    }

    It 'preserves existing files and reports partial staging as unknown' {
        $existing = Join-Path $script:Directory 'spec.json'
        [IO.File]::WriteAllText($existing, 'preserve')
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Directory = $script:Directory } {
            param($Guest, $Directory)
            { New-CtsPilotGuardianStage -GuestSession $Guest -Directory $Directory -Role Server `
                -Executable 'C:\Windows\powershell.exe' -Arguments @('-NoProfile') `
                -MinimumFreeMemoryBytes 1 -MinimumFreeDiskBytes 1 -HeartbeatTimeoutSeconds 10 `
                -MaximumLifetimeSeconds 30 -PollMilliseconds 200 } | Should -Throw '*staging outcome is unknown*'
        }
        [IO.File]::ReadAllText($existing) | Should -Be 'preserve'
        Test-Path -LiteralPath (Join-Path $script:Directory 'PilotGuardian.ps1') | Should -BeTrue
    }

    It 'rejects unsafe inputs before contacting the guest' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Directory = $script:Directory } {
            param($Guest, $Directory)
            $params = @{
                GuestSession = $Guest; Directory = $Directory; Role = 'Client'
                Executable = 'C:\Windows\powershell.exe'; Arguments = @('-NoProfile')
                MinimumFreeMemoryBytes = 1; MinimumFreeDiskBytes = 1
                HeartbeatTimeoutSeconds = 2; MaximumLifetimeSeconds = 30; PollMilliseconds = 200
            }
            $params.Directory = 'C:\runs\..\other'
            { New-CtsPilotGuardianStage @params } | Should -Throw
            $params.Directory = $Directory; $params.Executable = 'C:\bad path\app.exe'
            { New-CtsPilotGuardianStage @params } | Should -Throw
            $params.Executable = 'C:\Windows\powershell.exe'; $params.Arguments = @('bad argument')
            { New-CtsPilotGuardianStage @params } | Should -Throw
            $params.Arguments = @('-NoProfile'); $params.PollMilliseconds = 2000
            { New-CtsPilotGuardianStage @params } | Should -Throw
        }
        Should -Invoke -ModuleName CtsReliability Invoke-CtsGuestCommand -Times 0
    }

    It 'rejects missing and mismatched returned hashes' {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Directory = $script:Directory } {
            param($Guest, $Directory)
            $params = @{
                GuestSession = $Guest; Directory = $Directory; Role = 'Client'
                Executable = 'C:\Windows\powershell.exe'; Arguments = @('-NoProfile')
                MinimumFreeMemoryBytes = 1; MinimumFreeDiskBytes = 1
                HeartbeatTimeoutSeconds = 10; MaximumLifetimeSeconds = 30; PollMilliseconds = 200
            }
            { New-CtsPilotGuardianStage @params } | Should -Throw '*staging outcome is unknown*'
        }
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand {
            foreach ($name in 'PilotGuardian.ps1', 'PilotSelfJob.cs', 'spec.json') {
                [pscustomobject]@{ Name = $name; Path = [IO.Path]::Combine($Directory, $name); Sha256 = 'wrong' }
            }
        }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Directory = $script:Directory } {
            param($Guest, $Directory)
            { New-CtsPilotGuardianStage -GuestSession $Guest -Directory $Directory -Role Client `
                -Executable 'C:\Windows\powershell.exe' -Arguments @('-NoProfile') `
                -MinimumFreeMemoryBytes 1 -MinimumFreeDiskBytes 1 -HeartbeatTimeoutSeconds 10 `
                -MaximumLifetimeSeconds 30 -PollMilliseconds 200 } | Should -Throw '*staging outcome is unknown*'
        }
    }
}
