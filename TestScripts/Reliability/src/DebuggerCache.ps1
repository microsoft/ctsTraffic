# Debugger package (cdb, ntsd, Application Verifier) host cache and installation.
# Policy: documents/reliability/debugging-tools.md.

function Compare-CtsVersion {
    <#
    .SYNOPSIS
    Compares two dotted version strings numerically. Returns -1, 0, or 1.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Left,
        [Parameter(Mandatory)][string]$Right
    )
    $parse = {
        param($text)
        $parts = @($text.Trim().Split('.') | ForEach-Object {
                if ($_ -notmatch '^\d+$') { throw "Not a numeric version: '$text'" }
                [int64]$_
            })
        $parts
    }
    $a = @(& $parse $Left)
    $b = @(& $parse $Right)
    $length = [math]::Max($a.Count, $b.Count)
    for ($i = 0; $i -lt $length; $i++) {
        $x = if ($i -lt $a.Count) { $a[$i] } else { 0 }
        $y = if ($i -lt $b.Count) { $b[$i] } else { 0 }
        if ($x -lt $y) { return -1 }
        if ($x -gt $y) { return 1 }
    }
    return 0
}

function Get-CtsMsiProperty {
    <#
    .SYNOPSIS
    Reads ProductVersion, ProductCode, ProductName, and Manufacturer from an MSI without installing it.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)
    $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).ProviderPath
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $null
    $result = [ordered]@{}
    try {
        $database = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @($resolved, 0))
        foreach ($name in 'ProductVersion', 'ProductCode', 'ProductName', 'Manufacturer') {
            $view = $database.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $database, @("SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = '$name'"))
            try {
                $null = $view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null)
                $record = $view.GetType().InvokeMember('Fetch', 'InvokeMethod', $null, $view, $null)
                $result[$name] = if ($record) { $record.GetType().InvokeMember('StringData', 'GetProperty', $null, $record, @(1)) } else { $null }
                if ($record) { $null = [System.Runtime.InteropServices.Marshal]::ReleaseComObject($record) }
            }
            finally {
                $null = $view.GetType().InvokeMember('Close', 'InvokeMethod', $null, $view, $null)
                $null = [System.Runtime.InteropServices.Marshal]::ReleaseComObject($view)
            }
        }
    }
    finally {
        if ($database) { $null = [System.Runtime.InteropServices.Marshal]::ReleaseComObject($database) }
        $null = [System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer)
        [GC]::Collect(); [GC]::WaitForPendingFinalizers()
    }
    if (-not $result.ProductVersion) { throw "MSI '$resolved' has no ProductVersion property." }
    return [pscustomobject]$result
}

function Get-CtsDebuggerCacheRoot {
    param([Parameter(Mandatory)]$Definition)
    return (Join-Path $Definition.labRoot 'cache\dbg')
}

function Get-CtsDebuggerCacheEntry {
    <#
    .SYNOPSIS
    Returns the metadata of the current cached debugger MSI for an architecture, or $null when none is cached.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)][ValidateSet('amd64', 'arm64')][string]$Architecture,
        [string]$CacheRoot
    )
    if (-not $CacheRoot) { $CacheRoot = Get-CtsDebuggerCacheRoot $Definition }
    $pointer = Join-Path $CacheRoot "$Architecture\current.json"
    if (-not (Test-Path -LiteralPath $pointer)) { return $null }
    $entry = Get-Content -LiteralPath $pointer -Raw | ConvertFrom-Json
    $msiPath = Join-Path $CacheRoot "$Architecture\$($entry.productVersion)\$($entry.fileName)"
    if (-not (Test-Path -LiteralPath $msiPath)) { throw "Debugger cache is inconsistent: '$pointer' points to missing '$msiPath'." }
    $entry | Add-Member -NotePropertyName msiPath -NotePropertyValue $msiPath -Force
    return $entry
}

