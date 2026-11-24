#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Tests for the prototype inventory scripts in TestScripts\ (device profile and scenario template).
# They read local CIM inventory only and write into TestDrive, so they are safe to run unelevated.

BeforeAll {
    $script:ScriptsRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $script:Profile = Join-Path $script:ScriptsRoot 'ctsTraffic_reliability_capture_device_profile.ps1'
    $script:Scenario = Join-Path $script:ScriptsRoot 'ctsTraffic_reliability_populate_template.ps1'
}

Describe 'ctsTraffic_reliability_capture_device_profile.ps1' {
    It 'fills every template token with local inventory' {
        $out = Join-Path $TestDrive 'profile.md'
        & $script:Profile -SystemClass desktop -DeviceRole host -OutputPath $out | Out-Null
        $text = Get-Content -LiteralPath $out -Raw
        $text | Should -Not -Match '\{\{[A-Z0-9_]+\}\}'
        $text | Should -Match ([regex]::Escape($env:COMPUTERNAME))
        $text | Should -Match 'host'
    }
    It 'uses -DeviceId when supplied' {
        $out = Join-Path $TestDrive 'profile-id.md'
        & $script:Profile -SystemClass server -DeviceRole DUT -OutputPath $out -DeviceId 'dut-01' | Out-Null
        Get-Content -LiteralPath $out -Raw | Should -Match 'dut-01'
    }
    It 'refuses to overwrite an existing file' {
        $out = Join-Path $TestDrive 'exists.md'
        'keep' | Set-Content -LiteralPath $out
        { & $script:Profile -SystemClass desktop -DeviceRole host -OutputPath $out } | Should -Throw '*already exists*'
        Get-Content -LiteralPath $out | Should -Be 'keep'
    }
    It 'refuses a missing output directory' {
        { & $script:Profile -SystemClass desktop -DeviceRole host -OutputPath (Join-Path $TestDrive 'no\such\p.md') } | Should -Throw '*does not exist*'
    }
    It 'rejects an unknown system class' {
        { & $script:Profile -SystemClass laptop -DeviceRole host -OutputPath (Join-Path $TestDrive 'x.md') } | Should -Throw
    }
    It 'fails when no processor inventory is returned' {
        Mock Get-CimInstance { if ($ClassName -eq 'Win32_Processor') { @() } else { [pscustomobject]@{ Name = 'n' } } }
        { & $script:Profile -SystemClass desktop -DeviceRole host -OutputPath (Join-Path $TestDrive 'np.md') } | Should -Throw '*No processor*'
        Test-Path (Join-Path $TestDrive 'np.md') | Should -BeFalse
    }
}

Describe 'ctsTraffic_reliability_populate_template.ps1' {
    It 'fills every template token and records the TBD image path' {
        $out = Join-Path $TestDrive 'scenario.md'
        & $script:Scenario -SystemClass desktop -OutputPath $out | Out-Null
        $text = Get-Content -LiteralPath $out -Raw
        $text | Should -Not -Match '\{\{[A-Z0-9_]+\}\}'
        $text | Should -Match 'Hyper-V'
        $text | Should -Match 'TBD'
    }
    It 'records a resolved VHDX path without modifying it' {
        $vhdx = Join-Path $TestDrive 'image.vhdx'
        'x' | Set-Content -LiteralPath $vhdx
        $before = (Get-Item $vhdx).LastWriteTimeUtc
        $out = Join-Path $TestDrive 'scenario-vhdx.md'
        & $script:Scenario -SystemClass server -OutputPath $out -VhdxPath $vhdx -GuestImage 'Win11 test' | Out-Null
        $text = Get-Content -LiteralPath $out -Raw
        $text | Should -Match ([regex]::Escape((Get-Item $vhdx).FullName))
        $text | Should -Match 'Win11 test'
        (Get-Item $vhdx).LastWriteTimeUtc | Should -Be $before
    }
    It 'rejects a VhdxPath that is not a .vhdx file' {
        $iso = Join-Path $TestDrive 'image.iso'
        'x' | Set-Content -LiteralPath $iso
        { & $script:Scenario -SystemClass desktop -OutputPath (Join-Path $TestDrive 's2.md') -VhdxPath $iso } | Should -Throw '*.vhdx*'
    }
    It 'refuses to overwrite an existing file' {
        $out = Join-Path $TestDrive 's-exists.md'
        'keep' | Set-Content -LiteralPath $out
        { & $script:Scenario -SystemClass desktop -OutputPath $out } | Should -Throw '*already exists*'
    }
}
