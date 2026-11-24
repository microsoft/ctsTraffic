#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Unit tests for src\Transport.ps1. PowerShell remoting cmdlets are mocked; no VM is required.

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:FakePSSession = New-MockObject -Type ([System.Management.Automation.Runspaces.PSSession])
    $script:Credential = [pscredential]::new('ctsadmin', (ConvertTo-SecureString 'unused' -AsPlainText -Force))

    function Read-LogLines([string]$Path) { @(Get-Content -LiteralPath $Path | ForEach-Object { $_ | ConvertFrom-Json }) }
}

Describe 'Write-CtsLogEntry' {
    It 'appends one compact JSON line per entry and creates the directory' {
        $log = Join-Path $TestDrive 'nested\dir\setup.jsonl'
        Write-CtsLogEntry -LogPath $log -Entry ([ordered]@{ operation = 'one'; n = 1 })
        Write-CtsLogEntry -LogPath $log -Entry ([ordered]@{ operation = 'two'; n = 2 })
        $lines = Read-LogLines $log
        $lines.Count | Should -Be 2
        $lines[1].operation | Should -Be 'two'
    }

    It 'does nothing when no log path is given' {
        { Write-CtsLogEntry -LogPath '' -Entry @{ a = 1 } } | Should -Not -Throw
    }
}

Describe 'Invoke-CtsLogged' {
    It 'returns the action result and logs success' {
        InModuleScope CtsReliability -Parameters @{ Log = (Join-Path $TestDrive 'ok.jsonl') } {
            param($Log)
            Invoke-CtsLogged -Operation 'Op' -Channel 'Direct' -Target 'vm' -LogPath $Log -Action { 42 } | Should -Be 42
            $entry = Get-Content $Log | ConvertFrom-Json
            $entry.success | Should -BeTrue
            $entry.operation | Should -Be 'Op'
            $entry.channel | Should -Be 'Direct'
            $entry.durationMs | Should -BeGreaterOrEqual 0
        }
    }

    It 'rethrows and logs the error on failure' {
        InModuleScope CtsReliability -Parameters @{ Log = (Join-Path $TestDrive 'fail.jsonl') } {
            param($Log)
            { Invoke-CtsLogged -Operation 'Op' -Channel 'WinRM' -Target '192.168.250.1' -LogPath $Log -Action { throw 'boom' } } | Should -Throw 'boom'
            $entry = Get-Content $Log | ConvertFrom-Json
            $entry.success | Should -BeFalse
            $entry.error | Should -Be 'boom'
        }
    }
}

Describe 'New-CtsGuestSession' {
    BeforeEach {
        Mock -ModuleName CtsReliability New-PSSession { $script:FakePSSession }
    }

    It 'uses PowerShell Direct (-VMName) for the Direct channel' {
        $log = Join-Path $TestDrive 'direct.jsonl'
        $s = New-CtsGuestSession -VMName 'ctsRel-A' -Channel Direct -Credential $script:Credential -LogPath $log
        $s.Channel | Should -Be 'Direct'
        $s.Target | Should -Be 'ctsRel-A'
        Should -Invoke -ModuleName CtsReliability New-PSSession -Times 1 -Exactly -ParameterFilter { $VMName -eq 'ctsRel-A' }
        (Read-LogLines $log)[0].operation | Should -Be 'OpenSession'
    }

    It 'uses WinRM with Negotiate against the management address' {
        $s = New-CtsGuestSession -VMName 'ctsRel-A' -Channel WinRM -Credential $script:Credential -ComputerName '192.168.250.1'
        $s.Target | Should -Be '192.168.250.1'
        Should -Invoke -ModuleName CtsReliability New-PSSession -Times 1 -Exactly -ParameterFilter {
            $ComputerName -contains '192.168.250.1' -and $Authentication -eq 'Negotiate'
        }
    }

    It 'requires ComputerName for WinRM' {
        { New-CtsGuestSession -VMName 'ctsRel-A' -Channel WinRM -Credential $script:Credential } | Should -Throw -ExpectedMessage '*ComputerName*'
        Should -Invoke -ModuleName CtsReliability New-PSSession -Times 0 -Exactly
    }
}

