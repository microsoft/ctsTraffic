#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    $script:Source = (Resolve-Path (Join-Path $PSScriptRoot '..\support\PilotSelfJob.cs')).Path
    $script:PowerShellPath = (Get-Command powershell.exe).Source
    $script:GuardianScript = Join-Path $TestDrive 'guardian.ps1'
    [IO.File]::WriteAllText($script:GuardianScript, @'
param($Source, $ChildPidFile, $Mode)
$ErrorActionPreference = 'Stop'
Add-Type -Path $Source
[CtsReliability.PilotSelfJob]::Enter()
$command = if ($Mode -eq 'hold') { 'Start-Sleep -Seconds 30' } else { 'exit 0' }
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
$child = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList @('-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded) -PassThru
[IO.File]::WriteAllText($ChildPidFile, [string]$child.Id)
if (-not $child.WaitForExit(35000)) { throw 'Child did not exit.' }
if ($Mode -eq 'normal' -and $child.ExitCode -ne 0) { throw 'Child failed.' }
exit 0
'@)
}

Describe 'guardian job containment' {
    It 'terminates a live child when its guardian is terminated' {
        $pidFile = Join-Path $TestDrive 'held-child.pid'
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $script:PowerShellPath
        $info.Arguments = '-NoProfile -NonInteractive -File "{0}" -Source "{1}" -ChildPidFile "{2}" -Mode hold' -f $script:GuardianScript, $script:Source, $pidFile
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $guardian = [Diagnostics.Process]::Start($info)
        $guardianHandle = $guardian.SafeHandle
        $guardianPinned = $false
        $guardianHandle.DangerousAddRef([ref]$guardianPinned)
        $child = $null
        $childHandle = $null
        $childPinned = $false
        try {
            $deadline = [Diagnostics.Stopwatch]::StartNew()
            while (-not (Test-Path -LiteralPath $pidFile) -and -not $guardian.HasExited -and $deadline.Elapsed.TotalSeconds -lt 20) {
                Start-Sleep -Milliseconds 100
            }
            Test-Path -LiteralPath $pidFile | Should -BeTrue
            $child = [Diagnostics.Process]::GetProcessById([int][IO.File]::ReadAllText($pidFile))
            $childHandle = $child.SafeHandle
            $childHandle.DangerousAddRef([ref]$childPinned)
            $child.HasExited | Should -BeFalse
            Stop-Process -Id $guardian.Id -ErrorAction Stop
            $guardian.WaitForExit(10000) | Should -BeTrue
            $child.WaitForExit(10000) | Should -BeTrue
        }
        finally {
            if (-not $guardian.HasExited) { Stop-Process -Id $guardian.Id -ErrorAction SilentlyContinue }
            if ($null -ne $child) {
                $null = $child.WaitForExit(35000)
                if ($childPinned) { $childHandle.DangerousRelease() }
                $child.Dispose()
            }
            if ($guardianPinned) { $guardianHandle.DangerousRelease() }
            $guardian.Dispose()
        }
    }

    It 'allows a guardian and its completed child to exit normally' {
        $pidFile = Join-Path $TestDrive 'normal-child.pid'
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $script:PowerShellPath
        $info.Arguments = '-NoProfile -NonInteractive -File "{0}" -Source "{1}" -ChildPidFile "{2}" -Mode normal' -f $script:GuardianScript, $script:Source, $pidFile
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $guardian = [Diagnostics.Process]::Start($info)
        $guardianHandle = $guardian.SafeHandle
        $guardianPinned = $false
        $guardianHandle.DangerousAddRef([ref]$guardianPinned)
        try {
            $guardian.WaitForExit(45000) | Should -BeTrue
            $guardian.ExitCode | Should -Be 0
            Test-Path -LiteralPath $pidFile | Should -BeTrue
        }
        finally {
            if (-not $guardian.HasExited) { Stop-Process -Id $guardian.Id -ErrorAction SilentlyContinue }
            if ($guardianPinned) { $guardianHandle.DangerousRelease() }
            $guardian.Dispose()
        }
    }
}
