#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Tests for src\IsoCache.ps1. Local-file sources are real copies into TestDrive; https transfers are mocked.

BeforeAll {
    Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'CtsReliability.psm1') -Force
    function New-TestDefinition([string]$PinnedHash) {
        $d = Get-CtsLabDefinition
        $d.labRoot = Join-Path $TestDrive ([guid]::NewGuid().ToString('N'))
        $d.goldenImage.iso.sha256.(Get-CtsHostArchitecture) = $PinnedHash
        $d
    }
    function New-SourceFile([string]$Content = 'fake iso content') {
        $path = Join-Path $TestDrive ("src-{0}.iso" -f [guid]::NewGuid().ToString('N'))
        [IO.File]::WriteAllText($path, $Content)
        [pscustomobject]@{ Path = $path; Hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
    }
    $script:Url = 'https://software.download.prss.microsoft.com/dbazure/Win11_ARM64.iso?t=SECRET-TOKEN&e=123'
}

Describe 'ConvertTo-CtsSafeSource' {
    It 'removes the query string and fragment from URLs' {
        InModuleScope CtsReliability -Parameters @{ Url = $script:Url } {
            ConvertTo-CtsSafeSource $Url | Should -Be 'https://software.download.prss.microsoft.com/dbazure/Win11_ARM64.iso'
        }
    }
    It 'leaves file paths unchanged' {
        InModuleScope CtsReliability { ConvertTo-CtsSafeSource '\\share\iso\a#b.iso' | Should -Be '\\share\iso\a#b.iso' }
    }
}

Describe 'Architecture-specific file names and hashes' {
    It 'names the <Arch> ISO and golden image for that architecture' -ForEach @(@{ Arch = 'amd64' }, @{ Arch = 'arm64' }) {
        InModuleScope CtsReliability -Parameters @{ Arch = $Arch } {
            param($Arch)
            $def = Get-CtsLabDefinition
            Split-Path -Leaf (Get-CtsIsoPath -Definition $def -Architecture $Arch).Iso | Should -Be "win11-$Arch.iso"
            Split-Path -Leaf (Get-CtsGoldenImagePath -Definition $def -Architecture $Arch).Vhdx | Should -Be "golden-win11-$Arch.vhdx"
        }
    }
    It 'pins only the hash for the requested architecture' {
        InModuleScope CtsReliability {
            $def = Get-CtsLabDefinition
            $def.goldenImage.iso.sha256.amd64 = 'a' * 64
            Get-CtsPinnedIsoHash -Definition $def -Architecture amd64 | Should -Be ('A' * 64)
            Get-CtsPinnedIsoHash -Definition $def -Architecture arm64 | Should -BeNullOrEmpty
        }
    }
}

Describe 'Get-CtsWindowsIso plan' {
    It 'blocks when nothing is cached and no source is given' {
        $plan = Get-CtsWindowsIso -Definition (New-TestDefinition)
        $plan.Blocked | Should -BeTrue
        $plan.Reasons -join ' ' | Should -Match 'no -Source'
    }
    It 'plans a download from https and never shows the URL token' {
        $plan = Get-CtsWindowsIso -Definition (New-TestDefinition) -Source $script:Url -ExpectedSha256 ('a' * 64)
        $plan.Action | Should -Be 'Download'
        $plan.Source | Should -Not -Match 'SECRET'
        $plan.ExpectedSha256 | Should -Be ('A' * 64)
    }
    It 'rejects plain http' {
        $plan = Get-CtsWindowsIso -Definition (New-TestDefinition) -Source 'http://example.com/x.iso' -ExpectedSha256 ('a' * 64)
        $plan.Reasons -join ' ' | Should -Match 'Only https'
    }
    It 'blocks without an expected hash unless -AllowUnverified' {
        $src = New-SourceFile
        (Get-CtsWindowsIso -Definition (New-TestDefinition) -Source $src.Path).Reasons -join ' ' | Should -Match 'No expected SHA-256'
        (Get-CtsWindowsIso -Definition (New-TestDefinition) -Source $src.Path -AllowUnverified).Action | Should -Be 'Download'
    }
    It 'uses the hash pinned in the lab definition' {
        $src = New-SourceFile
        (Get-CtsWindowsIso -Definition (New-TestDefinition $src.Hash) -Source $src.Path).ExpectedSha256 | Should -Be $src.Hash
    }
    It 'blocks a malformed hash or one that contradicts the pinned hash' {
        $src = New-SourceFile
        (Get-CtsWindowsIso -Definition (New-TestDefinition) -Source $src.Path -ExpectedSha256 'xyz').Blocked | Should -BeTrue
        (Get-CtsWindowsIso -Definition (New-TestDefinition ('b' * 64)) -Source $src.Path -ExpectedSha256 ('c' * 64)).Reasons -join ' ' | Should -Match 'differs from goldenImage.iso.sha256'
    }
    It 'blocks a missing source file' {
        (Get-CtsWindowsIso -Definition (New-TestDefinition) -Source (Join-Path $TestDrive 'none.iso') -AllowUnverified).Reasons -join ' ' | Should -Match 'not found'
    }
    It 'creates nothing in plan mode' {
        $def = New-TestDefinition; $src = New-SourceFile
        $null = Get-CtsWindowsIso -Definition $def -Source $src.Path -ExpectedSha256 $src.Hash
        Test-Path $def.labRoot | Should -BeFalse
    }
}

