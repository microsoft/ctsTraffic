# Inventory and compare WER dump names; neither operation collects or deletes files.

$script:CtsPilotWerInventoryScript = {
    param($Directory)
    $ErrorActionPreference = 'Stop'
    if (-not [IO.Directory]::Exists($Directory)) { throw 'WER dump directory is unavailable.' }
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    if ($null -eq $os.LastBootUpTime) { throw 'Guest boot time is unavailable.' }
    $files = @(Get-ChildItem -LiteralPath $Directory -Filter '*.dmp' -File -Force -ErrorAction Stop | Sort-Object Name | ForEach-Object {
        if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'WER dump inventory contains a reparse point.' }
        [pscustomobject]@{
            Name = $_.Name; Bytes = [long]$_.Length
            CreatedUtc = $_.CreationTimeUtc.ToString('o'); ModifiedUtc = $_.LastWriteTimeUtc.ToString('o')
        }
    })
    [pscustomobject]@{
        Guest = $env:COMPUTERNAME; Directory = $Directory
        BootUtc = $os.LastBootUpTime.ToUniversalTime().ToString('o')
        ObservedUtc = [DateTime]::UtcNow.ToString('o'); FileCount = $files.Count; Files = $files
    }
}

function Assert-CtsPilotWerInventory {
    param([Parameter(Mandatory)]$Inventory)
    foreach ($field in 'Guest', 'Target', 'Directory') {
        $value = Get-CtsProp $Inventory $field
        if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) { throw "Invalid WER inventory $field." }
    }
    Assert-CtsPilotGuardianDirectory -Directory (Get-CtsProp $Inventory 'Directory')
    foreach ($field in 'BootUtc', 'ObservedUtc') {
        $value = Get-CtsProp $Inventory $field
        $parsed = [DateTimeOffset]::MinValue
        if ($value -isnot [string] -or $value -notmatch 'Z$' -or -not [DateTimeOffset]::TryParse($value, [ref]$parsed)) {
            throw "Invalid WER inventory $field."
        }
    }
    $count = Get-CtsProp $Inventory 'FileCount'
    if ($count -isnot [int] -or $count -lt 0 -or -not (Test-CtsHasProp $Inventory 'Files')) {
        throw 'Invalid WER inventory file count.'
    }
    $files = @(Get-CtsProp $Inventory 'Files')
    if ($files.Count -ne $count) { throw 'WER inventory file count does not match.' }
    $names = @{}
    foreach ($file in $files) {
        $name = Get-CtsProp $file 'Name'
        $size = Get-CtsProp $file 'Bytes'
        if ($name -isnot [string] -or $name -notmatch '^[^\\/:*?"<>|\x00-\x1f]+\.dmp$' -or
            $names.ContainsKey($name) -or ($size -isnot [int] -and $size -isnot [long]) -or $size -lt 0) {
            throw 'Invalid or duplicate WER dump record.'
        }
        $names[$name] = $true
        foreach ($field in 'CreatedUtc', 'ModifiedUtc') {
            $value = Get-CtsProp $file $field
            $parsed = [DateTimeOffset]::MinValue
            if ($value -isnot [string] -or $value -notmatch 'Z$' -or -not [DateTimeOffset]::TryParse($value, [ref]$parsed)) {
                throw 'Invalid WER dump timestamp.'
            }
        }
    }
}

