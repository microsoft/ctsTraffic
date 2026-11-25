#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Tests for the thin entry scripts in TestScripts\Reliability. Module functions are mocked; these tests prove
# each script loads the lab definition, passes the right arguments, and plans by default.

BeforeAll {
    $script:Root = Split-Path $PSScriptRoot -Parent
    Import-Module (Join-Path $script:Root 'CtsReliability.psm1') -Force
    function Invoke-Entry([string]$Name, [hashtable]$Arguments = @{}) { & (Join-Path $script:Root $Name) @Arguments }
    function New-TestCredential([string]$User = 'ctsadmin', [string]$Password = 'unused-Pw1') {
        $secure = New-Object System.Security.SecureString
        foreach ($ch in $Password.ToCharArray()) { $secure.AppendChar($ch) }
        New-Object System.Management.Automation.PSCredential($User, $secure)
    }
}

Describe 'Entry script coverage' {
    It 'every module source file has a test file' {
        $missing = Get-ChildItem (Join-Path $script:Root 'src') -Filter *.ps1 |
            Where-Object { -not (Test-Path (Join-Path $PSScriptRoot ($_.BaseName + '.Tests.ps1'))) }
        @($missing).Count | Should -Be 0 -Because "untested: $(@($missing.Name) -join ', ')"
    }
    It 'every entry script is exercised by this file' {
        $text = Get-Content -LiteralPath $PSCommandPath -Raw
        $missing = Get-ChildItem $script:Root -Filter *.ps1 | Where-Object { $text -notmatch [regex]::Escape("'$($_.Name)'") }
        @($missing).Count | Should -Be 0 -Because "untested: $(@($missing.Name) -join ', ')"
    }
    It 'every entry script parses and has a synopsis' {
        foreach ($file in Get-ChildItem $script:Root -Filter *.ps1) {
            $tokens = $null; $errors = $null
            $ast = [System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$errors)
            @($errors).Count | Should -Be 0 -Because $file.Name
            $ast.GetHelpContent().Synopsis | Should -Not -BeNullOrEmpty -Because $file.Name
        }
    }
}

Describe 'Set-CtsGuestCredential.ps1' {
    BeforeEach { Mock Set-CtsStoredCredential { } }

    It 'stores the credential under the configured target without echoing the password' {
        $out = Invoke-Entry 'Set-CtsGuestCredential.ps1' @{ Credential = (New-TestCredential) }
        Should -Invoke Set-CtsStoredCredential -Times 1 -Exactly -ParameterFilter { $Target -eq 'ctsReliability/guest' -and $Credential.UserName -eq 'ctsadmin' }
        "$out" | Should -Match 'ctsReliability/guest'
        "$out" | Should -Not -Match 'unused-Pw1'
    }
    It 'accepts the .\ local-account prefix' {
        Invoke-Entry 'Set-CtsGuestCredential.ps1' @{ Credential = (New-TestCredential '.\ctsadmin') } | Out-Null
        Should -Invoke Set-CtsStoredCredential -Times 1 -Exactly
    }
    It 'prompts with the configured user name when no credential is supplied' {
        Mock Get-Credential { New-TestCredential }
        Invoke-Entry 'Set-CtsGuestCredential.ps1' | Out-Null
        Should -Invoke Get-Credential -Times 1 -Exactly -ParameterFilter { $UserName -eq 'ctsadmin' }
        Should -Invoke Set-CtsStoredCredential -Times 1 -Exactly
    }
    It 'rejects a different user name' {
        { Invoke-Entry 'Set-CtsGuestCredential.ps1' @{ Credential = (New-TestCredential 'someone') } } | Should -Throw '*does not match*'
        Should -Invoke Set-CtsStoredCredential -Times 0
    }
    It 'rejects an empty password' {
        $empty = New-Object System.Management.Automation.PSCredential('ctsadmin', (New-Object System.Security.SecureString))
        { Invoke-Entry 'Set-CtsGuestCredential.ps1' @{ Credential = $empty } } | Should -Throw '*empty*'
        Should -Invoke Set-CtsStoredCredential -Times 0
    }
    It 'writes nothing with -WhatIf' {
        Invoke-Entry 'Set-CtsGuestCredential.ps1' @{ Credential = (New-TestCredential); WhatIf = $true } | Out-Null
        Should -Invoke Set-CtsStoredCredential -Times 0
    }
}

