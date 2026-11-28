#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    function New-WerFile([string]$Name, [long]$Bytes = 10) {
        [pscustomobject]@{
            Name = $Name; Bytes = $Bytes
            CreatedUtc = '2026-01-01T00:00:00Z'; ModifiedUtc = '2026-01-01T00:00:00Z'
        }
    }
    function New-WerInventory([object[]]$Files, [string]$Time = '2026-01-01T01:00:00Z') {
        [pscustomobject]@{
            Guest = 'CTSREL-A'; Target = 'ctsRel-A'; Directory = 'C:\ctsReliability\dumps'
            BootUtc = '2026-01-01T00:00:00Z'; ObservedUtc = $Time
            FileCount = $Files.Count; Files = $Files
        }
    }
    $script:Guest = [pscustomobject]@{ Channel = 'Direct'; Target = 'ctsRel-A'; LogPath = ''; Session = $null }
}

Describe 'WER dump inventory' {
    It 'records dump metadata without reading or modifying file contents' {
        Mock -ModuleName CtsReliability Get-CimInstance { [pscustomobject]@{ LastBootUpTime = [DateTime]::UtcNow } }
        InModuleScope CtsReliability -Parameters @{ Directory = $TestDrive } {
            param($Directory)
            [IO.File]::WriteAllText((Join-Path $Directory 'sample.dmp'), 'dump')
            [IO.File]::WriteAllText((Join-Path $Directory 'other.txt'), 'ignore')
            $inventory = & $script:CtsPilotWerInventoryScript $Directory
            $inventory.FileCount | Should -Be 1
            $inventory.Files[0].Name | Should -Be 'sample.dmp'
            $inventory.Files[0].Bytes | Should -Be 4
            [IO.File]::ReadAllText((Join-Path $Directory 'sample.dmp')) | Should -Be 'dump'
        }
    }

    It 'treats a failed remote inventory as unknown' {
        Mock -ModuleName CtsReliability Invoke-CtsLogged { throw 'session lost' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
            param($Guest)
            { Get-CtsPilotWerInventory -GuestSession $Guest -Directory 'C:\ctsReliability\dumps' -TimeoutSeconds 2 } |
                Should -Throw '*unknown*session lost*'
        }
    }
}

Describe 'WER inventory comparison' {
    It 'identifies new names and flags changed or removed old dumps' {
        $before = New-WerInventory @((New-WerFile 'old.dmp'), (New-WerFile 'removed.dmp'))
        $after = New-WerInventory @((New-WerFile 'old.dmp' 20), (New-WerFile 'new.dmp')) '2026-01-01T02:00:00Z'
        InModuleScope CtsReliability -Parameters @{ Before = $before; After = $after } {
            param($Before, $After)
            $result = Compare-CtsPilotWerInventory -Before $Before -After $After
            @($result.Candidates).Count | Should -Be 1
            $result.Candidates[0].Name | Should -Be 'new.dmp'
            @($result.Ambiguous | Where-Object { $_.Name -eq 'old.dmp' -and $_.Reason -eq 'Changed' }).Count | Should -Be 1
            @($result.Ambiguous | Where-Object { $_.Name -eq 'removed.dmp' -and $_.Reason -eq 'MissingAfter' }).Count | Should -Be 1
        }

    }

    It 'supports empty inventories and preserves pre-existing dump names' {
        $empty = New-WerInventory @()
        $after = New-WerInventory @((New-WerFile 'old.dmp')) '2026-01-01T02:00:00Z'
        InModuleScope CtsReliability -Parameters @{ Empty = $empty; After = $after } {
            param($Empty, $After)
            @((Compare-CtsPilotWerInventory -Before $Empty -After $After).Candidates).Count | Should -Be 1
            @((Compare-CtsPilotWerInventory -Before $After -After $After).Candidates).Count | Should -Be 0
        }
    }

    It 'flags a reboot and rejects changed identity or reversed observations' {
        $before = New-WerInventory @()
        $after = New-WerInventory @() '2026-01-01T02:00:00Z'
        InModuleScope CtsReliability -Parameters @{ Before = $before; After = $after } {
            param($Before, $After)
            $After.Target = 'ctsRel-B'
            { Compare-CtsPilotWerInventory -Before $Before -After $After } | Should -Throw '*Target*'
            $After.Target = $Before.Target
            $After.BootUtc = '2026-01-01T01:00:00Z'
            (Compare-CtsPilotWerInventory -Before $Before -After $After).BootChanged | Should -BeTrue
            $After.BootUtc = $Before.BootUtc
            $After.ObservedUtc = '2025-12-31T23:00:00Z'
            { Compare-CtsPilotWerInventory -Before $Before -After $After } | Should -Throw '*backwards*'
        }
    }

    It 'rejects duplicate names and inconsistent file counts' {
        $duplicate = New-WerInventory @((New-WerFile 'same.dmp'), (New-WerFile 'SAME.dmp'))
        $valid = New-WerInventory @()
        InModuleScope CtsReliability -Parameters @{ Duplicate = $duplicate; Valid = $valid } {
            param($Duplicate, $Valid)
            { Compare-CtsPilotWerInventory -Before $Duplicate -After $Valid } | Should -Throw '*duplicate*'
            $Valid.FileCount = 1
            { Compare-CtsPilotWerInventory -Before $Valid -After $Duplicate } | Should -Throw '*count*'
        }
    }
}
