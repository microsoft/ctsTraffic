#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    function New-CleanupFile([string]$Name, [long]$Bytes = 10) {
        [pscustomobject]@{
            Name = $Name; Bytes = $Bytes
            CreatedUtc = '2026-01-01T00:00:00Z'; ModifiedUtc = '2026-01-01T00:00:00Z'
        }
    }
    function New-CleanupInventory([object[]]$Files, [string]$Time) {
        [pscustomobject]@{
            Guest = 'CTSREL-A'; Target = 'ctsRel-A'; Directory = 'C:\ctsReliability\dumps'
            BootUtc = '2026-01-01T00:00:00Z'; ObservedUtc = $Time; FileCount = $Files.Count; Files = $Files
        }
    }
    $script:Guest = [pscustomobject]@{ Channel = 'Direct'; Target = 'ctsRel-A'; LogPath = ''; Session = $null }
}

Describe 'WER endpoint cleanup' {
    BeforeEach {
        $script:Before = New-CleanupInventory @((New-CleanupFile 'old.dmp'), (New-CleanupFile 'removed.dmp')) '2026-01-01T01:00:00Z'
        $script:After = New-CleanupInventory @((New-CleanupFile 'old.dmp' 20), (New-CleanupFile 'new.dmp')) '2026-01-01T02:00:00Z'
        $script:FailCopy = ''
        $script:FailTransfer = ''
        Mock -ModuleName CtsReliability Get-CtsPilotWerInventory { $script:After }
        Mock -ModuleName CtsReliability Copy-CtsPilotWerCandidate {
            if ($Name -eq $script:FailCopy) { throw 'guest copy outcome unknown' }
            [pscustomobject]@{ Name = $Name; DestinationPath = [IO.Path]::Combine($RunDirectory, ('wer-' + $Name)) }
        }
        Mock -ModuleName CtsReliability Copy-CtsPilotWerToHost {
            if ($Receipt.Name -eq $script:FailTransfer) { throw 'host transfer outcome unknown' }
            [pscustomobject]@{ Name = $Receipt.Name; HostPath = [IO.Path]::Combine($HostRunDirectory, "pilot-$TransferToken", ('wer-' + $Receipt.Name)) }
        }
    }

    It 'collects new and changed candidates while retaining ambiguity and missing-file evidence' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Before = $script:Before } {
            param($Guest, $Before)
            $result = Invoke-CtsPilotWerCleanup -GuestSession $Guest -Before $Before `
                -GuestRunDirectory 'C:\ctsReliability\runs\pilot-a' -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10
            $result.Outcome | Should -Be 'Incomplete'
            $result.Copies.Count | Should -Be 2
            @($result.Copies | Where-Object { $_.Name -eq 'new.dmp' -and $_.Kind -eq 'NewName' -and $_.Outcome -eq 'Transferred' }).Count | Should -Be 1
            @($result.Copies | Where-Object { $_.Name -eq 'old.dmp' -and $_.Kind -eq 'ChangedExisting' -and $_.Outcome -eq 'Transferred' }).Count | Should -Be 1
            @($result.Comparison.Ambiguous | Where-Object { $_.Name -eq 'removed.dmp' -and $_.Reason -eq 'MissingAfter' }).Count | Should -Be 1
            $result.Copies[0].HostDirectory | Should -Not -Be $result.Copies[1].HostDirectory
        }
        Should -Invoke -ModuleName CtsReliability Copy-CtsPilotWerCandidate -Times 2 -Exactly
        Should -Invoke -ModuleName CtsReliability Copy-CtsPilotWerToHost -Times 2 -Exactly
    }

    It 'reports completed collection after transferring a new candidate' {
        $script:Before = New-CleanupInventory @() '2026-01-01T01:00:00Z'
        $script:After = New-CleanupInventory @((New-CleanupFile 'new.dmp')) '2026-01-01T02:00:00Z'
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Before = $script:Before } {
            param($Guest, $Before)
            $result = Invoke-CtsPilotWerCleanup -GuestSession $Guest -Before $Before `
                -GuestRunDirectory 'C:\ctsReliability\runs\pilot-a' -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10
            $result.Outcome | Should -Be 'Completed'
            $result.Copies.Count | Should -Be 1
            $result.Copies[0].Outcome | Should -Be 'Transferred'
        }
    }

    It 'records an unknown guest copy and continues with another candidate without retrying' {
        $script:FailCopy = 'new.dmp'
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Before = $script:Before } {
            param($Guest, $Before)
            $result = Invoke-CtsPilotWerCleanup -GuestSession $Guest -Before $Before `
                -GuestRunDirectory 'C:\ctsReliability\runs\pilot-a' -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10
            $failed = @($result.Copies | Where-Object Name -eq 'new.dmp')[0]
            $failed.Outcome | Should -Be 'GuestCopyUnknown'
            $failed.Error | Should -Match 'guest copy outcome unknown'
            $failed.GuestPath | Should -Match 'wer-new\.dmp$'
            @($result.Copies | Where-Object { $_.Name -eq 'old.dmp' -and $_.Outcome -eq 'Transferred' }).Count | Should -Be 1
        }
        Should -Invoke -ModuleName CtsReliability Copy-CtsPilotWerCandidate -Times 1 -Exactly -ParameterFilter { $Name -eq 'new.dmp' }
        Should -Invoke -ModuleName CtsReliability Copy-CtsPilotWerToHost -Times 1 -Exactly
    }

    It 'preserves a guest receipt and attempted host path when transfer fails' {
        $script:FailTransfer = 'new.dmp'
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Before = $script:Before } {
            param($Guest, $Before)
            $result = Invoke-CtsPilotWerCleanup -GuestSession $Guest -Before $Before `
                -GuestRunDirectory 'C:\ctsReliability\runs\pilot-a' -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10
            $failed = @($result.Copies | Where-Object Name -eq 'new.dmp')[0]
            $failed.Outcome | Should -Be 'HostTransferUnknown'
            $failed.GuestReceipt.Name | Should -Be 'new.dmp'
            $failed.HostDirectory | Should -Match '\\pilot-[0-9a-f]{32}$'
            $failed.Error | Should -Match 'host transfer outcome unknown'
        }
        Should -Invoke -ModuleName CtsReliability Copy-CtsPilotWerToHost -Times 1 -Exactly -ParameterFilter { $Receipt.Name -eq 'new.dmp' }
    }

    It 'reports completed collection for a stable empty inventory but not after a reboot' {
        $script:Before = New-CleanupInventory @() '2026-01-01T01:00:00Z'
        $script:After = New-CleanupInventory @() '2026-01-01T02:00:00Z'
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Before = $script:Before } {
            param($Guest, $Before)
            $result = Invoke-CtsPilotWerCleanup -GuestSession $Guest -Before $Before `
                -GuestRunDirectory 'C:\ctsReliability\runs\pilot-a' -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10
            $result.Outcome | Should -Be 'Completed'
            $result.Copies.Count | Should -Be 0
        }
        $script:After.BootUtc = '2026-01-01T01:30:00Z'
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Before = $script:Before } {
            param($Guest, $Before)
            $result = Invoke-CtsPilotWerCleanup -GuestSession $Guest -Before $Before `
                -GuestRunDirectory 'C:\ctsReliability\runs\pilot-a' -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10
            $result.Outcome | Should -Be 'Incomplete'
            $result.Comparison.BootChanged | Should -BeTrue
        }
    }

    It 'reports an unavailable final inventory without making copies' {
        Mock -ModuleName CtsReliability Get-CtsPilotWerInventory { throw 'session lost' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Before = $script:Before } {
            param($Guest, $Before)
            $result = Invoke-CtsPilotWerCleanup -GuestSession $Guest -Before $Before `
                -GuestRunDirectory 'C:\ctsReliability\runs\pilot-a' -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10
            $result.Outcome | Should -Be 'Incomplete'
            $result.Error | Should -Match 'session lost'
        }
        Should -Invoke -ModuleName CtsReliability Copy-CtsPilotWerCandidate -Times 0
    }

    It 'rejects a mismatched guest or shared source and destination before inventory' {
        InModuleScope CtsReliability -Parameters @{ Before = $script:Before } {
            param($Before)
            $other = [pscustomobject]@{ Target = 'ctsRel-B' }
            { Invoke-CtsPilotWerCleanup -GuestSession $other -Before $Before -GuestRunDirectory 'C:\ctsReliability\runs\pilot-a' `
                -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10 } | Should -Throw '*guest session*'
            $guest = [pscustomobject]@{ Target = 'ctsRel-A' }
            { Invoke-CtsPilotWerCleanup -GuestSession $guest -Before $Before -GuestRunDirectory 'C:\ctsReliability\dumps\' `
                -HostRunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 10 } | Should -Throw '*distinct*'
        }
        Should -Invoke -ModuleName CtsReliability Get-CtsPilotWerInventory -Times 0
    }
}