Describe 'New-CtsGoldenImage.ps1' {
    BeforeEach { Mock New-CtsGoldenImage { 'result' } }

    It 'plans by default' {
        Invoke-Entry 'New-CtsGoldenImage.ps1' @{ IsoPath = 'D:\x.iso' } | Should -Be 'result'
        Should -Invoke New-CtsGoldenImage -Times 1 -Exactly -ParameterFilter { $IsoPath -eq 'D:\x.iso' -and -not $Apply -and $Definition.labId -eq 'ctsrel-v1' }
    }
    It 'passes -Apply and -LogPath' {
        Invoke-Entry 'New-CtsGoldenImage.ps1' @{ IsoPath = 'D:\x.iso'; Apply = $true; LogPath = 'C:\l.jsonl' } | Out-Null
        Should -Invoke New-CtsGoldenImage -Times 1 -Exactly -ParameterFilter { $Apply -and $LogPath -eq 'C:\l.jsonl' }
    }
    It 'loads an alternate lab definition' {
        $path = Join-Path $TestDrive 'lab.json'
        $json = Get-Content (Join-Path $script:Root 'lab.v1.json') -Raw | ConvertFrom-Json
        $json.labId = 'ctsrel-alt'
        $json | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path
        Invoke-Entry 'New-CtsGoldenImage.ps1' @{ IsoPath = 'D:\x.iso'; LabDefinitionPath = $path } | Out-Null
        Should -Invoke New-CtsGoldenImage -Times 1 -Exactly -ParameterFilter { $Definition.labId -eq 'ctsrel-alt' }
    }
    It 'defaults to the cached ISO' {
        Mock Get-CtsIsoCacheEntry { [pscustomobject]@{ isoPath = 'C:\ctsReliability\iso\win11-arm64.iso' } }
        Invoke-Entry 'New-CtsGoldenImage.ps1' | Out-Null
        Should -Invoke New-CtsGoldenImage -Times 1 -Exactly -ParameterFilter { $IsoPath -eq 'C:\ctsReliability\iso\win11-arm64.iso' }
    }
    It 'explains how to get an ISO when none is cached' {
        Mock Get-CtsIsoCacheEntry { $null }
        { Invoke-Entry 'New-CtsGoldenImage.ps1' } | Should -Throw '*Get-CtsWindowsIso.ps1*'
        Should -Invoke New-CtsGoldenImage -Times 0
    }
}

Describe 'Set-CtsLocalConfig.ps1' {
    BeforeEach {
        Mock Set-CtsLocalConfig { 'plan' }
        Mock Get-CtsHostArchitecture { 'arm64' }
        Mock Read-Host { '"\\server.example\share\dbg_arm64.msi" ' }
    }

    It 'prompts for this host architecture when no source is given' {
        Invoke-Entry 'Set-CtsLocalConfig.ps1' | Should -Be 'plan'
        Should -Invoke Read-Host -Times 1 -Exactly
        Should -Invoke Set-CtsLocalConfig -Times 1 -Exactly -ParameterFilter {
            $DebuggerSourceArm64 -eq '\\server.example\share\dbg_arm64.msi' -and -not $DebuggerSourceAmd64 -and -not $Apply -and $Definition.labId -eq 'ctsrel-v1'
        }
    }
    It 'passes given sources and -Apply without prompting' {
        Invoke-Entry 'Set-CtsLocalConfig.ps1' @{ DebuggerSourceAmd64 = '\\s\dbg_amd64.msi'; Apply = $true } | Out-Null
        Should -Invoke Read-Host -Times 0
        Should -Invoke Set-CtsLocalConfig -Times 1 -Exactly -ParameterFilter { $DebuggerSourceAmd64 -eq '\\s\dbg_amd64.msi' -and $Apply -and -not $PSBoundParameters.ContainsKey('LabDefinitionPath') }
    }
}

