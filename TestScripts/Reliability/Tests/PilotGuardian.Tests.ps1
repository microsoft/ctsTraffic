#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    $script:GuardianPath = (Resolve-Path (Join-Path $PSScriptRoot '..\support\PilotGuardian.ps1')).Path
    $script:PowerShellPath = (Get-Command powershell.exe).Source
    function Invoke-GuardianFixture {
        param([string]$Command, [int]$HeartbeatSeconds = 120, [long]$MemoryMinimum = 1,
            [long]$DiskMinimum = 1, [switch]$KillGuardian, [switch]$ExistingStdout,
            [int]$InitialHeartbeatAgeSeconds = 0, [switch]$KeepPulsing, [switch]$PulseUntilStarted)
        $directory = Join-Path $TestDrive ([guid]::NewGuid().ToString('N'))
        $null = New-Item -ItemType Directory -Path $directory
        $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Command))
        $spec = [ordered]@{
            role = 'client'; executable = $script:PowerShellPath
            arguments = @('-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded)
            minimumFreeMemoryBytes = $MemoryMinimum; minimumFreeDiskBytes = $DiskMinimum
            heartbeatTimeoutSeconds = $HeartbeatSeconds; pollMilliseconds = 200
        }
        $specPath = Join-Path $directory 'spec.json'
        [IO.File]::WriteAllText($specPath, ($spec | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        $heartbeat = Join-Path $directory 'heartbeat.txt'
        [IO.File]::WriteAllText($heartbeat, 'alive')
        [IO.File]::SetLastWriteTimeUtc($heartbeat, [DateTime]::UtcNow.AddSeconds(-$InitialHeartbeatAgeSeconds))
        $initialHeartbeatUtc = [IO.File]::GetLastWriteTimeUtc($heartbeat)
        $pulseJob = if ($KeepPulsing -or $PulseUntilStarted) {
            Start-Job -ArgumentList $heartbeat -ScriptBlock {
                param($path)
                for ($i = 0; $i -lt 180; $i++) {
                    [IO.File]::SetLastWriteTimeUtc($path, [DateTime]::UtcNow)
                    Start-Sleep -Milliseconds 500
                }
            }
        } else { $null }
        if ($null -ne $pulseJob) {
            $ready = [Diagnostics.Stopwatch]::StartNew()
            while ([IO.File]::GetLastWriteTimeUtc($heartbeat) -le $initialHeartbeatUtc -and $ready.Elapsed.TotalSeconds -lt 30) {
                Start-Sleep -Milliseconds 100
            }
            if ([IO.File]::GetLastWriteTimeUtc($heartbeat) -le $initialHeartbeatUtc) {
                Remove-Job -Job $pulseJob -Force -ErrorAction SilentlyContinue
                throw 'Heartbeat fixture did not start.'
            }
        }
        $stdoutPath = Join-Path $directory 'g1b-pilot-client-stdout.txt'
        if ($ExistingStdout) { [IO.File]::WriteAllText($stdoutPath, 'preserve') }
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $script:PowerShellPath
        $info.Arguments = '-NoProfile -NonInteractive -File "{0}" -SpecPath "{1}"' -f $script:GuardianPath, $specPath
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
            $events = Join-Path $directory 'g1b-pilot-client-guardian.jsonl'
            if ($KillGuardian -or $PulseUntilStarted) {
                $deadline = [Diagnostics.Stopwatch]::StartNew()
                $started = $null
                while ($deadline.Elapsed.TotalSeconds -lt 40 -and -not $guardian.HasExited) {
                    if (Test-Path -LiteralPath $events) {
                        $started = @(Get-Content -LiteralPath $events | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object type -eq 'started') | Select-Object -First 1
                        if ($null -ne $started) { break }
                    }
                    Start-Sleep -Milliseconds 100
                }
                $started | Should -Not -BeNullOrEmpty
                if ($PulseUntilStarted) {
                    Remove-Job -Job $pulseJob -Force -ErrorAction Stop
                    $pulseJob = $null
                }
                if ($KillGuardian) {
                    $child = [Diagnostics.Process]::GetProcessById([int]$started.processId)
                    $childHandle = $child.SafeHandle
                    $childHandle.DangerousAddRef([ref]$childPinned)
                    $child.HasExited | Should -BeFalse
                    Stop-Process -Id $guardian.Id -ErrorAction Stop
                }
            }
            if (-not $guardian.WaitForExit(45000)) { throw 'Guardian did not exit.' }
            $childExitedAfterGuardian = if ($null -ne $child) { $child.WaitForExit(10000) } else { $null }
            $resultPath = Join-Path $directory 'g1b-pilot-client-guardian-result.json'
            [pscustomobject]@{
                Directory = $directory; GuardianExitCode = $guardian.ExitCode; InitialHeartbeatUtc = $initialHeartbeatUtc
                ChildExitedAfterGuardian = $childExitedAfterGuardian
                Result = if (Test-Path -LiteralPath $resultPath) { Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json } else { $null }
            }
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
            if ($null -ne $pulseJob) { Remove-Job -Job $pulseJob -Force -ErrorAction SilentlyContinue }
        }
    }
}