function Update-CtsDebuggerCache {
    <#
    .SYNOPSIS
    Applies the debugger cache refresh rule for one architecture and returns what happened.
    .DESCRIPTION
    Refreshes only when the source share is reachable and its MSI ProductVersion is newer than the cached
    version (or nothing is cached). Never downgrades. Keeps the current and previous versions.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)][ValidateSet('amd64', 'arm64')][string]$Architecture,
        [string]$SourcePath,
        [string]$CacheRoot,
        [string]$LogPath
    )
    if (-not $SourcePath) { $SourcePath = $Definition.debugger.sources.$Architecture }
    if (-not $CacheRoot) { $CacheRoot = Get-CtsDebuggerCacheRoot $Definition }
    $archRoot = Join-Path $CacheRoot $Architecture
    $warnings = New-Object System.Collections.Generic.List[string]
    $cached = Get-CtsDebuggerCacheEntry -Definition $Definition -Architecture $Architecture -CacheRoot $CacheRoot
    $result = [ordered]@{
        architecture        = $Architecture
        sourcePath          = $SourcePath
        shareReachable      = $false
        sourceVersion       = $null
        cachedVersionBefore = if ($cached) { $cached.productVersion } else { $null }
        cachedVersion       = if ($cached) { $cached.productVersion } else { $null }
        refreshed           = $false
        msiPath             = if ($cached) { $cached.msiPath } else { $null }
        warnings            = $warnings
    }

    $reachable = $false
    try { $reachable = [bool](Test-Path -LiteralPath $SourcePath -PathType Leaf -ErrorAction Stop) } catch { $reachable = $false }
    $result.shareReachable = $reachable

    if (-not $reachable) {
        if (-not $cached) { throw "No cached debugger MSI for $Architecture and the source '$SourcePath' is not accessible." }
        $warnings.Add("Source '$SourcePath' not accessible; using cached $($cached.productVersion).")
    }
    else {
        $source = Get-CtsMsiProperty -Path $SourcePath
        $result.sourceVersion = $source.ProductVersion
        $comparison = if ($cached) { Compare-CtsVersion $source.ProductVersion $cached.productVersion } else { 1 }
        if ($comparison -gt 0) {
            $versionDir = Join-Path $archRoot $source.ProductVersion
            $fileName = Split-Path -Leaf $SourcePath
            $null = New-Item -ItemType Directory -Path $versionDir -Force
            $temp = Join-Path $versionDir "$fileName.partial"
            Copy-Item -LiteralPath $SourcePath -Destination $temp -Force -ErrorAction Stop
            try {
                $sourceLength = (Get-Item -LiteralPath $SourcePath).Length
                $copyLength = (Get-Item -LiteralPath $temp).Length
                if ($copyLength -ne $sourceLength) { throw "Copy is incomplete ($copyLength of $sourceLength bytes)." }
                $signature = Get-AuthenticodeSignature -LiteralPath $temp
                if ([string]$signature.Status -ne 'Valid') { throw "Authenticode signature is not valid: $($signature.Status) $($signature.StatusMessage)" }
                $copied = Get-CtsMsiProperty -Path $temp
                if ($copied.ProductVersion -ne $source.ProductVersion) { throw "Copied MSI version $($copied.ProductVersion) does not match source version $($source.ProductVersion)." }
                $final = Join-Path $versionDir $fileName
                Move-Item -LiteralPath $temp -Destination $final -Force
            }
            catch {
                Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
                throw "Debugger cache refresh for $Architecture failed: $($_.Exception.Message)"
            }
            $metadata = [ordered]@{
                architecture    = $Architecture
                fileName        = $fileName
                productVersion  = $copied.ProductVersion
                productCode     = $copied.ProductCode
                productName     = $copied.ProductName
                sha256          = (Get-FileHash -LiteralPath $final -Algorithm SHA256).Hash
                signatureStatus = [string]$signature.Status
                signer          = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null }
                sourcePath      = $SourcePath
                copiedUtc       = [DateTime]::UtcNow.ToString('o')
            }
            $metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $versionDir 'metadata.json') -Encoding UTF8
            $metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $archRoot 'current.json') -Encoding UTF8

            # Keep only the current and the previous version.
            $keep = @($copied.ProductVersion)
            if ($cached) { $keep += $cached.productVersion }
            foreach ($dir in @(Get-ChildItem -LiteralPath $archRoot -Directory)) {
                if ($keep -notcontains $dir.Name) { Remove-Item -LiteralPath $dir.FullName -Recurse -Force }
            }
            $result.refreshed = $true
            $result.cachedVersion = $copied.ProductVersion
            $result.msiPath = $final
        }
        elseif ($comparison -eq 0) {
            $sourceHash = (Get-FileHash -LiteralPath $SourcePath -Algorithm SHA256).Hash
            if ($sourceHash -ne $cached.sha256) {
                $warnings.Add("Source has the same version $($source.ProductVersion) but a different SHA-256; keeping the cached MSI.")
            }
        }
        else {
            $warnings.Add("Source version $($source.ProductVersion) is older than cached $($cached.productVersion); not downgrading.")
        }
    }

    Write-CtsLogEntry -LogPath $LogPath -Entry ([ordered]@{
            startUtc = [DateTime]::UtcNow.ToString('o'); operation = 'DebuggerCacheCheck'; architecture = $Architecture
            shareReachable = $result.shareReachable; sourceVersion = $result.sourceVersion
            cachedVersionBefore = $result.cachedVersionBefore; cachedVersion = $result.cachedVersion
            refreshed = $result.refreshed; warnings = @($warnings)
        })
    $result.warnings = @($warnings)
    return [pscustomobject]$result
}

