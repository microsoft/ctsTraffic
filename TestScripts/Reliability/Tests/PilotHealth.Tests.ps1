#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Guest = [pscustomobject]@{ Session = (New-MockObject -Type ([System.Management.Automation.Runspaces.PSSession])); Channel = 'Direct'; Target = 'vm'; LogPath = '' }
    $script:Boot = [datetime]::SpecifyKind([datetime]'2026-11-27T12:00:00', [DateTimeKind]::Utc)
    $script:Sample = [pscustomobject]@{ BootUtc = $script:Boot.ToString('o'); FreeMemoryBytes = [long]104857600; FreeDiskBytes = [long]10737418240 }
}

Describe 'Get-CtsPilotGuestSample' {
    BeforeAll {
        $script:Job = Start-Job -ScriptBlock { }
        $null = Wait-Job -Job $script:Job
        $script:FailedJob = Start-Job -ScriptBlock { throw 'job failed' }
        $null = Wait-Job -Job $script:FailedJob
    }

    AfterAll {
        Remove-Job -Job $script:Job -Force -ErrorAction SilentlyContinue
        Remove-Job -Job $script:FailedJob -Force -ErrorAction SilentlyContinue
    }

    BeforeEach {
        $script:Guest.LogPath = ''
        Mock -ModuleName CtsReliability Invoke-Command { $script:Job }
        Mock -ModuleName CtsReliability Wait-Job { $script:Job }
        Mock -ModuleName CtsReliability Receive-Job { $script:Sample }
        Mock -ModuleName CtsReliability Remove-Job { }
    }

    It 'returns the remote sample and removes its job' {
        $script:Guest.LogPath = Join-Path $TestDrive 'probe.jsonl'
        $sample = Get-CtsPilotGuestSample -GuestSession $script:Guest -TimeoutSeconds 10
        $sample.BootUtc | Should -Be $script:Sample.BootUtc
        $entry = Get-Content -LiteralPath $script:Guest.LogPath -Raw | ConvertFrom-Json
        $entry.operation | Should -Be 'PilotGuestSample'
        $entry.success | Should -BeTrue
        Should -Invoke -ModuleName CtsReliability Invoke-Command -Times 1 -Exactly -ParameterFilter { $AsJob }
        Should -Invoke -ModuleName CtsReliability Remove-Job -Times 1 -Exactly
    }

    It 'throws on timeout or missing sample and cleans up its job' {
        Mock -ModuleName CtsReliability Wait-Job { }
        { Get-CtsPilotGuestSample -GuestSession $script:Guest -TimeoutSeconds 1 } | Should -Throw '*timed out*'
        Should -Invoke -ModuleName CtsReliability Remove-Job -Times 1 -Exactly
        Mock -ModuleName CtsReliability Wait-Job { $script:Job }
        Mock -ModuleName CtsReliability Receive-Job { }
        { Get-CtsPilotGuestSample -GuestSession $script:Guest -TimeoutSeconds 1 } | Should -Throw '*exactly one sample*'
    }

    It 'propagates a remote probe error instead of returning a sample' {
        Mock -ModuleName CtsReliability Receive-Job { throw 'remote probe failed' }
        { Get-CtsPilotGuestSample -GuestSession $script:Guest -TimeoutSeconds 1 } | Should -Throw '*remote probe failed*'
        Should -Invoke -ModuleName CtsReliability Remove-Job -Times 1 -Exactly
    }

    It 'rejects a failed job even if it could return partial output' {
        Mock -ModuleName CtsReliability Invoke-Command { $script:FailedJob }
        { Get-CtsPilotGuestSample -GuestSession $script:Guest -TimeoutSeconds 1 } | Should -Throw "*state 'Failed'*"
    }
}

Describe 'Test-CtsPilotGuestSample' {
    It 'accepts a matching boot and values at the specified minimums' {
        $findings = @(Test-CtsPilotGuestSample -Sample $script:Sample -ExpectedBootUtc $script:Boot -MinimumFreeMemoryBytes 104857600 -MinimumFreeDiskBytes 10737418240)
        $findings | Should -BeNullOrEmpty
    }

    It 'reports a changed boot and resource values below the limits' {
        $sample = [pscustomobject]@{ BootUtc = '2026-11-27T12:01:00Z'; FreeMemoryBytes = 99; FreeDiskBytes = 999 }
        $findings = @(Test-CtsPilotGuestSample -Sample $sample -ExpectedBootUtc $script:Boot -MinimumFreeMemoryBytes 100 -MinimumFreeDiskBytes 1000)
        $findings | Should -Contain 'Guest boot time changed'
        $findings | Should -Contain 'Guest FreeMemoryBytes is below the minimum'
        $findings | Should -Contain 'Guest FreeDiskBytes is below the minimum'
    }

    It 'reports missing, invalid and timezone-less data instead of accepting it' {
        $sample = [pscustomobject]@{ BootUtc = '2026-11-27T12:00:00'; FreeMemoryBytes = '100'; FreeDiskBytes = -1 }
        $findings = @(Test-CtsPilotGuestSample -Sample $sample -ExpectedBootUtc $script:Boot -MinimumFreeMemoryBytes 100 -MinimumFreeDiskBytes 1000)
        $findings | Should -Contain 'Guest boot time is missing or invalid'
        $findings | Should -Contain 'Guest FreeMemoryBytes is missing or invalid'
        $findings | Should -Contain 'Guest FreeDiskBytes is missing or invalid'
        $missing = @(Test-CtsPilotGuestSample -Sample ([pscustomobject]@{}) -ExpectedBootUtc $script:Boot -MinimumFreeMemoryBytes 100 -MinimumFreeDiskBytes 1000)
        $missing.Count | Should -Be 3
    }

    It 'rejects an expected boot time without an explicit UTC kind' {
        $unspecified = [datetime]::SpecifyKind($script:Boot, [DateTimeKind]::Unspecified)
        { Test-CtsPilotGuestSample -Sample $script:Sample -ExpectedBootUtc $unspecified -MinimumFreeMemoryBytes 100 -MinimumFreeDiskBytes 1000 } | Should -Throw '*UTC DateTime*'
    }
}