Describe 'guardian monitoring-loss policy' {
    It 'retains a nonzero child exit without stopping for error output' {
        $r = Invoke-GuardianFixture '[Console]::Out.Write("connection failed"); Start-Sleep -Seconds 2; [Console]::Error.Write("err"); exit 7'
        $r.GuardianExitCode | Should -Be 0
        $r.Result.reason | Should -Be 'Exited'
        $r.Result.exitCode | Should -Be 7
        [IO.File]::ReadAllText((Join-Path $r.Directory 'g1b-pilot-client-stdout.txt')) | Should -Be 'connection failed'
        [IO.File]::ReadAllText((Join-Path $r.Directory 'g1b-pilot-client-stderr.txt')) | Should -Match 'err'
    }

    It 'records resource breaches but lets the child finish' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 3; exit 7' -MemoryMinimum ([long]::MaxValue) -DiskMinimum ([long]::MaxValue) -KeepPulsing
        $r.Result.reason | Should -Be 'Exited'
        $r.Result.exitCode | Should -Be 7
        (Get-Content -LiteralPath (Join-Path $r.Directory 'g1b-pilot-client-guardian.jsonl') -Raw) | Should -Match 'Resource threshold breached'
    }

    It 'stops a running child after heartbeat loss' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -HeartbeatSeconds 10 -PulseUntilStarted
        $r.GuardianExitCode | Should -Not -Be 0
        $r.Result.reason | Should -Be 'MonitorLost' -Because $r.Result.detail
        $r.Result.processId | Should -BeGreaterThan 0
        $r.Result.exitCode | Should -Not -BeNullOrEmpty
    }

    It 'counts initial heartbeat age towards the timeout' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -HeartbeatSeconds 20 -InitialHeartbeatAgeSeconds 8
        $r.Result.reason | Should -Be 'MonitorLost'
        $r.Result.processId | Should -BeGreaterThan 0
        $events = @(Get-Content -LiteralPath (Join-Path $r.Directory 'g1b-pilot-client-guardian.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        $finished = $events | Where-Object type -eq 'finished'
        (([DateTimeOffset]$finished.utc) - ([DateTimeOffset]$r.InitialHeartbeatUtc)).TotalSeconds | Should -BeLessThan 23
    }

    It 'stops the child when its guardian is terminated and monitoring is lost' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -KillGuardian
        $r.ChildExitedAfterGuardian | Should -BeTrue
    }

    It 'does not overwrite existing output or launch a child' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -ExistingStdout
        $r.Result.reason | Should -Be 'GuardianError'
        $r.Result.processId | Should -BeNullOrEmpty
        [IO.File]::ReadAllText((Join-Path $r.Directory 'g1b-pilot-client-stdout.txt')) | Should -Be 'preserve'
    }
}
