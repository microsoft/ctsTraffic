# Windows ISO host cache for the golden image build.
# Policy: documents/reliability/lab-configuration.md (Guest image).

function Get-CtsIsoPath {
    <#
    .SYNOPSIS
    Returns the cached ISO, partial-download, and metadata paths under <labRoot>\iso.
    #>
    param([Parameter(Mandatory)]$Definition, [string]$Architecture = (Get-CtsHostArchitecture))
    $fileName = 'windows-{arch}.iso'
    $iso = Get-CtsProp $Definition.goldenImage 'iso'
    if ($iso -and (Get-CtsProp $iso 'fileName')) { $fileName = $iso.fileName }
    $path = Join-Path (Join-Path $Definition.labRoot 'iso') ([string]$fileName).Replace('{arch}', $Architecture)
    return [pscustomobject]@{ Iso = $path; Partial = "$path.partial"; Metadata = "$path.json" }
}

function Get-CtsPinnedIsoHash {
    <#
    .SYNOPSIS
    Returns goldenImage.iso.sha256.<architecture> from the lab definition, or $null if not pinned.
    #>
    param([Parameter(Mandatory)]$Definition, [string]$Architecture = (Get-CtsHostArchitecture))
    $iso = Get-CtsProp $Definition.goldenImage 'iso'
    if ($iso) {
        $hashes = Get-CtsProp $iso 'sha256'
        if ($hashes) { $value = Get-CtsProp $hashes $Architecture; if ($value) { return ([string]$value).ToUpperInvariant() } }
    }
    return $null
}

function ConvertTo-CtsSafeSource {
    <#
    .SYNOPSIS
    Removes the query string and fragment from a URL so time-limited download tokens are never recorded.
    #>
    param([Parameter(Mandatory)][string]$Source)
    if ($Source -match '^[a-z][a-z0-9+.-]*://') { return ($Source -replace '[?#].*$', '') }
    return $Source
}

function Get-CtsIsoCacheEntry {
    <#
    .SYNOPSIS
    Returns the cached ISO metadata with its path, or $null when nothing is cached.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Definition)
    $paths = Get-CtsIsoPath -Definition $Definition
    $hasIso = Test-Path -LiteralPath $paths.Iso -PathType Leaf
    $hasMetadata = Test-Path -LiteralPath $paths.Metadata -PathType Leaf
    if (-not $hasIso -and -not $hasMetadata) { return $null }
    if (-not ($hasIso -and $hasMetadata)) {
        throw "ISO cache is inconsistent: expected both '$($paths.Iso)' and '$($paths.Metadata)'. Delete the leftover file and download again."
    }
    $entry = Get-Content -LiteralPath $paths.Metadata -Raw | ConvertFrom-Json
    $entry | Add-Member -NotePropertyName isoPath -NotePropertyValue $paths.Iso -Force
    return $entry
}

function Get-CtsIsoPlan {
    <#
    .SYNOPSIS
    Read-only decision for the ISO cache: UseCached, Download, or Blocked, with reasons.
    #>
    param(
        [Parameter(Mandatory)]$Definition,
        [string]$Source,
        [string]$ExpectedSha256,
        [switch]$AllowUnverified,
        [switch]$Refresh,
        [long]$MinimumFreeBytes = 12GB
    )
    $paths = Get-CtsIsoPath -Definition $Definition
    $reasons = [System.Collections.Generic.List[string]]::new()
    $notes = [System.Collections.Generic.List[string]]::new()
    $cached = $null
    try { $cached = Get-CtsIsoCacheEntry -Definition $Definition } catch { $reasons.Add($_.Exception.Message) }

    $pinned = Get-CtsPinnedIsoHash -Definition $Definition
    $expected = if ($ExpectedSha256) { $ExpectedSha256.Trim().ToUpperInvariant() } else { $null }
    if ($expected -and $expected -notmatch '^[0-9A-F]{64}$') { $reasons.Add("ExpectedSha256 is not a 64-character hex SHA-256: $ExpectedSha256") }
    if ($expected -and $pinned -and $expected -ne $pinned) { $reasons.Add("ExpectedSha256 $expected differs from goldenImage.iso.sha256.$(Get-CtsHostArchitecture) $pinned in the lab definition.") }
    if (-not $expected) { $expected = $pinned }

    $action = 'Download'
    if ($cached -and -not $Refresh) {
        $action = 'UseCached'
        if ($Source) { $notes.Add("An ISO is already cached; -Source is ignored. Use -Refresh to replace it.") }
        if ($expected -and $cached.sha256 -ne $expected) { $reasons.Add("Cached ISO SHA-256 $($cached.sha256) does not match the expected $expected. Use -Refresh with a matching source.") }
    }
    else {
        if (-not $Source) {
            $reasons.Add('No ISO is cached and no -Source was given. Supply a share or file path, or an https download URL.')
        }
        elseif ($Source -match '^[a-z][a-z0-9+.-]*://') {
            if ($Source -notmatch '^https://') { $reasons.Add("Only https URLs are accepted: $(ConvertTo-CtsSafeSource $Source)") }
        }
        elseif (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
            $reasons.Add("Source file not found: $Source")
        }
        if (-not $expected -and -not $AllowUnverified) {
            $reasons.Add('No expected SHA-256. Pass -ExpectedSha256 with the hash Microsoft publishes for this ISO, set goldenImage.iso.sha256.<arch>, or pass -AllowUnverified.')
        }
        $root = [IO.Path]::GetPathRoot($paths.Iso)
        try {
            $free = ([IO.DriveInfo]::new($root)).AvailableFreeSpace
            if ($free -lt $MinimumFreeBytes) { $reasons.Add(("Only {0:N1} GiB free on {1}; need {2:N0} GiB." -f ($free / 1GB), $root, ($MinimumFreeBytes / 1GB))) }
        }
        catch { $reasons.Add("Cannot read free space on ${root}: $($_.Exception.Message)") }
        if (Test-Path -LiteralPath $paths.Partial) { $notes.Add("A previous download left $($paths.Partial); it will be deleted and downloaded again.") }
    }

    return [pscustomobject]@{
        Action         = if ($reasons.Count -gt 0) { 'Blocked' } else { $action }
        Blocked        = ($reasons.Count -gt 0)
        Reasons        = @($reasons)
        Notes          = @($notes)
        Source         = if ($Source) { ConvertTo-CtsSafeSource $Source } else { $null }
        ExpectedSha256 = $expected
        IsoPath        = $paths.Iso
        CachedSha256   = if ($cached) { $cached.sha256 } else { $null }
    }
}

