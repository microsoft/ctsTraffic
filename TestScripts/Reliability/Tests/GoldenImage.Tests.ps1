#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Unit tests for src\GoldenImage.ps1. Building a real image needs elevation and an ISO, so the Apply path is
# covered only for its guards here; the full build is a manual elevated verification (see tooling-tests.md).

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Password = 'Pester-Unattend-7!<&>'
    $script:Credential = [pscredential]::new('ctsadmin', (ConvertTo-SecureString $script:Password -AsPlainText -Force))
    function New-TestDefinition {
        $d = Get-CtsLabDefinition
        $d.labRoot = Join-Path $TestDrive ([guid]::NewGuid().ToString('n'))
        $null = New-Item -ItemType Directory -Path (Join-Path $d.labRoot 'vhdx') -Force
        return $d
    }
}

Describe 'New-CtsUnattendXml' {
    BeforeAll {
        $script:Text = New-CtsUnattendXml -ComputerName 'CTSREL-A' -Credential $script:Credential -Architecture arm64
        $script:Xml = [xml]$script:Text
        $script:Ns = [System.Xml.XmlNamespaceManager]::new($script:Xml.NameTable)
        $script:Ns.AddNamespace('u', 'urn:schemas-microsoft-com:unattend')
    }

    It 'is well-formed XML with specialize and oobeSystem passes' {
        @($script:Xml.unattend.settings | ForEach-Object pass) | Should -Be @('specialize', 'oobeSystem')
    }

    It 'never contains the plain-text password' {
        $script:Text | Should -Not -Match ([regex]::Escape($script:Password))
        $script:Text | Should -Not -Match ([regex]::Escape([Security.SecurityElement]::Escape($script:Password)))
    }

    It 'encodes the password as Base64(UTF-16LE(password + "Password")) with PlainText false' {
        $node = $script:Xml.SelectSingleNode('//u:LocalAccount/u:Password', $script:Ns)
        $node.PlainText | Should -Be 'false'
        [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($node.Value)) | Should -BeExactly ($script:Password + 'Password')
    }

    It 'creates the account in Administrators with the credential user name' {
        $account = $script:Xml.SelectSingleNode('//u:LocalAccount', $script:Ns)
        $account.Name | Should -Be 'ctsadmin'
        $account.Group | Should -Be 'Administrators'
    }

    It 'sets the computer name and UTC time zone in specialize' {
        $script:Xml.SelectSingleNode('//u:settings[@pass="specialize"]//u:ComputerName', $script:Ns).InnerText | Should -Be 'CTSREL-A'
        $script:Xml.SelectSingleNode('//u:settings[@pass="specialize"]//u:TimeZone', $script:Ns).InnerText | Should -Be 'UTC'
    }

    It 'stamps every component with the requested architecture' {
        $arches = @($script:Xml.SelectNodes('//u:component', $script:Ns) | ForEach-Object processorArchitecture | Select-Object -Unique)
        $arches | Should -Be @('arm64')
        $amd = [xml](New-CtsUnattendXml -ComputerName 'X' -Credential $script:Credential -Architecture amd64)
        @($amd.unattend.settings.component | ForEach-Object processorArchitecture | Select-Object -Unique) | Should -Be @('amd64')
    }

    It 'suppresses the online-account and EULA OOBE pages' {
        $oobe = $script:Xml.SelectSingleNode('//u:OOBE', $script:Ns)
        $oobe.HideOnlineAccountScreens | Should -Be 'true'
        $oobe.HideEULAPage | Should -Be 'true'
    }

    It 'escapes XML-special characters in the user name' {
        $c = [pscredential]::new('a&b', $script:Credential.Password)
        $x = [xml](New-CtsUnattendXml -ComputerName 'X' -Credential $c -Architecture arm64)
        $x.unattend.settings[1].component[1].UserAccounts.LocalAccounts.LocalAccount.Name | Should -Be 'a&b'
    }

    It 'rejects computer names that are invalid or longer than 15 characters' {
        { New-CtsUnattendXml -ComputerName 'ABCDEFGHIJKLMNOP' -Credential $script:Credential -Architecture arm64 } | Should -Throw
        { New-CtsUnattendXml -ComputerName 'bad name' -Credential $script:Credential -Architecture arm64 } | Should -Throw
    }
}

Describe 'New-CtsSetupCompleteCmd' {
    It 'deletes both unattend copies, writes the marker under the guest root, and deletes itself' {
        InModuleScope CtsReliability {
            $cmd = New-CtsSetupCompleteCmd -GuestRootPath 'C:\ctsReliability'
            $cmd | Should -Match ([regex]::Escape('del /f /q "%WINDIR%\Panther\unattend.xml"'))
            $cmd | Should -Match ([regex]::Escape('del /f /q "%WINDIR%\Panther\Unattend\unattend.xml"'))
            $cmd | Should -Match ([regex]::Escape('"C:\ctsReliability\setupcomplete.txt"'))
            $cmd | Should -Match ([regex]::Escape('del /f /q "%~f0"'))
        }
    }
}

