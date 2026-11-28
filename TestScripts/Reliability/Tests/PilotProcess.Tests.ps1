#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:PowerShellPath = (Get-Command powershell.exe).Source
    function Encode-TestCommand([string]$Command) {
        [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Command))
    }
}

Describe 'session-owned pilot process operations' {
    BeforeEach {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { & $ScriptBlock @ArgumentList }
        $script:Token = [guid]::NewGuid().ToString('N')
        $script:Stdout = Join-Path $TestDrive "$($script:Token)-stdout.txt"
        $script:Stderr = Join-Path $TestDrive "$($script:Token)-stderr.txt"
    }

    It 'retains a process handle, collects raw output, and preserves the exit code' {
        $encoded = Encode-TestCommand '[Console]::Out.Write("out"); [Console]::Error.Write("err"); exit 7'
        InModuleScope CtsReliability -Parameters @{ Token = $script:Token; Exe = $script:PowerShellPath; Encoded = $encoded; OutFile = $script:Stdout; ErrFile = $script:Stderr } {
            param($Token, $Exe, $Encoded, $OutFile, $ErrFile)
            $guest = [pscustomobject]@{}
            $started = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Start -Token $Token -FilePath $Exe `
                -Arguments @('-NoProfile', '-NonInteractive', '-EncodedCommand', $Encoded) -StdoutPath $OutFile -StderrPath $ErrFile
            try {
                $finished = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 30000
                $finished.ProcessId | Should -Be $started.ProcessId
                $finished.ExitCode | Should -Be 7
                [IO.File]::ReadAllText($OutFile) | Should -Be 'out'
                [IO.File]::ReadAllText($ErrFile) | Should -Be 'err'
                { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Status -Token $Token } | Should -Throw '*Unknown process token*'
            }
            finally {
                if ($global:CtsReliabilityPilotProcesses.ContainsKey($Token)) {
                    Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 10000 | Out-Null
                }
            }
        }
    }

    It 'refuses early completion or unknown and duplicate tokens' {
        $encoded = Encode-TestCommand 'Start-Sleep -Seconds 10'
        InModuleScope CtsReliability -Parameters @{ Token = $script:Token; Exe = $script:PowerShellPath; Encoded = $encoded; OutFile = $script:Stdout; ErrFile = $script:Stderr } {
            param($Token, $Exe, $Encoded, $OutFile, $ErrFile)
            $guest = [pscustomobject]@{}
            $started = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Start -Token $Token -FilePath $Exe `
                -Arguments @('-NoProfile', '-NonInteractive', '-EncodedCommand', $Encoded) -StdoutPath $OutFile -StderrPath $ErrFile
            try {
                $status = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Status -Token $Token
                $status.Exited | Should -BeFalse
                { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Status -Token ([guid]::NewGuid().ToString('N')) } | Should -Throw '*Unknown process token*'
                { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Start -Token $Token -FilePath $Exe `
                        -Arguments @('-NoProfile') -StdoutPath $OutFile -StderrPath $ErrFile } | Should -Throw '*token already exists*'
                { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 1 } | Should -Throw '*has not exited*'
                $finished = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 30000
                $finished.ProcessId | Should -Be $started.ProcessId
                $finished.ExitCode | Should -Be 0
            }
            finally {
                if ($global:CtsReliabilityPilotProcesses.ContainsKey($Token)) {
                    Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 30000 | Out-Null
                }
            }
        }
    }

    It 'refuses existing output paths and leaves their contents unchanged' {
        [IO.File]::WriteAllText($script:Stdout, 'existing')
        InModuleScope CtsReliability -Parameters @{ Token = $script:Token; Exe = $script:PowerShellPath; OutFile = $script:Stdout; ErrFile = $script:Stderr } {
            param($Token, $Exe, $OutFile, $ErrFile)
            { Invoke-CtsPilotProcessAction -GuestSession ([pscustomobject]@{}) -Action Start -Token $Token -FilePath $Exe `
                    -Arguments @('-NoProfile') -StdoutPath $OutFile -StderrPath $ErrFile } | Should -Throw
            [IO.File]::ReadAllText($OutFile) | Should -Be 'existing'
        }
    }

    It 'rejects unsafe argument tokens before creating output files' {
        InModuleScope CtsReliability -Parameters @{ Token = $script:Token; Exe = $script:PowerShellPath; OutFile = $script:Stdout; ErrFile = $script:Stderr } {
            param($Token, $Exe, $OutFile, $ErrFile)
            { Invoke-CtsPilotProcessAction -GuestSession ([pscustomobject]@{}) -Action Start -Token $Token -FilePath $Exe `
                    -Arguments @('contains spaces') -StdoutPath $OutFile -StderrPath $ErrFile } | Should -Throw '*without whitespace*'
            Test-Path -LiteralPath $OutFile | Should -BeFalse
            Test-Path -LiteralPath $ErrFile | Should -BeFalse
        }
    }
}