Describe 'Get-CtsWindowsIso.ps1' {
    BeforeEach { Mock Get-CtsWindowsIso { 'plan' } }

    It 'plans by default and passes only the definition' {
        Invoke-Entry 'Get-CtsWindowsIso.ps1' | Should -Be 'plan'
        Should -Invoke Get-CtsWindowsIso -Times 1 -Exactly -ParameterFilter {
            -not $Apply -and $Definition.labId -eq 'ctsrel-v1' -and -not $PSBoundParameters.ContainsKey('Source') -and -not $PSBoundParameters.ContainsKey('LabDefinitionPath')
        }
    }
    It 'passes the source, hash, and switches' {
        Invoke-Entry 'Get-CtsWindowsIso.ps1' @{ Source = 'https://x/y.iso'; ExpectedSha256 = ('a' * 64); Refresh = $true; Apply = $true } | Out-Null
        Should -Invoke Get-CtsWindowsIso -Times 1 -Exactly -ParameterFilter { $Source -eq 'https://x/y.iso' -and $ExpectedSha256 -eq ('a' * 64) -and $Refresh -and $Apply -and -not $AllowUnverified }
    }
}

Describe 'Invoke-CtsLabProvision.ps1' {
    BeforeEach { Mock Invoke-CtsLabProvision { } }

    It 'plans by default and passes only the definition' {
        Invoke-Entry 'Invoke-CtsLabProvision.ps1' | Out-Null
        Should -Invoke Invoke-CtsLabProvision -Times 1 -Exactly -ParameterFilter {
            -not $Apply -and $Definition.labId -eq 'ctsrel-v1' -and -not $PSBoundParameters.ContainsKey('VmCount') -and -not $PSBoundParameters.ContainsKey('LabDefinitionPath')
        }
    }
    It 'passes through apply options' {
        Invoke-Entry 'Invoke-CtsLabProvision.ps1' @{ Apply = $true; VmCount = 3; SkipSnapshot = $true; GuestReadyTimeoutSeconds = 60 } | Out-Null
        Should -Invoke Invoke-CtsLabProvision -Times 1 -Exactly -ParameterFilter { $Apply -and $VmCount -eq 3 -and $SkipSnapshot -and $GuestReadyTimeoutSeconds -eq 60 }
    }
}

Describe 'New-CtsSystemSnapshot.ps1' {
    BeforeEach {
        Mock New-CtsSnapshot { }
        Mock Get-CtsStoredCredential { New-TestCredential }
    }

    It 'uses the stored guest credential' {
        Invoke-Entry 'New-CtsSystemSnapshot.ps1' @{ Channel = 'WinRM' } | Out-Null
        Should -Invoke Get-CtsStoredCredential -Times 1 -Exactly -ParameterFilter { $Target -eq 'ctsReliability/guest' }
        Should -Invoke New-CtsSnapshot -Times 1 -Exactly -ParameterFilter { $Channel -eq 'WinRM' -and $Credential.UserName -eq 'ctsadmin' }
    }
    It 'does not need a credential for -HostOnly' {
        Invoke-Entry 'New-CtsSystemSnapshot.ps1' @{ HostOnly = $true; OutputRoot = 'C:\o' } | Out-Null
        Should -Invoke Get-CtsStoredCredential -Times 0
        Should -Invoke New-CtsSnapshot -Times 1 -Exactly -ParameterFilter { $HostOnly -and $OutputRoot -eq 'C:\o' -and $null -eq $Credential }
    }
}