function Save-CtsIsoFile {
    # Transfers the source to Destination. https uses BITS when available, else Invoke-WebRequest.
    param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Destination)
    if ($Source -match '^https://') {
        if (Get-Command Start-BitsTransfer -ErrorAction SilentlyContinue) {
            Start-BitsTransfer -Source $Source -Destination $Destination -DisplayName 'ctsReliability ISO' -ErrorAction Stop
        }
        else {
            $progress = $ProgressPreference
            try { $ProgressPreference = 'SilentlyContinue'; Invoke-WebRequest -Uri $Source -OutFile $Destination -UseBasicParsing -ErrorAction Stop }
            finally { $ProgressPreference = $progress }
        }
    }
    else {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
    }
}

function Get-CtsWindowsIso {
    <#
    .SYNOPSIS
    Plans (default) or fills (-Apply) the host's Windows ISO cache and returns the cached ISO.
    .DESCRIPTION
    Downloads or copies from -Source only when nothing is cached or -Refresh is given. The SHA-256 must match
    -ExpectedSha256 or goldenImage.iso.sha256.<arch> unless -AllowUnverified. The URL query string (which carries
    time-limited tokens) is never recorded. Apply re-verifies an existing cached ISO against its metadata.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [string]$Source,
        [string]$ExpectedSha256,
        [switch]$AllowUnverified,
        [switch]$Refresh,
        [switch]$Apply,
        [string]$LogPath
    )
    $plan = Get-CtsIsoPlan -Definition $Definition -Source $Source -ExpectedSha256 $ExpectedSha256 -AllowUnverified:$AllowUnverified -Refresh:$Refresh
    if (-not $Apply) { return $plan }
    if ($plan.Blocked) { throw "ISO cache is blocked: $($plan.Reasons -join ' ')" }
    $paths = Get-CtsIsoPath -Definition $Definition
    $log = { param($op, $ok, $detail) Write-CtsLogEntry -LogPath $LogPath -Entry ([ordered]@{ startUtc = [DateTime]::UtcNow.ToString('o'); channel = 'Host'; target = $paths.Iso; operation = $op; success = $ok; detail = $detail }) }

    if ($plan.Action -eq 'UseCached') {
        $entry = Get-CtsIsoCacheEntry -Definition $Definition
        $hash = (Get-FileHash -LiteralPath $paths.Iso -Algorithm SHA256).Hash
        if ($hash -ne $entry.sha256) {
            & $log 'IsoVerify' $false "hash $hash does not match metadata $($entry.sha256)"
            throw "Cached ISO '$($paths.Iso)' has SHA-256 $hash but its metadata records $($entry.sha256). Use -Refresh to replace it."
        }
        & $log 'IsoVerify' $true $hash
        return $entry
    }

    $directory = Split-Path -Parent $paths.Iso
    if (-not (Test-Path -LiteralPath $directory)) { $null = New-Item -ItemType Directory -Path $directory -Force }
    if (Test-Path -LiteralPath $paths.Partial) { Remove-Item -LiteralPath $paths.Partial -Force }
    try {
        Save-CtsIsoFile -Source $Source -Destination $paths.Partial
        $hash = (Get-FileHash -LiteralPath $paths.Partial -Algorithm SHA256).Hash
        if ($plan.ExpectedSha256 -and $hash -ne $plan.ExpectedSha256) {
            throw "Downloaded SHA-256 $hash does not match the expected $($plan.ExpectedSha256)."
        }
    }
    catch {
        Remove-Item -LiteralPath $paths.Partial -Force -ErrorAction SilentlyContinue
        & $log 'IsoDownload' $false "$($plan.Source): $($_.Exception.Message)"
        throw "ISO download from '$($plan.Source)' failed: $($_.Exception.Message)"
    }

    if (Test-Path -LiteralPath $paths.Iso) {
        Set-ItemProperty -LiteralPath $paths.Iso -Name IsReadOnly -Value $false
        Remove-Item -LiteralPath $paths.Iso -Force
    }
    Move-Item -LiteralPath $paths.Partial -Destination $paths.Iso
    Set-ItemProperty -LiteralPath $paths.Iso -Name IsReadOnly -Value $true
    $metadata = [ordered]@{
        fileName      = Split-Path -Leaf $paths.Iso
        sha256        = $hash
        sizeBytes     = (Get-Item -LiteralPath $paths.Iso).Length
        verification  = if ($plan.ExpectedSha256) { 'matchedExpected' } else { 'unverified' }
        source        = $plan.Source
        cachedUtc     = [DateTime]::UtcNow.ToString('o')
        replacedSha256 = $plan.CachedSha256
    }
    $metadata | ConvertTo-Json | Set-Content -LiteralPath $paths.Metadata -Encoding UTF8
    & $log 'IsoDownload' $true "$($plan.Source) $hash $($metadata.verification)"
    return (Get-CtsIsoCacheEntry -Definition $Definition)
}
