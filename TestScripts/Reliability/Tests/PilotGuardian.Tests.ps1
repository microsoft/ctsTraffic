#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    $script:GuardianPath = (Resolve-Path (Join-Path $PSScriptRoot '..\support\PilotGuardian.ps1')).Path
    $script:PowerShellPath = (Get-Command powershell.exe).Source
    function Invoke-GuardianFixture {
        param([string]$Command, [int]$HeartbeatSeconds = 20, [int]$LifetimeSeconds = 20,
            [long]$MemoryMinimum = 1, [long]$DiskMinimum = 1, [switch]$ExistingStdout, [switch]$Pulse,
            [int]$InitialHeartbeatAgeSeconds = 0, [switch]$RemoveHeartbeatOnExit, [switch]$SetFutureHeartbeat)
        $directory = Join-Path $TestDrive ([guid]::NewGuid().ToString('N'))
        $null = New-Item -ItemType Directory -Path $directory
        $heartbeat = Join-Path $directory 'heartbeat.txt'
        [IO.File]::WriteAllText($heartbeat, 'alive')
        [IO.File]::SetLastWriteTimeUtc($heartbeat, [DateTime]::UtcNow.AddSeconds(-$InitialHeartbeatAgeSeconds))
        $initialHeartbeatUtc = [IO.File]::GetLastWriteTimeUtc($heartbeat)
        if ($RemoveHeartbeatOnExit) {
            $Command += "; Remove-Item -LiteralPath '$($heartbeat -replace "'", "''")' -ErrorAction Stop"
        }
        if ($SetFutureHeartbeat) {
            $Command += "; [IO.File]::SetLastWriteTimeUtc('$($heartbeat -replace "'", "''")', [DateTime]::UtcNow.AddMinutes(1)); Start-Sleep -Seconds 10"
        }
        $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Command))
        $spec = [ordered]@{
            role = 'client'; executable = $script:PowerShellPath
            arguments = @('-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded)
            minimumFreeMemoryBytes = $MemoryMinimum; minimumFreeDiskBytes = $DiskMinimum
            heartbeatTimeoutSeconds = $HeartbeatSeconds; maximumLifetimeSeconds = $LifetimeSeconds
            pollMilliseconds = 200
        }
        $specPath = Join-Path $directory 'spec.json'
        [IO.File]::WriteAllText($specPath, ($spec | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        $pulseJob = if ($Pulse) {
            Start-Job -ArgumentList $heartbeat -ScriptBlock {
                param($Path)
                for ($i = 0; $i -lt 30; $i++) {
                    [IO.File]::SetLastWriteTimeUtc($Path, [DateTime]::UtcNow)
                    Start-Sleep -Milliseconds 500
                }
            }
        } else { $null }
        $stdoutPath = Join-Path $directory 'g1b-pilot-client-stdout.txt'
        if ($ExistingStdout) { [IO.File]::WriteAllText($stdoutPath, 'preserve') }
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $script:PowerShellPath
        $info.Arguments = '-NoProfile -NonInteractive -File "{0}" -SpecPath "{1}"' -f $script:GuardianPath, $specPath
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $guardian = [Diagnostics.Process]::Start($info)
        $handle = $guardian.SafeHandle
        $pinned = $false
        $handle.DangerousAddRef([ref]$pinned)
        try {
            if (-not $guardian.WaitForExit(45000)) { throw 'Guardian fixture did not exit.' }
            $resultPath = Join-Path $directory 'g1b-pilot-client-guardian-result.json'
            [pscustomobject]@{
                Directory = $directory
                GuardianExitCode = $guardian.ExitCode
                InitialHeartbeatUtc = $initialHeartbeatUtc
                Result = if (Test-Path -LiteralPath $resultPath) { Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json } else { $null }
            }
        }
        finally {
            if (-not $guardian.HasExited) { Stop-Process -Id $guardian.Id -ErrorAction SilentlyContinue }
            if ($pinned) { $handle.DangerousRelease() }
            $guardian.Dispose()
            if ($null -ne $pulseJob) { Remove-Job -Job $pulseJob -Force -ErrorAction SilentlyContinue }
        }
    }
}

Describe 'standalone pilot guardian' {
    It 'captures output and exit code when a child completes' {
        $r = Invoke-GuardianFixture '[Console]::Out.Write("out"); [Console]::Error.Write("err"); exit 7'
        $r.GuardianExitCode | Should -Be 0 -Because $r.Result.detail
        $r.Result.reason | Should -Be 'Exited'
        $r.Result.exitCode | Should -Be 7
        [IO.File]::ReadAllText((Join-Path $r.Directory 'g1b-pilot-client-stdout.txt')) | Should -Be 'out'
        [IO.File]::ReadAllText((Join-Path $r.Directory 'g1b-pilot-client-stderr.txt')) | Should -Be 'err'
    }

    It 'stops a running child after heartbeat loss' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -HeartbeatSeconds 3
        $r.GuardianExitCode | Should -Not -Be 0
        $r.Result.reason | Should -Be 'MonitorLost' -Because $r.Result.detail
        $r.Result.processId | Should -BeGreaterThan 0
        $r.Result.exitCode | Should -Not -BeNullOrEmpty
    }

    It 'counts the age of the initial heartbeat toward the timeout' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -HeartbeatSeconds 15 -InitialHeartbeatAgeSeconds 9
        $r.Result.reason | Should -Be 'MonitorLost' -Because $r.Result.detail
        $r.Result.processId | Should -BeGreaterThan 0
        $events = @(Get-Content -LiteralPath (Join-Path $r.Directory 'g1b-pilot-client-guardian.jsonl') |
            ForEach-Object { $_ | ConvertFrom-Json })
        $finished = $events | Where-Object type -eq 'finished'
        (([DateTimeOffset]$finished.utc) - ([DateTimeOffset]$r.InitialHeartbeatUtc)).TotalSeconds | Should -BeLessThan 18
    }

    It 'does not launch a child for a heartbeat too far in the future' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -InitialHeartbeatAgeSeconds -30
        $r.Result.reason | Should -Be 'MonitorLost' -Because $r.Result.detail
        $r.Result.processId | Should -BeNullOrEmpty
    }

    It 'stops a running child when a later heartbeat is too far in the future' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 1' -SetFutureHeartbeat
        $r.Result.reason | Should -Be 'MonitorLost' -Because $r.Result.detail
        $r.Result.processId | Should -BeGreaterThan 0
        $r.Result.exitCode | Should -Not -BeNullOrEmpty
    }

    It 'checks monitoring even when the child exits' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Milliseconds 100' -RemoveHeartbeatOnExit
        $r.Result.reason | Should -Be 'MonitorLost' -Because $r.Result.detail
        $r.Result.processId | Should -BeGreaterThan 0
        $r.Result.exitCode | Should -Be 0
    }

    It 'keeps a running child alive while heartbeats advance' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 6; exit 0' -HeartbeatSeconds 3 -Pulse
        $r.Result.reason | Should -Be 'Exited' -Because $r.Result.detail
        $r.Result.exitCode | Should -Be 0
    }

    It 'stops a running child at its guardian watchdog limit' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -LifetimeSeconds 2
        $r.Result.reason | Should -Be 'Watchdog' -Because $r.Result.detail
        $r.Result.processId | Should -BeGreaterThan 0
        $r.Result.exitCode | Should -Not -BeNullOrEmpty
    }

    It 'does not launch a child when available memory is below the minimum' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -MemoryMinimum ([long]::MaxValue)
        $r.Result.reason | Should -Be 'ResourceLimit' -Because $r.Result.detail
        $r.Result.processId | Should -BeNullOrEmpty
    }

    It 'does not launch a child when disk space is below the minimum' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -DiskMinimum ([long]::MaxValue)
        $r.Result.reason | Should -Be 'ResourceLimit' -Because $r.Result.detail
        $r.Result.processId | Should -BeNullOrEmpty
    }

    It 'does not overwrite an existing output file or launch a child' {
        $r = Invoke-GuardianFixture 'Start-Sleep -Seconds 30' -ExistingStdout
        $r.Result.reason | Should -Be 'GuardianError'
        $r.Result.processId | Should -BeNullOrEmpty
        [IO.File]::ReadAllText((Join-Path $r.Directory 'g1b-pilot-client-stdout.txt')) | Should -Be 'preserve'
    }
}