Describe 'Get-CtsGoldenImagePlan / New-CtsGoldenImage (plan mode)' {
    BeforeEach {
        $script:Def = New-TestDefinition
        $script:Iso = Join-Path $TestDrive 'fake.iso'
        Set-Content -LiteralPath $script:Iso -Value 'not an iso'
    }

    It 'is not blocked when the ISO exists and nothing is built yet, and reports the target paths' {
        $plan = New-CtsGoldenImage -Definition $script:Def -IsoPath $script:Iso
        $plan.Blocked | Should -BeFalse
        $plan.TargetPath | Should -Be (Join-Path $script:Def.labRoot 'vhdx\golden-win11-arm64.vhdx')
        $plan.MetadataPath | Should -Be (Join-Path $script:Def.labRoot 'vhdx\golden-win11-arm64.json')
        $plan.Steps.Count | Should -BeGreaterThan 3
    }

    It 'creates no files in plan mode' {
        $before = @(Get-ChildItem -LiteralPath $script:Def.labRoot -Recurse -File).Count
        $null = New-CtsGoldenImage -Definition $script:Def -IsoPath $script:Iso
        @(Get-ChildItem -LiteralPath $script:Def.labRoot -Recurse -File).Count | Should -Be $before
    }

    It 'blocks when the ISO is missing' {
        $plan = New-CtsGoldenImage -Definition $script:Def -IsoPath (Join-Path $TestDrive 'missing.iso')
        $plan.Blocked | Should -BeTrue
        $plan.Reasons -join ' ' | Should -Match 'ISO not found'
    }

    It 'blocks rather than overwrite an existing golden image' {
        Set-Content -LiteralPath (Join-Path $script:Def.labRoot 'vhdx\golden-win11-arm64.vhdx') -Value 'x'
        (New-CtsGoldenImage -Definition $script:Def -IsoPath $script:Iso).Reasons -join ' ' | Should -Match 'never overwritten'
    }

    It 'blocks when a failed build left a partial VHDX' {
        Set-Content -LiteralPath (Join-Path $script:Def.labRoot 'vhdx\golden-win11-arm64.partial.vhdx') -Value 'x'
        (New-CtsGoldenImage -Definition $script:Def -IsoPath $script:Iso).Reasons -join ' ' | Should -Match 'failed build'
    }

    It 'blocks when free space is below the minimum' {
        InModuleScope CtsReliability -Parameters @{ D = $script:Def; I = $script:Iso } {
            param($D, $I)
            (Get-CtsGoldenImagePlan -Definition $D -IsoPath $I -MinimumFreeBytes ([long]::MaxValue)).Reasons -join ' ' | Should -Match 'GiB free'
        }
    }
}

Describe 'New-CtsGoldenImage -Apply guards' {
    BeforeEach {
        $script:Def = New-TestDefinition
        $script:Iso = Join-Path $TestDrive 'fake.iso'
        Set-Content -LiteralPath $script:Iso -Value 'not an iso'
        Mock -ModuleName CtsReliability Mount-DiskImage { throw 'must not be called' }
        Mock -ModuleName CtsReliability New-VHD { throw 'must not be called' }
    }

    It 'throws with the reasons when the plan is blocked, before touching anything' {
        { New-CtsGoldenImage -Definition $script:Def -IsoPath (Join-Path $TestDrive 'missing.iso') -Apply } | Should -Throw '*blocked*ISO not found*'
        Should -Invoke -ModuleName CtsReliability Mount-DiskImage -Times 0
    }

    It 'throws when not elevated' {
        Mock -ModuleName CtsReliability Test-CtsElevated { $false }
        { New-CtsGoldenImage -Definition $script:Def -IsoPath $script:Iso -Apply } | Should -Throw '*elevated*'
        Should -Invoke -ModuleName CtsReliability New-VHD -Times 0
    }

    It 'dismounts the ISO and leaves no final VHDX when a build step fails' {
        Mock -ModuleName CtsReliability Test-CtsElevated { $true }
        Mock -ModuleName CtsReliability Get-FileHash { [pscustomobject]@{ Hash = 'ABC' } }
        Mock -ModuleName CtsReliability Mount-DiskImage { [pscustomobject]@{ ImagePath = 'x' } }
        Mock -ModuleName CtsReliability Get-Volume { throw 'simulated mount failure' } -RemoveParameterType DiskImage
        Mock -ModuleName CtsReliability Dismount-DiskImage { }
        $log = Join-Path $TestDrive 'golden.jsonl'
        { New-CtsGoldenImage -Definition $script:Def -IsoPath $script:Iso -Apply -LogPath $log } | Should -Throw '*simulated mount failure*'
        Should -Invoke -ModuleName CtsReliability Dismount-DiskImage -Times 1 -Exactly
        Test-Path -LiteralPath (Join-Path $script:Def.labRoot 'vhdx\golden-win11-arm64.vhdx') | Should -BeFalse
        $entries = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
        ($entries | Where-Object operation -eq 'BuildGoldenImage').success | Should -BeFalse
    }
}