Describe 'Guest command and copy helpers' {
    BeforeEach {
        $script:Guest = [pscustomobject]@{ Channel = 'Direct'; VMName = 'ctsRel-A'; Target = 'ctsRel-A'; Session = $script:FakePSSession; LogPath = (Join-Path $TestDrive 'guest.jsonl') }
        Mock -ModuleName CtsReliability Invoke-Command { 'guest-result' }
        Mock -ModuleName CtsReliability Copy-Item { }
    }

    It 'Invoke-CtsGuestCommand runs in the session and logs the operation name' {
        Invoke-CtsGuestCommand -GuestSession $script:Guest -ScriptBlock { 1 } -Operation 'Probe' | Should -Be 'guest-result'
        Should -Invoke -ModuleName CtsReliability Invoke-Command -Times 1 -Exactly
        (Read-LogLines $script:Guest.LogPath)[-1].operation | Should -Be 'Probe'
    }

    It 'Copy-CtsToGuest creates the destination then copies with -ToSession' {
        Copy-CtsToGuest -GuestSession $script:Guest -Path 'C:\src\file.txt' -Destination 'C:\ctsReliability\tools'
        Should -Invoke -ModuleName CtsReliability Invoke-Command -Times 1 -Exactly
        Should -Invoke -ModuleName CtsReliability Copy-Item -Times 1 -Exactly -ParameterFilter { $ToSession -and $Destination -eq 'C:\ctsReliability\tools' }
    }

    It 'Copy-CtsFromGuest creates the local destination then copies with -FromSession' {
        $dest = Join-Path $TestDrive 'evidence'
        Copy-CtsFromGuest -GuestSession $script:Guest -Path 'C:\ctsReliability\logs\*' -Destination $dest
        Test-Path $dest | Should -BeTrue
        Should -Invoke -ModuleName CtsReliability Copy-Item -Times 1 -Exactly -ParameterFilter { $FromSession -and $Destination -eq $dest }
    }

    It 'logs a failed copy and rethrows' {
        Mock -ModuleName CtsReliability Copy-Item { throw 'copy failed' }
        { Copy-CtsToGuest -GuestSession $script:Guest -Path 'C:\x' -Destination 'C:\y' } | Should -Throw '*copy failed*'
        $last = (Read-LogLines $script:Guest.LogPath)[-1]
        $last.success | Should -BeFalse
        $last.operation | Should -Be 'CopyTo:C:\y'
    }
}

Describe 'Wait-CtsGuestReady' {
    It 'returns once the guest answers' {
        InModuleScope CtsReliability -Parameters @{ Cred = $script:Credential } {
            param($Cred)
            $script:calls = 0
            Mock Invoke-Command { $script:calls++; if ($script:calls -lt 3) { throw 'not yet' } 'CTSREL-A' }
            Mock Start-Sleep { }
            Wait-CtsGuestReady -VMName 'ctsRel-A' -Credential $Cred -TimeoutSeconds 60 -PollSeconds 1
            $script:calls | Should -Be 3
        }
    }

    It 'throws with the last error after the timeout' {
        InModuleScope CtsReliability -Parameters @{ Cred = $script:Credential } {
            param($Cred)
            Mock Invoke-Command { throw 'unreachable' }
            Mock Start-Sleep { }
            { Wait-CtsGuestReady -VMName 'ctsRel-A' -Credential $Cred -TimeoutSeconds 0 -PollSeconds 1 } | Should -Throw
            { Wait-CtsGuestReady -VMName 'ctsRel-A' -Credential $Cred -TimeoutSeconds 1 -PollSeconds 1 } | Should -Throw -ExpectedMessage '*unreachable*'
        }
    }
}