$script:CtsDebuggerPackageScript = {
    # Self-contained: runs locally or inside a guest. Mode 'Query' reports the installed package and tools;
    # mode 'Install' runs msiexec and then reports.
    param(
        [ValidateSet('Query', 'Install')][string]$Mode = 'Query',
        [string]$ProductName,
        [string]$MsiPath,
        [string]$InstallLogPath
    )
    $ErrorActionPreference = 'Stop'
    $exitCode = $null
    if ($Mode -eq 'Install') {
        $process = Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList @('/i', "`"$MsiPath`"", '/qn', '/norestart', '/l*v', "`"$InstallLogPath`"") -Wait -PassThru
        $exitCode = $process.ExitCode
    }
    $installed = @(Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
        Where-Object { $_.PSObject.Properties['DisplayName'] -and $_.DisplayName -eq $ProductName } |
        Sort-Object { [version]($_.DisplayVersion -replace '[^\d\.]', '') } -Descending)
    $entry = if ($installed.Count -gt 0) { $installed[0] } else { $null }
    $installLocation = if ($entry -and $entry.PSObject.Properties['InstallLocation']) { [string]$entry.InstallLocation } else { $null }

    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
    $roots = @($installLocation, 'C:\Debuggers', "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\$arch", "$env:ProgramFiles\Debuggers", "$env:SystemRoot\System32") |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -Unique
    $tools = [ordered]@{}
    foreach ($tool in 'cdb.exe', 'ntsd.exe', 'appverif.exe') {
        $found = $null
        foreach ($root in $roots) {
            $candidate = Join-Path $root $tool
            if (Test-Path -LiteralPath $candidate) { $found = Get-Item -LiteralPath $candidate; break }
        }
        $tools[$tool] = if ($found) { [ordered]@{ path = $found.FullName; fileVersion = [string]$found.VersionInfo.FileVersion } } else { $null }
    }
    [ordered]@{
        computerName     = $env:COMPUTERNAME
        architecture     = $env:PROCESSOR_ARCHITECTURE
        installedVersion = if ($entry) { [string]$entry.DisplayVersion } else { $null }
        installLocation  = $installLocation
        exitCode         = $exitCode
        tools            = $tools
    }
}

function Get-CtsDebuggerPackageScript {
    [CmdletBinding()]
    param()
    return $script:CtsDebuggerPackageScript
}

function Get-CtsDebuggerInstallDecision {
    <#
    .SYNOPSIS
    Pure decision: Install (missing or older), Skip (same), or Drift (installed newer than cache; never downgrade).
    #>
    param([string]$InstalledVersion, [Parameter(Mandatory)][string]$CachedVersion)
    if (-not $InstalledVersion) { return 'Install' }
    switch (Compare-CtsVersion $InstalledVersion $CachedVersion) {
        -1 { return 'Install' }
        0 { return 'Skip' }
        1 { return 'Drift' }
    }
}

function Install-CtsDebuggerPackage {
    <#
    .SYNOPSIS
    Installs the cached debugger package in a guest (GuestSession) or on the local host, following the
    guest install steps in debugging-tools.md. Returns the outcome; reboot handling is the caller's job.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)][ValidateSet('amd64', 'arm64')][string]$Architecture,
        [object]$GuestSession,
        [string]$CacheRoot,
        [string]$EvidenceDirectory
    )
    $cached = Get-CtsDebuggerCacheEntry -Definition $Definition -Architecture $Architecture -CacheRoot $CacheRoot
    if (-not $cached) { throw "No cached debugger MSI for $Architecture. Run Update-CtsDebuggerCache first." }
    $script = $script:CtsDebuggerPackageScript
    $run = {
        param([object[]]$Arguments, [string]$Operation)
        if ($GuestSession) { Invoke-CtsGuestCommand -GuestSession $GuestSession -ScriptBlock $script -ArgumentList $Arguments -Operation $Operation }
        else { & $script @Arguments }
    }

    $before = & $run @('Query', $cached.productName) 'DebuggerQuery'
    $decision = Get-CtsDebuggerInstallDecision -InstalledVersion $before.installedVersion -CachedVersion $cached.productVersion
    $outcome = [ordered]@{
        target         = if ($GuestSession) { $GuestSession.VMName } else { $env:COMPUTERNAME }
        cachedVersion  = $cached.productVersion
        versionBefore  = $before.installedVersion
        decision       = $decision
        exitCode       = $null
        rebootRequired = $false
        versionAfter   = $before.installedVersion
        tools          = $before.tools
        installLog     = $null
        missingTools   = @()
    }

    if ($decision -eq 'Install') {
        $remoteRoot = if ($GuestSession) { Join-Path $Definition.guest.rootPath 'tools\dbg' } else { Split-Path -Parent $cached.msiPath }
        $msiOnTarget = Join-Path $remoteRoot $cached.fileName
        $logOnTarget = Join-Path $(if ($GuestSession) { Join-Path $Definition.guest.rootPath 'logs' } else { Join-Path $Definition.labRoot 'logs' }) ("dbg-install-{0:yyyyMMddTHHmmssZ}.log" -f [DateTime]::UtcNow)
        if ($GuestSession) {
            Copy-CtsToGuest -GuestSession $GuestSession -Path $cached.msiPath -Destination $remoteRoot
            $null = Invoke-CtsGuestCommand -GuestSession $GuestSession -ScriptBlock { param($d) $null = New-Item -ItemType Directory -Path $d -Force } -ArgumentList (Split-Path -Parent $logOnTarget) -Operation 'EnsureLogDir'
        }
        else {
            if (-not (Test-CtsElevated)) { throw 'Installing the debugger package on the host requires an elevated session.' }
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $logOnTarget) -Force
        }
        $after = & $run @('Install', $cached.productName, $msiOnTarget, $logOnTarget) 'DebuggerInstall'
        $outcome.exitCode = $after.exitCode
        $outcome.rebootRequired = ($after.exitCode -eq 3010)
        $outcome.versionAfter = $after.installedVersion
        $outcome.tools = $after.tools
        $outcome.installLog = $logOnTarget
        if ($EvidenceDirectory -and $GuestSession) {
            Copy-CtsFromGuest -GuestSession $GuestSession -Path $logOnTarget -Destination $EvidenceDirectory
        }
        if ($after.exitCode -notin 0, 3010) {
            throw "Debugger package install on '$($outcome.target)' failed with exit code $($after.exitCode). Log: $logOnTarget"
        }
    }

    $toolNames = if ($outcome.tools -is [System.Collections.IDictionary]) { @($outcome.tools.Keys) } else { @($outcome.tools.PSObject.Properties.Name) }
    $outcome.missingTools = @($toolNames | Where-Object { -not (Get-CtsProp $outcome.tools $_) })
    return [pscustomobject]$outcome
}