Describe 'Get-CtsWindowsIso -Apply' {
    It 'caches a verified copy read-only with metadata and no partial file' {
        $def = New-TestDefinition; $src = New-SourceFile
        $log = Join-Path $TestDrive 'iso.jsonl'
        $entry = Get-CtsWindowsIso -Definition $def -Source $src.Path -ExpectedSha256 $src.Hash -Apply -LogPath $log
        $entry.sha256 | Should -Be $src.Hash
        $entry.verification | Should -Be 'matchedExpected'
        (Get-Item $entry.isoPath).IsReadOnly | Should -BeTrue
        Test-Path "$($entry.isoPath).partial" | Should -BeFalse
        (Get-CtsIsoCacheEntry -Definition $def).sha256 | Should -Be $src.Hash
    }
    It 'records an unverified copy as unverified' {
        $src = New-SourceFile
        (Get-CtsWindowsIso -Definition (New-TestDefinition) -Source $src.Path -AllowUnverified -Apply).verification | Should -Be 'unverified'
    }
    It 'rejects a hash mismatch and leaves nothing behind' {
        $def = New-TestDefinition; $src = New-SourceFile
        { Get-CtsWindowsIso -Definition $def -Source $src.Path -ExpectedSha256 ('0' * 64) -Apply } | Should -Throw '*does not match the expected*'
        Get-ChildItem (Join-Path $def.labRoot 'iso') -File | Should -BeNullOrEmpty
        Get-CtsIsoCacheEntry -Definition $def | Should -BeNullOrEmpty
    }
    It 'downloads https sources through the transfer helper and never records the token' {
        $def = New-TestDefinition
        $content = 'downloaded iso'
        $tmp = Join-Path $TestDrive 'hash-me.bin'; [IO.File]::WriteAllText($tmp, $content)
        $hash = (Get-FileHash $tmp -Algorithm SHA256).Hash
        Mock -ModuleName CtsReliability Save-CtsIsoFile { [IO.File]::WriteAllText($Destination, 'downloaded iso') }
        $log = Join-Path $TestDrive 'https.jsonl'
        $entry = Get-CtsWindowsIso -Definition $def -Source $script:Url -ExpectedSha256 $hash -Apply -LogPath $log
        Should -Invoke -ModuleName CtsReliability Save-CtsIsoFile -Times 1 -Exactly -ParameterFilter { $Source -eq $script:Url }
        $entry.source | Should -Not -Match 'SECRET'
        Get-Content (Join-Path $def.labRoot "iso\win11-$(Get-CtsHostArchitecture).iso.json") -Raw | Should -Not -Match 'SECRET'
        Get-Content $log -Raw | Should -Not -Match 'SECRET'
    }
    It 'deletes a leftover partial file before downloading' {
        $def = New-TestDefinition; $src = New-SourceFile
        $partial = Join-Path $def.labRoot "iso\win11-$(Get-CtsHostArchitecture).iso.partial"
        New-Item -ItemType Directory -Path (Split-Path $partial) -Force | Out-Null
        'stale' | Set-Content $partial
        (Get-CtsWindowsIso -Definition $def -Source $src.Path -ExpectedSha256 $src.Hash).Notes -join ' ' | Should -Match 'will be deleted'
        $null = Get-CtsWindowsIso -Definition $def -Source $src.Path -ExpectedSha256 $src.Hash -Apply
        Test-Path $partial | Should -BeFalse
    }
}

Describe 'Get-CtsWindowsIso with an existing cache' {
    BeforeEach {
        $script:Def = New-TestDefinition
        $script:First = New-SourceFile 'first'
        $script:Entry = Get-CtsWindowsIso -Definition $script:Def -Source $script:First.Path -ExpectedSha256 $script:First.Hash -Apply
    }

    It 'uses the cache, ignores a new source without -Refresh, and says so' {
        $second = New-SourceFile 'second'
        $plan = Get-CtsWindowsIso -Definition $script:Def -Source $second.Path -AllowUnverified
        $plan.Action | Should -Be 'UseCached'
        $plan.Notes -join ' ' | Should -Match '-Refresh'
        (Get-CtsWindowsIso -Definition $script:Def -Source $second.Path -AllowUnverified -Apply).sha256 | Should -Be $script:First.Hash
    }
    It 'needs no source or hash to reuse the cache' {
        (Get-CtsWindowsIso -Definition $script:Def -Apply).sha256 | Should -Be $script:First.Hash
    }
    It 'detects a cached ISO that changed since it was recorded' {
        Set-ItemProperty $script:Entry.isoPath -Name IsReadOnly -Value $false
        [IO.File]::WriteAllText($script:Entry.isoPath, 'tampered')
        { Get-CtsWindowsIso -Definition $script:Def -Apply } | Should -Throw '*metadata records*'
    }
    It 'blocks when the cached ISO does not match the pinned hash' {
        $script:Def.goldenImage.iso.sha256.(Get-CtsHostArchitecture) = 'd' * 64
        (Get-CtsWindowsIso -Definition $script:Def).Reasons -join ' ' | Should -Match 'does not match the expected'
    }
    It 'replaces the cache with -Refresh and records the replaced hash' {
        $second = New-SourceFile 'second'
        $entry = Get-CtsWindowsIso -Definition $script:Def -Source $second.Path -ExpectedSha256 $second.Hash -Refresh -Apply
        $entry.sha256 | Should -Be $second.Hash
        $entry.replacedSha256 | Should -Be $script:First.Hash
        (Get-Item $entry.isoPath).IsReadOnly | Should -BeTrue
    }
    It 'reports an inconsistent cache when the metadata is missing' {
        Remove-Item "$($script:Entry.isoPath).json"
        (Get-CtsWindowsIso -Definition $script:Def).Reasons -join ' ' | Should -Match 'inconsistent'
    }
}