Describe 'Compare-CtsSystemSnapshot.ps1' {
    BeforeEach {
        foreach ($n in 'a', 'b') {
            $d = Join-Path $TestDrive $n
            New-Item -ItemType Directory -Path $d -Force | Out-Null
            "{ ""snapshotId"": ""$n"" }" | Set-Content -LiteralPath (Join-Path $d 'snapshot.json')
        }
        Mock Test-CtsSnapshot { }
        Mock Compare-CtsSnapshot { 'drift' }
    }

    It 'validates both snapshots then compares them' {
        Invoke-Entry 'Compare-CtsSystemSnapshot.ps1' @{ Reference = (Join-Path $TestDrive 'a'); Difference = (Join-Path $TestDrive 'b\snapshot.json') } | Should -Be 'drift'
        Should -Invoke Test-CtsSnapshot -Times 2 -Exactly
        Should -Invoke Compare-CtsSnapshot -Times 1 -Exactly -ParameterFilter {
            $Reference.snapshotId -eq 'a' -and $Difference.snapshotId -eq 'b' -and -not $PSBoundParameters.ContainsKey('IgnorePath')
        }
    }
    It 'adds -IgnorePath to the default ignore list' {
        Invoke-Entry 'Compare-CtsSystemSnapshot.ps1' @{ Reference = (Join-Path $TestDrive 'a'); Difference = (Join-Path $TestDrive 'b'); IgnorePath = 'extra.*' } | Out-Null
        Should -Invoke Compare-CtsSnapshot -Times 1 -Exactly -ParameterFilter { $IgnorePath -contains 'extra.*' -and @($IgnorePath).Count -gt 1 }
    }
    It 'refuses an invalid snapshot' {
        Mock Test-CtsSnapshot { 'hash mismatch snapshot.json' }
        { Invoke-Entry 'Compare-CtsSystemSnapshot.ps1' @{ Reference = (Join-Path $TestDrive 'a'); Difference = (Join-Path $TestDrive 'b') } } | Should -Throw '*hash mismatch*'
        Should -Invoke Compare-CtsSnapshot -Times 0
    }
}

Describe 'Get-CtsPoolTagSnapshot.ps1' {
    It 'passes the output path' {
        Mock Get-CtsPoolTagSnapshot { }
        Invoke-Entry 'Get-CtsPoolTagSnapshot.ps1' @{ Path = 'C:\p.csv' } | Out-Null
        Should -Invoke Get-CtsPoolTagSnapshot -Times 1 -Exactly -ParameterFilter { $Path -eq 'C:\p.csv' -and $null -eq $GuestSession }
    }
}

Describe 'Compare-CtsPoolTagSnapshot.ps1' {
    BeforeEach {
        Mock Compare-CtsPoolTagSnapshot { 'growth' }
        'Tag' | Set-Content (Join-Path $TestDrive 'r.csv')
        'Tag' | Set-Content (Join-Path $TestDrive 'd.csv')
    }

    It 'compares two capture files' {
        Invoke-Entry 'Compare-CtsPoolTagSnapshot.ps1' @{ Reference = (Join-Path $TestDrive 'r.csv'); Difference = (Join-Path $TestDrive 'd.csv'); MinimumBytes = 4096 } | Should -Be 'growth'
        Should -Invoke Compare-CtsPoolTagSnapshot -Times 1 -Exactly -ParameterFilter { $MinimumBytes -eq 4096 -and $Reference -like '*r.csv' }
    }
    It 'fails clearly when a capture is missing' {
        { Invoke-Entry 'Compare-CtsPoolTagSnapshot.ps1' @{ Reference = (Join-Path $TestDrive 'r.csv'); Difference = (Join-Path $TestDrive 'none.csv') } } | Should -Throw '*not found*'
    }
}

Describe 'Update-CtsDebuggerCache.ps1' {
    BeforeEach {
        Mock Update-CtsDebuggerCache { }
        Mock Get-CtsHostArchitecture { 'arm64' }
    }

    It 'defaults to the host architecture' {
        Invoke-Entry 'Update-CtsDebuggerCache.ps1' | Out-Null
        Should -Invoke Update-CtsDebuggerCache -Times 1 -Exactly -ParameterFilter { $Architecture -eq 'arm64' }
    }
    It 'refreshes each requested architecture' {
        Invoke-Entry 'Update-CtsDebuggerCache.ps1' @{ Architecture = @('amd64', 'arm64') } | Out-Null
        Should -Invoke Update-CtsDebuggerCache -Times 2 -Exactly
        Should -Invoke Update-CtsDebuggerCache -Times 1 -Exactly -ParameterFilter { $Architecture -eq 'amd64' }
    }
}
