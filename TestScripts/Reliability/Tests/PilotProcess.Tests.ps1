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

    Describe 'timed pilot process operations' {
        BeforeAll {
            $script:Guest = [pscustomobject]@{ Session = (New-MockObject -Type ([System.Management.Automation.Runspaces.PSSession])); Channel = 'Direct'; Target = 'vm'; LogPath = '' }
            $script:CompletedJob = Start-Job -ScriptBlock { }
            $null = Wait-Job -Job $script:CompletedJob
            $script:FailedJob = Start-Job -ScriptBlock { throw 'job failed' }
            $null = Wait-Job -Job $script:FailedJob
        }

        AfterAll {
            Remove-Job -Job $script:CompletedJob -Force -ErrorAction SilentlyContinue
            Remove-Job -Job $script:FailedJob -Force -ErrorAction SilentlyContinue
        }

        BeforeEach {
            Mock -ModuleName CtsReliability Invoke-Command { $script:CompletedJob }
            Mock -ModuleName CtsReliability Wait-Job { $script:CompletedJob }
            Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Token = ('a' * 32); ProcessId = 123; Exited = $false; ExitCode = $null } }
            Mock -ModuleName CtsReliability Remove-Job { }
        }

        It 'returns the remote result and removes the job' {
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                $result = Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Status -Token ('a' * 32) -TimeoutSeconds 10
                $result.ProcessId | Should -Be 123
            }
            Should -Invoke -ModuleName CtsReliability Invoke-Command -Times 1 -Exactly -ParameterFilter { $AsJob }
            Should -Invoke -ModuleName CtsReliability Remove-Job -Times 1 -Exactly
        }

        It 'reports an unknown outcome on timeout and removes the job' {
            Mock -ModuleName CtsReliability Wait-Job { }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Start -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*outcome is unknown*'
            }
            Should -Invoke -ModuleName CtsReliability Remove-Job -Times 1 -Exactly
        }

        It 'rejects failed jobs and missing results' {
            Mock -ModuleName CtsReliability Invoke-Command { $script:FailedJob }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Stop -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*outcome is unknown*'
            }
            Mock -ModuleName CtsReliability Invoke-Command { $script:CompletedJob }
            Mock -ModuleName CtsReliability Receive-Job { }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Status -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*exactly one result*'
            }
        }

        It 'treats submission errors and mismatched process identity as unknown outcomes' {
            Mock -ModuleName CtsReliability Invoke-Command { throw 'submission failed' }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Start -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*outcome is unknown*'
            }
            Mock -ModuleName CtsReliability Invoke-Command { $script:CompletedJob }
            Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Token = ('b' * 32); ProcessId = 123; Exited = $false } }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Status -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*invalid token*'
            }
        }

        It 'rejects malformed status and reports logging failure as an unknown outcome' {
            Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Token = ('a' * 32); ProcessId = 123; Exited = $true } }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Status -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*invalid exit code*'
            }
            Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Token = ('a' * 32); ProcessId = 123; Exited = $false; ExitCode = $null } }
            Mock -ModuleName CtsReliability Write-CtsLogEntry { throw 'simulated logging failure' }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Status -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*outcome is unknown*'
            }
        }

        It 'rejects incomplete start, stop, and completion responses' {
            Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Token = ('a' * 32); ProcessId = 123; StartedUtc = 'invalid' } }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Start -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*invalid start time*'
            }
            Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Token = ('a' * 32); ProcessId = 123; Exited = $false; ExitCode = $null } }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Stop -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*invalid exit state*'
            }
            Mock -ModuleName CtsReliability Receive-Job { [pscustomobject]@{ Token = ('a' * 32); ProcessId = 123; ExitCode = $null } }
            InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
                param($Guest)
                { Invoke-CtsPilotProcessAction -GuestSession $Guest -Action Complete -Token ('a' * 32) -TimeoutSeconds 1 } | Should -Throw '*invalid exit code*'
            }
        }
    }

    It 'retains a process handle, collects raw output, and preserves the exit code' {
        $encoded = Encode-TestCommand '[Console]::Out.Write("out"); [Console]::Error.Write("err"); exit 7'
        InModuleScope CtsReliability -Parameters @{ Token = $script:Token; Exe = $script:PowerShellPath; Encoded = $encoded; OutFile = $script:Stdout; ErrFile = $script:Stderr } {
            param($Token, $Exe, $Encoded, $OutFile, $ErrFile)
            $guest = [pscustomobject]@{}
            $started = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Start -Token $Token -FilePath $Exe `
                -Arguments @('-NoProfile', '-NonInteractive', '-EncodedCommand', $Encoded) -StdoutPath $OutFile -StderrPath $ErrFile
            try {
                $handle = $global:CtsReliabilityPilotProcesses[$Token].PinnedHandle
                $global:CtsReliabilityPilotProcesses[$Token].Process.WaitForExit(30000) | Should -BeTrue
                $stopped = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Stop -Token $Token
                $stopped.ExitCode | Should -Be 7
                $finished = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 30000
                $finished.ProcessId | Should -Be $started.ProcessId
                $finished.ExitCode | Should -Be 7
                $handle.IsClosed | Should -BeTrue
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
                { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Stop -Token ([guid]::NewGuid().ToString('N')) } | Should -Throw '*Unknown process token*'
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

    It 'stops the pinned process without affecting a second tracked process' {
        $encoded = Encode-TestCommand 'Start-Sleep -Seconds 30'
        $otherToken = [guid]::NewGuid().ToString('N')
        $otherOut = Join-Path $TestDrive "$otherToken-stdout.txt"
        $otherErr = Join-Path $TestDrive "$otherToken-stderr.txt"
        InModuleScope CtsReliability -Parameters @{ Token = $script:Token; Other = $otherToken; Exe = $script:PowerShellPath; Encoded = $encoded; OutFile = $script:Stdout; ErrFile = $script:Stderr; OtherOut = $otherOut; OtherErr = $otherErr } {
            param($Token, $Other, $Exe, $Encoded, $OutFile, $ErrFile, $OtherOut, $OtherErr)
            $guest = [pscustomobject]@{}
            try {
                $first = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Start -Token $Token -FilePath $Exe `
                    -Arguments @('-NoProfile', '-NonInteractive', '-EncodedCommand', $Encoded) -StdoutPath $OutFile -StderrPath $ErrFile
                $second = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Start -Token $Other -FilePath $Exe `
                    -Arguments @('-NoProfile', '-NonInteractive', '-EncodedCommand', $Encoded) -StdoutPath $OtherOut -StderrPath $OtherErr
                { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Stop -Token $Token -ExpectedProcessId $second.ProcessId } |
                    Should -Throw '*does not match*'
                (Invoke-CtsPilotProcessAction -GuestSession $guest -Action Status -Token $Token -ExpectedProcessId $first.ProcessId).Exited | Should -BeFalse
                $global:CtsReliabilityPilotProcesses[$Token].HandlePinned = $false
                try {
                    { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Stop -Token $Token } | Should -Throw '*not pinned*'
                    (Invoke-CtsPilotProcessAction -GuestSession $guest -Action Status -Token $Token).Exited | Should -BeFalse
                }
                finally { $global:CtsReliabilityPilotProcesses[$Token].HandlePinned = $true }
                $stopped = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Stop -Token $Token -WaitMilliseconds 10000 -ExpectedProcessId $first.ProcessId
                $stopped.ProcessId | Should -Be $first.ProcessId
                $stopped.Exited | Should -BeTrue
                $otherStatus = Invoke-CtsPilotProcessAction -GuestSession $guest -Action Status -Token $Other
                $otherStatus.ProcessId | Should -Be $second.ProcessId
                $otherStatus.Exited | Should -BeFalse
            }
            finally {
                foreach ($id in $Token, $Other) {
                    if ($global:CtsReliabilityPilotProcesses.ContainsKey($id)) {
                        Invoke-CtsPilotProcessAction -GuestSession $guest -Action Stop -Token $id -WaitMilliseconds 10000 | Out-Null
                        Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $id -WaitMilliseconds 10000 | Out-Null
                    }
                }
            }
        }
    }

    It 'retains tracking if stopping a live process fails' {
        $encoded = Encode-TestCommand 'Start-Sleep -Seconds 10'
        InModuleScope CtsReliability -Parameters @{ Token = $script:Token; Exe = $script:PowerShellPath; Encoded = $encoded; OutFile = $script:Stdout; ErrFile = $script:Stderr } {
            param($Token, $Exe, $Encoded, $OutFile, $ErrFile)
            $guest = [pscustomobject]@{}
            Invoke-CtsPilotProcessAction -GuestSession $guest -Action Start -Token $Token -FilePath $Exe `
                -Arguments @('-NoProfile', '-NonInteractive', '-EncodedCommand', $Encoded) -StdoutPath $OutFile -StderrPath $ErrFile | Out-Null
            try {
                Mock Stop-Process { throw 'simulated stop failure' }
                { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Stop -Token $Token } | Should -Throw '*simulated stop failure*'
                $global:CtsReliabilityPilotProcesses[$Token].HandlePinned | Should -BeTrue
                (Invoke-CtsPilotProcessAction -GuestSession $guest -Action Status -Token $Token).Exited | Should -BeFalse
            }
            finally {
                Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 30000 | Out-Null
            }
        }
    }

    It 'releases tracking when output capture has failed after the process exits' {
        $encoded = Encode-TestCommand 'exit 0'
        InModuleScope CtsReliability -Parameters @{ Token = $script:Token; Exe = $script:PowerShellPath; Encoded = $encoded; OutFile = $script:Stdout; ErrFile = $script:Stderr } {
            param($Token, $Exe, $Encoded, $OutFile, $ErrFile)
            $guest = [pscustomobject]@{}
            Invoke-CtsPilotProcessAction -GuestSession $guest -Action Start -Token $Token -FilePath $Exe `
                -Arguments @('-NoProfile', '-NonInteractive', '-EncodedCommand', $Encoded) -StdoutPath $OutFile -StderrPath $ErrFile | Out-Null
            $record = $global:CtsReliabilityPilotProcesses[$Token]
            try {
                $record.Process.WaitForExit(30000) | Should -BeTrue
                [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($record.StdoutTask, $record.StderrTask), 30000) | Should -BeTrue
                $handle = $record.PinnedHandle
                $record.StdoutTask = [Threading.Tasks.Task]::FromException([Exception]::new('simulated capture failure'))
                { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 30000 } | Should -Throw
                $handle.IsClosed | Should -BeTrue
                $global:CtsReliabilityPilotProcesses.ContainsKey($Token) | Should -BeFalse
            }
            finally {
                if ($global:CtsReliabilityPilotProcesses.ContainsKey($Token)) {
                    try { Invoke-CtsPilotProcessAction -GuestSession $guest -Action Complete -Token $Token -WaitMilliseconds 30000 | Out-Null } catch {}
                }
            }
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
