# Stages guardian inputs without starting a process or authorizing traffic.

$script:CtsPilotGuardianSupportDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'support'
$script:CtsPilotGuardianStageScript = {
    param($Directory, $Payload)

    $ErrorActionPreference = 'Stop'
    if ($Directory -notmatch '^[Cc]:\\[^\\]' -or $Directory -match '[\s"<>|?*\x00-\x1f]' -or
        $Directory.Substring(2) -match ':' -or
        @($Directory.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
        throw 'Invalid guardian directory.'
    }
    foreach ($name in @('PilotGuardian.ps1', 'PilotSelfJob.cs', 'spec.json')) {
        $path = [IO.Path]::Combine($Directory, $name)
        $bytes = [Convert]::FromBase64String($Payload[$name])
        $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        try { $stream.Write($bytes, 0, $bytes.Length) }
        finally { $stream.Dispose() }
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            $inputStream = [IO.File]::OpenRead($path)
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($inputStream)).Replace('-', '') }
            finally { $inputStream.Dispose() }
        }
        finally { $sha.Dispose() }
        [pscustomobject]@{ Name = $name; Path = $path; Sha256 = $hash }
    }
}

function New-CtsPilotGuardianStage {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][ValidateRange(1, [long]::MaxValue)][long]$MinimumFreeMemoryBytes,
        [Parameter(Mandatory)][ValidateRange(1, [long]::MaxValue)][long]$MinimumFreeDiskBytes,
        [Parameter(Mandatory)][ValidateRange(2, 600)][int]$HeartbeatTimeoutSeconds,
        [Parameter(Mandatory)][ValidateRange(1, 3600)][int]$MaximumLifetimeSeconds,
        [Parameter(Mandatory)][ValidateRange(100, 5000)][int]$PollMilliseconds
    )
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    if ($Executable -notmatch '^[A-Za-z]:\\' -or $Executable -match '[\s"<>|?*\x00-\x1f]' -or
        $Executable.Substring(2) -match ':' -or
        @($Executable.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
        throw 'Guardian child executable must be an absolute local path without whitespace or dot segments.'
    }
    foreach ($argument in $Arguments) {
        if ($argument -eq '' -or $argument -match '[\s"\x00]') {
            throw 'Guardian arguments must be nonempty unquoted tokens without whitespace.'
        }
    }
    if ($PollMilliseconds -ge ($HeartbeatTimeoutSeconds * 1000)) {
        throw 'Guardian polling interval must be shorter than the heartbeat timeout.'
    }

    $spec = [ordered]@{
        role = $Role.ToLowerInvariant()
        executable = $Executable
        arguments = @($Arguments)
        minimumFreeMemoryBytes = $MinimumFreeMemoryBytes
        minimumFreeDiskBytes = $MinimumFreeDiskBytes
        heartbeatTimeoutSeconds = $HeartbeatTimeoutSeconds
        maximumLifetimeSeconds = $MaximumLifetimeSeconds
        pollMilliseconds = $PollMilliseconds
    }
    $names = @('PilotGuardian.ps1', 'PilotSelfJob.cs', 'spec.json')
    $payload = @{}
    $expected = @{}
    foreach ($name in $names) {
        [byte[]]$bytes = if ($name -eq 'spec.json') {
            [Text.UTF8Encoding]::new($false).GetBytes(($spec | ConvertTo-Json -Depth 4))
        }
        else {
            [IO.File]::ReadAllBytes([IO.Path]::Combine($script:CtsPilotGuardianSupportDirectory, $name))
        }
        $payload[$name] = [Convert]::ToBase64String($bytes)
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $expected[$name] = [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '') }
        finally { $sha.Dispose() }
    }
    try {
        $results = @(Invoke-CtsGuestCommand -GuestSession $GuestSession -ScriptBlock $script:CtsPilotGuardianStageScript `
            -ArgumentList @($Directory, $payload) -Operation 'PilotGuardian:Stage')
    }
    catch {
        throw "Guardian staging did not complete reliably; the staging outcome is unknown: $($_.Exception.Message)"
    }
    if ($results.Count -ne $names.Count) { throw 'Guardian staging did not return all file hashes; the staging outcome is unknown.' }
    foreach ($name in $names) {
        $entries = @($results | Where-Object { (Get-CtsProp $_ 'Name') -ceq $name })
        if ($entries.Count -ne 1 -or (Get-CtsProp $entries[0] 'Path') -ine [IO.Path]::Combine($Directory, $name) -or
            (Get-CtsProp $entries[0] 'Sha256') -cne $expected[$name]) {
            throw "Guardian staging did not verify '$name'; the staging outcome is unknown."
        }
    }
    return $results
}
