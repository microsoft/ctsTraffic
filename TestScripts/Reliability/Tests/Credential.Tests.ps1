#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Live tests for src\Credential.ps1. They write, read, and delete a uniquely named temporary generic
# credential in the current user's Windows Credential Manager; the real guest credential is never touched.

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Target = "ctsReliability/pester-$([guid]::NewGuid())"
}

AfterAll {
    $null = Remove-CtsStoredCredential -Target $script:Target -Confirm:$false
}

Describe 'Credential Manager round trip' -Tag 'Live' {
    It 'throws a helpful error when the credential does not exist' {
        { Get-CtsStoredCredential -Target $script:Target } | Should -Throw -ExpectedMessage '*Set-CtsGuestCredential.ps1*'
    }

    It 'stores and reads back the user name and password' {
        $secret = 'Pester-' + [guid]::NewGuid().ToString('N')
        $credential = [pscredential]::new('ctsadmin', (ConvertTo-SecureString $secret -AsPlainText -Force))
        Set-CtsStoredCredential -Target $script:Target -Credential $credential -Confirm:$false
        $read = Get-CtsStoredCredential -Target $script:Target
        $read | Should -BeOfType [pscredential]
        $read.UserName | Should -Be 'ctsadmin'
        $read.GetNetworkCredential().Password | Should -Be $secret
    }

    It 'overwrites an existing credential' {
        $credential = [pscredential]::new('otheruser', (ConvertTo-SecureString 'second-value' -AsPlainText -Force))
        Set-CtsStoredCredential -Target $script:Target -Credential $credential -Confirm:$false
        (Get-CtsStoredCredential -Target $script:Target).UserName | Should -Be 'otheruser'
    }

    It 'honors -WhatIf' {
        $credential = [pscredential]::new('whatif', (ConvertTo-SecureString 'x' -AsPlainText -Force))
        Set-CtsStoredCredential -Target $script:Target -Credential $credential -WhatIf
        (Get-CtsStoredCredential -Target $script:Target).UserName | Should -Be 'otheruser'
    }

    It 'deletes the credential and reports a missing one as $false' {
        Remove-CtsStoredCredential -Target $script:Target -Confirm:$false | Should -BeTrue
        Remove-CtsStoredCredential -Target $script:Target -Confirm:$false | Should -BeFalse
        { Get-CtsStoredCredential -Target $script:Target } | Should -Throw
    }
}
