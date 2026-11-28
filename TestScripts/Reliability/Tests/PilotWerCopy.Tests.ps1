#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    function New-WerFile([string]$Name) {
        [pscustomobject]@{ Name = $Name; Bytes = 10; CreatedUtc = '2026-01-01T00:00:00Z'; ModifiedUtc = '2026-01-01T00:00:00Z' }
    }
    function New-WerInventory([object[]]$Files, [string]$Time) {
        [pscustomobject]@{
            Guest = 'CTSREL-A'; Target = 'ctsRel-A'; Directory = 'C:\ctsReliability\dumps'
            BootUtc = '2026-01-01T00:00:00Z'; ObservedUtc = $Time; FileCount = $Files.Count; Files = $Files
        }
    }
}

Describe 'WER candidate copy' {
    It 'copies and hashes a stable dump without modifying or overwriting files' {
        InModuleScope CtsReliability -Parameters @{ Root = $TestDrive } {
            param($Root)
            $source = Join-Path $Root 'source'
            $run = Join-Path $Root 'run'
            $null = New-Item -ItemType Directory -Path $source, $run
            $sourcePath = Join-Path $source 'test.dmp'
            [IO.File]::WriteAllText($sourcePath, 'dump bytes')
            $item = Get-Item -LiteralPath $sourcePath
            $candidate = [pscustomobject]@{
                Name = $item.Name; Bytes = [long]$item.Length
                CreatedUtc = $item.CreationTimeUtc.ToString('o'); ModifiedUtc = $item.LastWriteTimeUtc.ToString('o')
            }
            $result = & $script:CtsPilotWerCopyScript $source $run $candidate
            $result.Sha256 | Should -Be (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
            [IO.File]::ReadAllText($result.DestinationPath) | Should -Be 'dump bytes'
            [IO.File]::ReadAllText($sourcePath) | Should -Be 'dump bytes'
            { & $script:CtsPilotWerCopyScript $source $run $candidate } | Should -Throw
            [IO.File]::ReadAllText($result.DestinationPath) | Should -Be 'dump bytes'
        }
    }

    It 'refuses a changed or actively written source without creating a copy' {
        InModuleScope CtsReliability -Parameters @{ Root = $TestDrive } {
            param($Root)
            $source = Join-Path $Root 'source2'
            $run = Join-Path $Root 'run2'
            $null = New-Item -ItemType Directory -Path $source, $run
            $sourcePath = Join-Path $source 'test.dmp'
            [IO.File]::WriteAllText($sourcePath, 'old')
            $item = Get-Item -LiteralPath $sourcePath
            $candidate = [pscustomobject]@{
                Name = $item.Name; Bytes = [long]$item.Length
                CreatedUtc = $item.CreationTimeUtc.ToString('o'); ModifiedUtc = $item.LastWriteTimeUtc.ToString('o')
            }
            [IO.File]::WriteAllText($sourcePath, 'changed')
            { & $script:CtsPilotWerCopyScript $source $run $candidate } | Should -Throw '*changed*'
            Test-Path -LiteralPath (Join-Path $run 'wer-test.dmp') | Should -BeFalse
            $writer = [IO.File]::Open($sourcePath, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
            try { { & $script:CtsPilotWerCopyScript $source $run $candidate } | Should -Throw }
            finally { $writer.Dispose() }
            Test-Path -LiteralPath (Join-Path $run 'wer-test.dmp') | Should -BeFalse
        }
    }

    It 'rejects pre-existing names and mismatched guest sessions' {
        $before = New-WerInventory @((New-WerFile 'old.dmp')) '2026-01-01T01:00:00Z'
        $after = New-WerInventory @((New-WerFile 'old.dmp'), (New-WerFile 'new.dmp')) '2026-01-01T02:00:00Z'
        InModuleScope CtsReliability -Parameters @{ Before = $before; After = $after } {
            param($Before, $After)
            $guest = [pscustomobject]@{ Target = 'ctsRel-A' }
            { Copy-CtsPilotWerCandidate -GuestSession $guest -Before $Before -After $After -Name 'old.dmp' `
                -RunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 2 } | Should -Throw '*candidate*'
            $other = [pscustomobject]@{ Target = 'ctsRel-B' }
            { Copy-CtsPilotWerCandidate -GuestSession $other -Before $Before -After $After -Name 'new.dmp' `
                -RunDirectory 'C:\ctsReliability\runs\pilot-a' -TimeoutSeconds 2 } | Should -Throw '*guest session*'
        }
    }
}
