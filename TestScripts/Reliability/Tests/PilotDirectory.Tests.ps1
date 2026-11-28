#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
}

Describe 'pilot run directory reservation' {
    BeforeEach {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { & $ScriptBlock @ArgumentList }
        $script:Token = [guid]::NewGuid().ToString('N')
    }

    It 'creates a new directory and refuses to reuse it without disturbing its contents' {
        InModuleScope CtsReliability -Parameters @{ Root = $TestDrive; Token = $script:Token } {
            param($Root, $Token)
            $guest = [pscustomobject]@{}
            $path = New-CtsPilotRunDirectory -GuestSession $guest -RootPath $Root -Token $Token
            $path | Should -Be (Join-Path $Root "pilot-$Token")
            Test-Path -LiteralPath $path -PathType Container | Should -BeTrue
            $marker = Join-Path $path 'existing.txt'
            [IO.File]::WriteAllText($marker, 'preserve')
            { New-CtsPilotRunDirectory -GuestSession $guest -RootPath $Root -Token $Token } | Should -Throw '*already exists*'
            [IO.File]::ReadAllText($marker) | Should -Be 'preserve'
        }
    }

    It 'rejects invalid tokens, unsafe roots, and missing parents' {
        InModuleScope CtsReliability -Parameters @{ Root = $TestDrive; Token = $script:Token } {
            param($Root, $Token)
            $guest = [pscustomobject]@{}
            { New-CtsPilotRunDirectory -GuestSession $guest -RootPath $Root -Token '../other' } | Should -Throw '*token*'
            { New-CtsPilotRunDirectory -GuestSession $guest -RootPath 'C:\runs\..\other' -Token $Token } | Should -Throw '*Pilot root*'
            { New-CtsPilotRunDirectory -GuestSession $guest -RootPath (Join-Path $Root 'missing-parent') -Token $Token } | Should -Throw '*Could not create*'
        }
    }
}
