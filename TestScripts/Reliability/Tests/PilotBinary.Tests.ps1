#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Path = 'C:\ctsReliability\tools\ctsTraffic.exe'
    $script:Hash = 'A' * 64
    function New-BinaryDevice {
        [pscustomobject]@{
            role = 'guest'; status = 'complete'; inventory = [pscustomobject]@{
                sections = [pscustomobject]@{
                    tools = [pscustomobject]@{
                        status = 'ok'
                        data = @([pscustomobject]@{ path = $script:Path; present = $true; bytes = 100; sha256 = $script:Hash })
                    }
                }
            }
        }
    }
}

Describe 'pilot binary snapshot check' {
    It 'accepts the expected binary hash and size' {
        InModuleScope CtsReliability -Parameters @{ Device = (New-BinaryDevice); Path = $script:Path; Hash = $script:Hash } {
            param($Device, $Path, $Hash)
            @(Test-CtsPilotBinarySnapshot -Device $Device -Path $Path -ExpectedSha256 $Hash.ToLowerInvariant()).Count | Should -Be 0
        }
    }

    It 'reports a changed executable or missing and duplicated evidence' {
        InModuleScope CtsReliability -Parameters @{ Device = (New-BinaryDevice); Path = $script:Path; Hash = $script:Hash } {
            param($Device, $Path, $Hash)
            $Device.inventory.sections.tools.data[0].sha256 = 'B' * 64
            @(Test-CtsPilotBinarySnapshot -Device $Device -Path $Path -ExpectedSha256 $Hash) |
                Should -Contain 'Executable SHA-256 does not match'
            $Device.inventory.sections.tools.data = @()
            @(Test-CtsPilotBinarySnapshot -Device $Device -Path $Path -ExpectedSha256 $Hash) |
                Should -Contain 'Expected executable was not recorded exactly once'
            $entry = [pscustomobject]@{ path = $Path }
            $Device.inventory.sections.tools.data = @($entry, $entry)
            @(Test-CtsPilotBinarySnapshot -Device $Device -Path $Path -ExpectedSha256 $Hash) |
                Should -Contain 'Expected executable was not recorded exactly once'
        }
    }

    It 'reports incomplete inventories and malformed binary records' {
        InModuleScope CtsReliability -Parameters @{ Device = (New-BinaryDevice); Path = $script:Path; Hash = $script:Hash } {
            param($Device, $Path, $Hash)
            $Device.status = 'partial'
            @(Test-CtsPilotBinarySnapshot -Device $Device -Path $Path -ExpectedSha256 $Hash) |
                Should -Contain 'Guest tool inventory is unavailable or incomplete'
            $Device.status = 'complete'
            $Device.inventory.sections.tools.data[0].bytes = 0
            @(Test-CtsPilotBinarySnapshot -Device $Device -Path $Path -ExpectedSha256 $Hash) |
                Should -Contain 'Executable inventory is missing or invalid'
        }
    }

    It 'rejects invalid expected hashes and unsafe paths' {
        InModuleScope CtsReliability -Parameters @{ Device = (New-BinaryDevice); Path = $script:Path; Hash = $script:Hash } {
            param($Device, $Path, $Hash)
            { Test-CtsPilotBinarySnapshot -Device $Device -Path $Path -ExpectedSha256 'invalid' } | Should -Throw '*SHA-256*'
            { Test-CtsPilotBinarySnapshot -Device $Device -Path 'C:\tools\..\ctsTraffic.exe' -ExpectedSha256 $Hash } | Should -Throw '*executable*'
        }
    }
}
