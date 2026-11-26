#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Tests for src\PoolTag.ps1. The parser is tested with synthetic SYSTEM_POOLTAG_INFORMATION buffers; the live
# kernel query is tagged 'Elevated' because Windows returns only a placeholder entry to non-administrators.

BeforeDiscovery {
    $script:IsElevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    InModuleScope CtsReliability { & $script:CtsPoolTagScript -LoadOnly }

    function Set-Int32([byte[]]$Buffer, [int]$Offset, [int]$Value) { [BitConverter]::GetBytes($Value).CopyTo($Buffer, $Offset) }
    function Set-Int64([byte[]]$Buffer, [int]$Offset, [long]$Value) { [BitConverter]::GetBytes($Value).CopyTo($Buffer, $Offset) }
    function Set-Tag([byte[]]$Buffer, [int]$Offset, [byte[]]$Tag) { $Tag.CopyTo($Buffer, $Offset) }

    function Export-TestCsv {
        param([Parameter(ValueFromPipeline)]$InputObject, [string]$Path)
        begin { $rows = @() } process { $rows += $InputObject }
        end {
            # -NoTypeInformation is needed on 5.1 and obsolete (warns) on 7.
            $csvArgs = @{ LiteralPath = $Path }
            if ($PSVersionTable.PSVersion.Major -lt 6) { $csvArgs.NoTypeInformation = $true }
            $rows | Export-Csv @csvArgs
        }
    }

    function New-Row([string]$Tag, [long]$Paged, [long]$NonPaged, [long]$PagedDiff = 0, [long]$NonPagedDiff = 0) {
        [pscustomobject]@{ Tag = $Tag; PagedAllocs = 0; PagedFrees = 0; PagedDiff = $PagedDiff; PagedUsed = $Paged
            NonPagedAllocs = 0; NonPagedFrees = 0; NonPagedDiff = $NonPagedDiff; NonPagedUsed = $NonPaged }
    }
}

Describe 'PoolTagReader.Parse' {
    It 'parses the 64-bit layout (40-byte entries starting at offset 8)' {
        $b = New-Object byte[] (8 + 2 * 40)
        Set-Int32 $b 0 2
        Set-Tag $b 8 ([Text.Encoding]::ASCII.GetBytes('Tcp '))
        Set-Int32 $b 12 10; Set-Int32 $b 16 3; Set-Int64 $b 24 5000000000
        Set-Int32 $b 32 -1; Set-Int32 $b 36 7; Set-Int64 $b 40 4096
        Set-Tag $b 48 ([byte[]](0x41, 0x01, 0x42, 0x43))
        $entries = @([CtsReliability.PoolTagReader]::Parse($b, $true))
        $entries.Count | Should -Be 2
        $entries[0].Tag | Should -Be 'Tcp '
        $entries[0].PagedAllocs | Should -Be 10
        $entries[0].PagedFrees | Should -Be 3
        $entries[0].PagedUsed | Should -Be 5000000000
        $entries[0].NonPagedAllocs | Should -Be 4294967295 -Because 'counters are unsigned 32-bit'
        $entries[0].NonPagedFrees | Should -Be 7
        $entries[0].NonPagedUsed | Should -Be 4096
        $entries[1].Tag | Should -Be 'A.BC' -Because 'non-printable tag bytes render as dots'
    }

    It 'parses the 32-bit layout (28-byte entries starting at offset 4)' {
        $b = New-Object byte[] (4 + 28)
        Set-Int32 $b 0 1
        Set-Tag $b 4 ([Text.Encoding]::ASCII.GetBytes('Udp '))
        Set-Int32 $b 8 5; Set-Int32 $b 12 2; Set-Int32 $b 16 1024
        Set-Int32 $b 20 9; Set-Int32 $b 24 4; Set-Int32 $b 28 2048
        $e = @([CtsReliability.PoolTagReader]::Parse($b, $false))[0]
        $e.Tag | Should -Be 'Udp '
        $e.PagedAllocs, $e.PagedFrees, $e.PagedUsed | Should -Be @(5, 2, 1024)
        $e.NonPagedAllocs, $e.NonPagedFrees, $e.NonPagedUsed | Should -Be @(9, 4, 2048)
    }

    It 'returns nothing for an empty table' {
        @([CtsReliability.PoolTagReader]::Parse((New-Object byte[] 8), $true)).Count | Should -Be 0
    }
}

