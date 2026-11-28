#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Guest = [pscustomobject]@{ Target = 'A'; Channel = 'Direct'; LogPath = ''; Session = $null }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes('sample'))).Replace('-', '') }
    finally { $sha.Dispose() }
    $script:Receipt = [pscustomobject]@{
        Target = 'A'; Guest = 'CTSREL-A'; DestinationPath = 'C:\runs\guest\wer-sample.dmp'
        Bytes = 6; Sha256 = $hash; CandidateKind = 'NewName'; BootChanged = $false
    }
}

Describe 'host WER collection' {
    BeforeEach {
        $script:Token = [guid]::NewGuid().ToString('N')
        Mock -ModuleName CtsReliability Copy-CtsPilotGuestFile {
            [IO.File]::WriteAllText((Join-Path $DestinationDirectory 'wer-sample.dmp'), 'sample')
        }
    }

    It 'retains a verified copy under a newly reserved host directory' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Receipt = $script:Receipt; Root = $TestDrive; Token = $script:Token } {
            param($Guest, $Receipt, $Root, $Token)
            $result = Copy-CtsPilotWerToHost -GuestSession $Guest -Receipt $Receipt -GuestRunDirectory 'C:\runs\guest' `
                -HostRunDirectory $Root -TransferToken $Token
            $result.HostPath | Should -Be (Join-Path (Join-Path $Root "pilot-$Token") 'wer-sample.dmp')
            $result.Sha256 | Should -Be $Receipt.Sha256
            [IO.File]::ReadAllText($result.HostPath) | Should -Be 'sample'
            { Copy-CtsPilotWerToHost -GuestSession $Guest -Receipt $Receipt -GuestRunDirectory 'C:\runs\guest' `
                -HostRunDirectory $Root -TransferToken $Token } | Should -Throw '*already exists*'
            [IO.File]::ReadAllText($result.HostPath) | Should -Be 'sample'
        }
        Should -Invoke -ModuleName CtsReliability Copy-CtsPilotGuestFile -Times 1 -Exactly
    }

    It 'preserves an unverified transfer and refuses to reuse its directory' {
        Mock -ModuleName CtsReliability Copy-CtsPilotGuestFile {
            [IO.File]::WriteAllText((Join-Path $DestinationDirectory 'wer-sample.dmp'), 'tamper')
        }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Receipt = $script:Receipt; Root = $TestDrive; Token = $script:Token } {
            param($Guest, $Receipt, $Root, $Token)
            { Copy-CtsPilotWerToHost -GuestSession $Guest -Receipt $Receipt -GuestRunDirectory 'C:\runs\guest' `
                -HostRunDirectory $Root -TransferToken $Token } | Should -Throw '*unknown*hash*'
            $path = Join-Path (Join-Path $Root "pilot-$Token") 'wer-sample.dmp'
            [IO.File]::ReadAllText($path) | Should -Be 'tamper'
            { Copy-CtsPilotWerToHost -GuestSession $Guest -Receipt $Receipt -GuestRunDirectory 'C:\runs\guest' `
                -HostRunDirectory $Root -TransferToken $Token } | Should -Throw '*already exists*'
        }
    }

    It 'rejects receipts from another guest or run before creating a directory' {
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest; Receipt = $script:Receipt; Root = $TestDrive; Token = $script:Token } {
            param($Guest, $Receipt, $Root, $Token)
            $other = [pscustomobject]@{ Target = 'B' }
            { Copy-CtsPilotWerToHost -GuestSession $other -Receipt $Receipt -GuestRunDirectory 'C:\runs\guest' `
                -HostRunDirectory $Root -TransferToken $Token } | Should -Throw '*receipt*'
            { Copy-CtsPilotWerToHost -GuestSession $Guest -Receipt $Receipt -GuestRunDirectory 'C:\runs\other' `
                -HostRunDirectory $Root -TransferToken $Token } | Should -Throw '*run directory*'
            Test-Path -LiteralPath (Join-Path $Root "pilot-$Token") | Should -BeFalse
        }
        Should -Invoke -ModuleName CtsReliability Copy-CtsPilotGuestFile -Times 0
    }
}
