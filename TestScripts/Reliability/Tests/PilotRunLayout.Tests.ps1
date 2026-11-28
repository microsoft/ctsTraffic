#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Server = [pscustomobject]@{ Channel = 'Direct'; Target = 'A'; Session = [pscustomobject]@{} }
    $script:Client = [pscustomobject]@{ Channel = 'Direct'; Target = 'B'; Session = [pscustomobject]@{} }
}

Describe 'pilot run layout reservation' {
    BeforeEach {
        $script:FailureTarget = ''
        Mock -ModuleName CtsReliability New-CtsPilotHostRunDirectory {
            if ($script:FailureTarget -eq 'Host') { throw 'host failure' }
            [IO.Path]::Combine($RootPath, "pilot-$Token")
        }
        Mock -ModuleName CtsReliability New-CtsPilotRunDirectory {
            if ($script:FailureTarget -eq $GuestSession.Target) { throw 'guest outcome unknown' }
            if ($script:FailureTarget -eq 'WrongPath') { return 'C:\runs\unexpected' }
            [IO.Path]::Combine($RootPath, "pilot-$Token")
        }
    }

    It 'reserves matching directories on host and both guests' {
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = New-CtsPilotRunLayout -Server $Server -Client $Client -RootPath 'C:\ctsReliability\runs' -Token ('a' * 32)
            $result.Outcome | Should -Be 'Reserved'
            $result.Reservations.Count | Should -Be 3
            @($result.Reservations | Where-Object Outcome -eq 'Reserved').Count | Should -Be 3
            @($result.Reservations | Where-Object { $_.Path -ne ('C:\ctsReliability\runs\pilot-' + ('a' * 32)) }).Count | Should -Be 0
        }
        Should -Invoke -ModuleName CtsReliability New-CtsPilotHostRunDirectory -Times 1 -Exactly
        Should -Invoke -ModuleName CtsReliability New-CtsPilotRunDirectory -Times 2 -Exactly
    }

    It 'keeps partial reservations and does not retry or continue after a guest failure' {
        $script:FailureTarget = 'A'
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = New-CtsPilotRunLayout -Server $Server -Client $Client -RootPath 'C:\ctsReliability\runs' -Token ('a' * 32)
            $result.Outcome | Should -Be 'Incomplete'
            $result.Reservations[0].Outcome | Should -Be 'Reserved'
            $result.Reservations[1].Outcome | Should -Be 'Unknown'
            $result.Reservations[1].Error | Should -Match 'guest outcome unknown'
            $result.Reservations[2].Outcome | Should -Be 'NotAttempted'
        }
        Should -Invoke -ModuleName CtsReliability New-CtsPilotRunDirectory -Times 1 -Exactly
    }

    It 'does not adopt an unexpected guest directory' {
        $script:FailureTarget = 'WrongPath'
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = New-CtsPilotRunLayout -Server $Server -Client $Client -RootPath 'C:\ctsReliability\runs' -Token ('a' * 32)
            $result.Reservations[1].Outcome | Should -Be 'Unknown'
            $result.Reservations[2].Outcome | Should -Be 'NotAttempted'
        }
    }

    It 'retains both earlier reservations if the client reservation fails' {
        $script:FailureTarget = 'B'
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = New-CtsPilotRunLayout -Server $Server -Client $Client -RootPath 'C:\ctsReliability\runs' -Token ('a' * 32)
            $result.Outcome | Should -Be 'Incomplete'
            $result.Reservations[0].Outcome | Should -Be 'Reserved'
            $result.Reservations[1].Outcome | Should -Be 'Reserved'
            $result.Reservations[2].Outcome | Should -Be 'Unknown'
        }
        Should -Invoke -ModuleName CtsReliability New-CtsPilotRunDirectory -Times 2 -Exactly
    }

    It 'does not contact guests if host reservation is unknown' {
        $script:FailureTarget = 'Host'
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            $result = New-CtsPilotRunLayout -Server $Server -Client $Client -RootPath 'C:\ctsReliability\runs' -Token ('a' * 32)
            $result.Reservations[0].Outcome | Should -Be 'Unknown'
            $result.Reservations[1].Outcome | Should -Be 'NotAttempted'
            $result.Reservations[2].Outcome | Should -Be 'NotAttempted'
        }
        Should -Invoke -ModuleName CtsReliability New-CtsPilotRunDirectory -Times 0
    }

    It 'rejects invalid tokens or duplicate guests before creating directories' {
        InModuleScope CtsReliability -Parameters @{ Server = $script:Server; Client = $script:Client } {
            param($Server, $Client)
            { New-CtsPilotRunLayout -Server $Server -Client $Client -RootPath 'C:\ctsReliability\runs' -Token 'invalid' } | Should -Throw '*token*'
            { New-CtsPilotRunLayout -Server $Server -Client $Server -RootPath 'C:\ctsReliability\runs' -Token ('a' * 32) } | Should -Throw '*distinct*'
            $sameSession = [pscustomobject]@{ Channel = 'Direct'; Target = 'B'; Session = $Server.Session }
            { New-CtsPilotRunLayout -Server $Server -Client $sameSession -RootPath 'C:\ctsReliability\runs' -Token ('a' * 32) } | Should -Throw '*distinct*'
        }
        Should -Invoke -ModuleName CtsReliability New-CtsPilotHostRunDirectory -Times 0
        Should -Invoke -ModuleName CtsReliability New-CtsPilotRunDirectory -Times 0
    }
}