Describe 'Get-CtsPoolTagSnapshot' {
    It 'refuses to overwrite an existing CSV' {
        $path = Join-Path $TestDrive 'exists.csv'; Set-Content $path 'x'
        { Get-CtsPoolTagSnapshot -Path $path } | Should -Throw -ExpectedMessage '*already exists*'
    }

    It 'requires the CSV directory to exist' {
        { Get-CtsPoolTagSnapshot -Path (Join-Path $TestDrive 'missing\pool.csv') } | Should -Throw -ExpectedMessage '*does not exist*'
    }

    It 'captures from a guest session, sorts by tag, and writes the CSV' {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand {
            [pscustomobject]@{ Tag = 'Zz  '; PagedAllocs = 1; PagedFrees = 0; PagedDiff = 1; PagedUsed = 64; NonPagedAllocs = 0; NonPagedFrees = 0; NonPagedDiff = 0; NonPagedUsed = 0 }
            [pscustomobject]@{ Tag = 'Aa  '; PagedAllocs = 2; PagedFrees = 1; PagedDiff = 1; PagedUsed = 32; NonPagedAllocs = 3; NonPagedFrees = 1; NonPagedDiff = 2; NonPagedUsed = 128 }
        } -ParameterFilter { $Operation -eq 'PoolTagSnapshot' }
        $guest = [pscustomobject]@{ VMName = 'ctsRel-A'; Channel = 'Direct'; Target = 'ctsRel-A'; Session = $null; LogPath = $null }
        $csv = Join-Path $TestDrive 'guest.csv'
        $rows = Get-CtsPoolTagSnapshot -GuestSession $guest -Path $csv
        @($rows.Tag) | Should -Be @('Aa  ', 'Zz  ')
        $read = @(Import-Csv $csv)
        $read.Count | Should -Be 2
        $read[0].PagedUsedBytes | Should -Be '32'
        $read[0].NonPagedUsedBytes | Should -Be '128'
        $read[0].PagedAllocsCount | Should -Be '2'
        $read[0].PagedFreesCount | Should -Be '1'
        $read[0].PagedOutstandingCount | Should -Be '1'
        $read[0].NonPagedOutstandingCount | Should -Be '2'
        $rows[0].NonPagedUsed | Should -Be 128
        (Get-Content $csv -TotalCount 1) | Should -Not -BeLike '#TYPE*'
    }

    It 'explains that elevation is required when only the placeholder entry is returned' -Skip:$script:IsElevated {
        { Get-CtsPoolTagSnapshot } | Should -Throw -ExpectedMessage '*elevated*'
    }

    It 'captures this machine''s pool tags when elevated' -Tag 'Live', 'Elevated' -Skip:(-not $script:IsElevated) {
        $csv = Join-Path $TestDrive 'local.csv'
        $rows = Get-CtsPoolTagSnapshot -Path $csv
        $rows.Count | Should -BeGreaterThan 100
        ($rows | Measure-Object NonPagedUsed -Sum).Sum | Should -BeGreaterThan 1MB
        @(Compare-CtsPoolTagSnapshot -Reference $csv -Difference $csv | Where-Object TotalUsedDelta -ne 0) | Should -BeNullOrEmpty
    }
}

Describe 'Compare-CtsPoolTagSnapshot' {
    BeforeAll {
        $script:Before = @((New-Row 'Tcp ' 1000 5000 1 10), (New-Row 'Gone' 100 0), (New-Row 'Flat' 50 50))
        $script:After = @((New-Row 'Tcp ' 1000 9000 1 30), (New-Row 'New ' 0 70000), (New-Row 'Flat' 50 50))
    }

    It 'orders tags by total growth and computes outstanding allocation growth' {
        $d = @(Compare-CtsPoolTagSnapshot -Reference $script:Before -Difference $script:After)
        @($d.Tag) | Should -Be @('New ', 'Tcp ', 'Flat', 'Gone')
        $tcp = $d | Where-Object Tag -eq 'Tcp '
        $tcp.NonPagedUsedDelta | Should -Be 4000
        $tcp.OutstandingAllocsDelta | Should -Be 20
        ($d | Where-Object Tag -eq 'New ').OnlyIn | Should -Be 'Difference'
        ($d | Where-Object Tag -eq 'Gone').OnlyIn | Should -Be 'Reference'
        ($d | Where-Object Tag -eq 'Gone').TotalUsedDelta | Should -Be -100
    }

    It 'filters with -MinimumBytes' {
        @((Compare-CtsPoolTagSnapshot -Reference $script:Before -Difference $script:After -MinimumBytes 65536).Tag) | Should -Be @('New ')
    }

    It 'accepts CSV paths written by Get-CtsPoolTagSnapshot' {
        $a = Join-Path $TestDrive 'a.csv'; $b = Join-Path $TestDrive 'b.csv'
        $script:Before | Export-TestCsv -Path $a
        $script:After | Export-TestCsv -Path $b
        @((Compare-CtsPoolTagSnapshot -Reference $a -Difference $b -MinimumBytes 1).Tag) | Should -Be @('New ', 'Tcp ')
    }

    It 'compares a legacy CSV with a unit-labeled capture without changing byte or count values' {
        $legacy = Join-Path $TestDrive 'legacy.csv'
        $labeled = Join-Path $TestDrive 'labeled.csv'
        $script:Before | Export-TestCsv -Path $legacy
        $script:After | Select-Object Tag,
            @{ Name = 'PagedUsedBytes'; Expression = { $_.PagedUsed } },
            @{ Name = 'NonPagedUsedBytes'; Expression = { $_.NonPagedUsed } },
            @{ Name = 'PagedOutstandingCount'; Expression = { $_.PagedDiff } },
            @{ Name = 'NonPagedOutstandingCount'; Expression = { $_.NonPagedDiff } } | Export-TestCsv -Path $labeled
        $tcp = Compare-CtsPoolTagSnapshot -Reference $legacy -Difference $labeled | Where-Object Tag -eq 'Tcp '
        $tcp.NonPagedUsedDelta | Should -Be 4000
        $tcp.OutstandingAllocsDelta | Should -Be 20
        $reverse = Compare-CtsPoolTagSnapshot -Reference $labeled -Difference $legacy -MinimumBytes -100000 |
            Where-Object Tag -eq 'Tcp '
        $reverse.NonPagedUsedDelta | Should -Be -4000
        $reverse.OutstandingAllocsDelta | Should -Be -20
    }
}
