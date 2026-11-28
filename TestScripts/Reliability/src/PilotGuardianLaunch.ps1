# Starts an already-staged guardian; this does not authorize traffic.

function Assert-CtsPilotGuardianDirectory {
    param([Parameter(Mandatory)][string]$Directory)
    if ($Directory -notmatch '^[Cc]:\\[^\\]' -or $Directory -match '[\s"<>|?*\x00-\x1f]' -or
        $Directory.Substring(2) -match ':' -or
        @($Directory.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
        throw 'Guardian directory must be a non-root C: path without whitespace or dot segments.'
    }
}

function Start-CtsPilotGuardian {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)][string]$Token,
        [Parameter(Mandatory)][string]$PowerShellPath,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )

    if ($Token -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid guardian process token.' }
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    if ($PowerShellPath -notmatch '^[A-Za-z]:\\' -or $PowerShellPath -match '[\s"<>|?*\x00-\x1f]' -or
        $PowerShellPath.Substring(2) -match ':' -or
        @($PowerShellPath.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0 -or
        [IO.Path]::GetFileName($PowerShellPath) -ine 'powershell.exe') {
        throw 'Guardian host must be an absolute Windows PowerShell executable path without whitespace or dot segments.'
    }

    $scriptPath = [IO.Path]::Combine($Directory, 'PilotGuardian.ps1')
    $specPath = [IO.Path]::Combine($Directory, 'spec.json')
    $prefix = 'g1b-pilot-' + $Role.ToLowerInvariant() + '-guardian-process'
    $stdoutPath = [IO.Path]::Combine($Directory, "$prefix-stdout.txt")
    $stderrPath = [IO.Path]::Combine($Directory, "$prefix-stderr.txt")

    $support = Join-Path (Split-Path -Parent $PSScriptRoot) 'support'
    $expected = @{}
    foreach ($name in 'PilotGuardian.ps1', 'PilotSelfJob.cs') {
        $expected[$name] = (Get-FileHash -LiteralPath (Join-Path $support $name) -Algorithm SHA256 -ErrorAction Stop).Hash
    }
    $verified = @(Invoke-CtsGuestCommand -GuestSession $GuestSession -Operation 'PilotGuardian:VerifySource' `
        -ArgumentList @($Directory, $expected) -ScriptBlock {
            param($directory, $expected)
            foreach ($name in 'PilotGuardian.ps1', 'PilotSelfJob.cs') {
                if ((Get-FileHash -LiteralPath ([IO.Path]::Combine($directory, $name)) -Algorithm SHA256 -ErrorAction Stop).Hash -cne $expected[$name]) {
                    throw "Guardian file hash mismatch: $name"
                }
            }
            return $true
        })
    if ($verified.Count -ne 1 -or $verified[0] -isnot [bool] -or -not $verified[0]) {
        throw 'Guardian files could not be verified; refusing launch.'
    }

    $heartbeat = Write-CtsPilotHeartbeat -GuestSession $GuestSession -Directory $Directory -Action Create -TimeoutSeconds $TimeoutSeconds
    $process = Invoke-CtsPilotProcessAction -GuestSession $GuestSession -Action Start -Token $Token -FilePath $PowerShellPath `
        -Arguments @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'RemoteSigned', '-File', $scriptPath, '-SpecPath', $specPath) `
        -StdoutPath $stdoutPath -StderrPath $stderrPath -TimeoutSeconds $TimeoutSeconds

    [pscustomobject]@{
        Token = $Token
        ProcessId = $process.ProcessId
        StartedUtc = $process.StartedUtc
        HeartbeatPath = $heartbeat.Path
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
    }
}
