# Transfers an already-preserved guest dump into a new host subdirectory.

function Copy-CtsPilotGuestFile {
    param($GuestSession, [string]$SourcePath, [string]$DestinationDirectory)
    Invoke-CtsLogged -Operation 'PilotWer:Transfer' -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
        Copy-Item -LiteralPath $SourcePath -Destination $DestinationDirectory -FromSession $GuestSession.Session -ErrorAction Stop
    }
}

function Copy-CtsPilotWerToHost {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)]$Receipt,
        [Parameter(Mandatory)][string]$GuestRunDirectory,
        [Parameter(Mandatory)][string]$HostRunDirectory,
        [Parameter(Mandatory)][string]$TransferToken
    )
    Assert-CtsPilotGuardianDirectory -Directory $GuestRunDirectory
    Assert-CtsPilotGuardianDirectory -Directory $HostRunDirectory
    if ($TransferToken -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid transfer token.' }
    $target = Get-CtsProp $GuestSession 'Target'
    $guest = Get-CtsProp $Receipt 'Guest'
    $path = Get-CtsProp $Receipt 'DestinationPath'
    $bytes = Get-CtsProp $Receipt 'Bytes'
    $hash = Get-CtsProp $Receipt 'Sha256'
    $kind = Get-CtsProp $Receipt 'CandidateKind'
    $bootChanged = Get-CtsProp $Receipt 'BootChanged'
    if ($target -isnot [string] -or [string]::IsNullOrWhiteSpace($target) -or
        (Get-CtsProp $Receipt 'Target') -ine $target -or $guest -isnot [string] -or [string]::IsNullOrWhiteSpace($guest) -or
        $path -isnot [string] -or ($bytes -isnot [int] -and $bytes -isnot [long]) -or $bytes -le 0 -or
        $hash -isnot [string] -or $hash -cnotmatch '^[0-9A-F]{64}$' -or
        $kind -cnotin @('NewName', 'ChangedExisting') -or $bootChanged -isnot [bool]) {
        throw 'Invalid guest WER copy receipt.'
    }
    $name = [IO.Path]::GetFileName($path)
    if ($name -notmatch '^wer-[^\\/:*?"<>|\x00-\x1f]+\.dmp$' -or
        $path -ine [IO.Path]::Combine($GuestRunDirectory, $name)) {
        throw 'WER copy receipt does not match the guest run directory.'
    }
    $hostDirectory = & $script:CtsPilotDirectoryScript $HostRunDirectory $TransferToken
    $hostPath = [IO.Path]::Combine($hostDirectory, $name)
    try {
        Copy-CtsPilotGuestFile -GuestSession $GuestSession -SourcePath $path -DestinationDirectory $hostDirectory
        $item = Get-Item -LiteralPath $hostPath -Force -ErrorAction Stop
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Transferred WER file is not a regular file.'
        }
        $file = [IO.File]::Open($hostPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            if ($file.Length -ne $bytes) { throw 'Transferred WER file length does not match.' }
            $actual = [BitConverter]::ToString($sha.ComputeHash($file)).Replace('-', '')
            if ($actual -cne $hash) { throw 'Transferred WER file hash does not match.' }
        }
        finally { $sha.Dispose(); $file.Dispose() }
        [pscustomobject]@{
            Target = $target; Guest = $guest; GuestPath = $path; HostPath = $hostPath
            Bytes = $bytes; Sha256 = $hash; CandidateKind = $kind; BootChanged = $bootChanged
        }
    }
    catch { throw "WER transfer outcome is unknown; preserve '$hostDirectory' and do not retry with the same token: $($_.Exception.Message)" }
}