function Get-CtsPilotWerInventory {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    Assert-CtsPilotGuardianDirectory -Directory $Directory
    $target = Get-CtsProp $GuestSession 'Target'
    if ($target -isnot [string] -or [string]::IsNullOrWhiteSpace($target)) { throw 'Invalid guest target.' }
    try {
        $values = @(Invoke-CtsLogged -Operation 'PilotWer:Inventory' -Channel $GuestSession.Channel -Target $target -LogPath $GuestSession.LogPath -Action {
            $job = $null
            try {
                $job = Invoke-Command -Session $GuestSession.Session -ScriptBlock $script:CtsPilotWerInventoryScript `
                    -ArgumentList @($Directory) -AsJob -ErrorAction Stop
                if ($null -eq (Wait-Job -Job $job -Timeout $TimeoutSeconds -ErrorAction Stop)) { throw 'WER inventory wait timed out.' }
                if ($job.State -ne 'Completed') { throw "WER inventory job ended in state '$($job.State)'." }
                Receive-Job -Job $job -ErrorAction Stop
            }
            finally { if ($null -ne $job) { Remove-Job -Job $job -Force -ErrorAction SilentlyContinue } }
        })
        if ($values.Count -ne 1 -or (Get-CtsProp $values[0] 'Directory') -ine $Directory) { throw 'Invalid WER inventory response.' }
        $inventory = $values[0]
        $inventory | Add-Member -NotePropertyName Target -NotePropertyValue $target -ErrorAction Stop
        Assert-CtsPilotWerInventory -Inventory $inventory
        return $inventory
    }
    catch { throw "WER inventory is unknown: $($_.Exception.Message)" }
}

function Compare-CtsPilotWerInventory {
    param([Parameter(Mandatory)]$Before, [Parameter(Mandatory)]$After)
    Assert-CtsPilotWerInventory -Inventory $Before
    Assert-CtsPilotWerInventory -Inventory $After
    foreach ($field in 'Guest', 'Target', 'Directory') {
        if ((Get-CtsProp $Before $field) -ine (Get-CtsProp $After $field)) {
            throw "WER inventory $field changed; attribution is unknown."
        }
    }
    if ([DateTimeOffset]::Parse($After.ObservedUtc) -lt [DateTimeOffset]::Parse($Before.ObservedUtc)) {
        throw 'WER inventory observation time moved backwards.'
    }
    $beforeByName = @{}
    $afterByName = @{}
    foreach ($file in @(Get-CtsProp $Before 'Files')) { $beforeByName[$file.Name] = $file }
    foreach ($file in @(Get-CtsProp $After 'Files')) { $afterByName[$file.Name] = $file }
    $new = @($afterByName.Keys | Where-Object { -not $beforeByName.ContainsKey($_) } | Sort-Object | ForEach-Object { $afterByName[$_] })
    $ambiguous = @($beforeByName.Keys | Sort-Object | ForEach-Object {
        $old = $beforeByName[$_]
        if (-not $afterByName.ContainsKey($_)) {
            [pscustomobject]@{ Name = $_; Reason = 'MissingAfter' }
        }
        else {
            $current = $afterByName[$_]
            if ($old.Bytes -ne $current.Bytes -or $old.CreatedUtc -cne $current.CreatedUtc -or $old.ModifiedUtc -cne $current.ModifiedUtc) {
                [pscustomobject]@{ Name = $_; Reason = 'Changed' }
            }
        }
    })
    [pscustomobject]@{
        Candidates = $new; Ambiguous = $ambiguous
        BootChanged = ((Get-CtsProp $Before 'BootUtc') -cne (Get-CtsProp $After 'BootUtc'))
    }
}

$script:CtsPilotWerCopyScript = {
    param($SourceDirectory, $RunDirectory, $Candidate)
    $ErrorActionPreference = 'Stop'
    $sourcePath = [IO.Path]::Combine($SourceDirectory, $Candidate.Name)
    $destinationPath = [IO.Path]::Combine($RunDirectory, ('wer-' + $Candidate.Name))
    $source = $null
    $destination = $null
    $sha = $null
    try {
        $source = [IO.File]::Open($sourcePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $item = Get-Item -LiteralPath $sourcePath -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            $source.Length -le 0 -or $source.Length -ne $Candidate.Bytes -or
            $item.CreationTimeUtc.ToString('o') -cne $Candidate.CreatedUtc -or
            $item.LastWriteTimeUtc.ToString('o') -cne $Candidate.ModifiedUtc) {
            throw 'WER dump changed or is not ready to copy.'
        }
        $destination = [IO.File]::Open($destinationPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $sha = [Security.Cryptography.SHA256]::Create()
        $sourceHash = [BitConverter]::ToString($sha.ComputeHash($source)).Replace('-', '')
        $source.Position = 0
        $source.CopyTo($destination)
        $destination.Flush($true)
        if ($destination.Length -ne $source.Length) { throw 'Copied WER dump length does not match.' }
        $destination.Position = 0
        $destinationHash = [BitConverter]::ToString($sha.ComputeHash($destination)).Replace('-', '')
        if ($sourceHash -cne $destinationHash) { throw 'Copied WER dump hash does not match.' }
        [pscustomobject]@{ SourcePath = $sourcePath; DestinationPath = $destinationPath; Bytes = $source.Length; Sha256 = $sourceHash }
    }
    finally {
        if ($null -ne $sha) { $sha.Dispose() }
        if ($null -ne $destination) { $destination.Dispose() }
        if ($null -ne $source) { $source.Dispose() }
    }
}

function Copy-CtsPilotWerCandidate {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)]$Before,
        [Parameter(Mandatory)]$After,
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$RunDirectory,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    Assert-CtsPilotGuardianDirectory -Directory $RunDirectory
    $comparison = Compare-CtsPilotWerInventory -Before $Before -After $After
    if ((Get-CtsProp $GuestSession 'Target') -ine (Get-CtsProp $After 'Target')) {
        throw 'WER inventory does not match the guest session.'
    }
    $candidates = @($comparison.Candidates | Where-Object { $_.Name -ieq $Name })
    $changed = @($comparison.Ambiguous | Where-Object { $_.Name -ieq $Name -and $_.Reason -eq 'Changed' })
    $kind = if ($candidates.Count -eq 1) { 'NewName' } elseif ($changed.Count -eq 1) { 'ChangedExisting' } else { $null }
    if ($null -eq $kind) { throw 'WER dump is not a unique new or changed candidate.' }
    $candidate = @((Get-CtsProp $After 'Files') | Where-Object { $_.Name -ieq $Name })[0]
    $sourceDirectory = Get-CtsProp $After 'Directory'
    $sourcePath = [IO.Path]::Combine($sourceDirectory, $candidate.Name)
    $destinationPath = [IO.Path]::Combine($RunDirectory, ('wer-' + $candidate.Name))
    try {
        $values = @(Invoke-CtsLogged -Operation 'PilotWer:Copy' -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
            $job = $null
            try {
                $job = Invoke-Command -Session $GuestSession.Session -ScriptBlock $script:CtsPilotWerCopyScript `
                    -ArgumentList @($sourceDirectory, $RunDirectory, $candidate) -AsJob -ErrorAction Stop
                if ($null -eq (Wait-Job -Job $job -Timeout $TimeoutSeconds -ErrorAction Stop)) { throw 'WER copy wait timed out.' }
                if ($job.State -ne 'Completed') { throw "WER copy job ended in state '$($job.State)'." }
                Receive-Job -Job $job -ErrorAction Stop
            }
            finally { if ($null -ne $job) { Remove-Job -Job $job -Force -ErrorAction SilentlyContinue } }
        })
        if ($values.Count -ne 1 -or (Get-CtsProp $values[0] 'SourcePath') -ine $sourcePath -or
            (Get-CtsProp $values[0] 'DestinationPath') -ine $destinationPath -or
            (Get-CtsProp $values[0] 'Bytes') -ne $candidate.Bytes -or
            (Get-CtsProp $values[0] 'Sha256') -isnot [string] -or
            (Get-CtsProp $values[0] 'Sha256') -cnotmatch '^[0-9A-F]{64}$') {
            throw 'Invalid WER copy receipt.'
        }
        [pscustomobject]@{
            SourcePath = $sourcePath; DestinationPath = $destinationPath
            Bytes = $values[0].Bytes; Sha256 = $values[0].Sha256
            CandidateKind = $kind; BootChanged = $comparison.BootChanged
        }
    }
    catch { throw "WER copy outcome is unknown; do not retry into the same path: $($_.Exception.Message)" }
}
