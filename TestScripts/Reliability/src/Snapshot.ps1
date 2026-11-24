# System snapshot: capture, validation, drift comparison, and readable report.

$script:CtsSnapshotSchemaVersion = 1
$script:CtsDeviceStatuses = 'complete', 'partial', 'failed', 'notCaptured', 'notProvisioned'
$script:CtsDefaultDriftIgnore = @(
    '*.capturedUtc'
    '*.availableMemoryBytes'
    '*.freeBytes'
    '*.sections.neighbors.*'
    '*.lastBootUtc'
    '*.reason'
)

function Get-CtsProp {
    param($Object, [string]$Name)
    if ($null -eq $Object) { return $null }
    if ($Object -is [System.Collections.IDictionary]) { if ($Object.Contains($Name)) { return $Object[$Name] } return $null }
    $p = $Object.PSObject.Properties[$Name]
    if ($p) { return $p.Value }
    return $null
}

function Test-CtsHasProp {
    param($Object, [string]$Name)
    if ($null -eq $Object) { return $false }
    if ($Object -is [System.Collections.IDictionary]) { return $Object.Contains($Name) }
    return $null -ne $Object.PSObject.Properties[$Name]
}

function New-CtsSnapshot {
    <#
    .SYNOPSIS
    Captures a read-only system snapshot of the Hyper-V host and every lab guest.
    .DESCRIPTION
    Writes snapshot.json (authoritative), report.md, and manifest.json (SHA-256 of each file) into
    <OutputRoot>\<snapshotId>. Returns an object with the path, snapshot, and validation errors.
    .PARAMETER HostOnly
    Capture the host and Hyper-V configuration only; guests are recorded as notCaptured.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [int]$VmCount = $Definition.vm.count,
        [ValidateSet('Direct', 'WinRM')][string]$Channel = 'Direct',
        [pscredential]$Credential,
        [string]$OutputRoot = (Join-Path $Definition.labRoot 'snapshots'),
        [switch]$HostOnly,
        [string]$LogPath
    )
    $start = [DateTime]::UtcNow
    $topology = Get-CtsLabTopology -Definition $Definition -VmCount $VmCount
    $snapshotId = 'snap-{0}-{1}' -f $start.ToString('yyyyMMddTHHmmssZ'), ($env:COMPUTERNAME -replace '[^\w-]', '')
    $errors = [System.Collections.Generic.List[string]]::new()

    $hostInventory = Get-CtsDeviceInventory -RootPath $Definition.labRoot -RouteDetailInterface @("vEthernet ($($Definition.namePrefix)*")
    $hyperV = Get-CtsHyperVInventory -VMName @($topology.Vms.Name)
    $hostInventory.sections['hyperV'] = $hyperV
    $hostStatus = if (@($hostInventory.sections.Values | Where-Object { $_.status -ne 'ok' }).Count -eq 0) { 'complete' } else { 'partial' }
    $devices = [System.Collections.Generic.List[object]]::new()
    $devices.Add([ordered]@{ deviceId = $env:COMPUTERNAME; role = 'host'; vmName = $null; channel = 'local'; status = $hostStatus; reason = $null; inventory = $hostInventory })

    $missing = if ($hyperV.status -eq 'ok') { @($hyperV.data.missingVms) } else { @() }
    foreach ($vm in $topology.Vms) {
        $device = [ordered]@{ deviceId = $vm.ComputerName; role = 'guest'; vmName = $vm.Name; channel = $Channel; status = $null; reason = $null; inventory = $null }
        if ($HostOnly) {
            $device.status = 'notCaptured'; $device.reason = 'host-only snapshot'
        }
        elseif ($missing -contains $vm.Name) {
            $device.status = 'notProvisioned'; $device.reason = 'VM does not exist'
        }
        elseif ($null -eq $Credential) {
            $device.status = 'failed'; $device.reason = 'no guest credential supplied'
        }
        else {
            $guestSession = $null
            try {
                $address = if ($Channel -eq 'WinRM') { $vm.MgmtNic.IPv4Address } else { $null }
                $guestSession = New-CtsGuestSession -VMName $vm.Name -Channel $Channel -Credential $Credential -ComputerName $address -LogPath $LogPath
                $inventory = Invoke-CtsGuestCommand -GuestSession $guestSession -ScriptBlock (Get-CtsDeviceInventoryScript) -ArgumentList $Definition.guest.rootPath -Operation 'SnapshotInventory'
                $device.inventory = $inventory
                $unavailable = @($inventory.sections.Values | Where-Object { $_.status -ne 'ok' })
                $device.status = if ($unavailable.Count -eq 0) { 'complete' } else { 'partial' }
            }
            catch {
                $device.status = 'failed'; $device.reason = $_.Exception.Message
            }
            finally {
                if ($guestSession) { Remove-PSSession -Session $guestSession.Session -ErrorAction SilentlyContinue }
            }
        }
        if ($device.status -ne 'complete') { $errors.Add("$($vm.Name): $($device.status) ($($device.reason))") }
        $devices.Add($device)
    }
    if ($hostStatus -ne 'complete') {
        $bad = @($hostInventory.sections.GetEnumerator() | Where-Object { $_.Value.status -ne 'ok' } | ForEach-Object { "$($_.Key): $($_.Value.reason)" })
        $errors.Add("host: partial ($($bad -join '; '))")
    }

    $snapshot = [ordered]@{
        schemaVersion   = $script:CtsSnapshotSchemaVersion
        snapshotId      = $snapshotId
        labId           = $Definition.labId
        captureStartUtc = $start.ToString('o')
        captureEndUtc   = [DateTime]::UtcNow.ToString('o')
        hostOnly        = [bool]$HostOnly
        complete        = (@($devices | Where-Object { $_.status -ne 'complete' }).Count -eq 0)
        topology        = $topology
        devices         = $devices
        errors          = $errors
    }
    # Round-trip so the returned object has the same shape as one read back from disk.
    $json = $snapshot | ConvertTo-Json -Depth 30
    $snapshotObject = $json | ConvertFrom-Json

    $directory = Join-Path $OutputRoot $snapshotId
    $null = New-Item -ItemType Directory -Path $directory -Force
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText((Join-Path $directory 'snapshot.json'), $json, $utf8)
    [System.IO.File]::WriteAllText((Join-Path $directory 'report.md'), (ConvertTo-CtsSnapshotReport -Snapshot $snapshotObject), $utf8)
    Write-CtsManifest -Directory $directory

    return [pscustomobject]@{
        Path             = $directory
        Snapshot         = $snapshotObject
        ValidationErrors = @(Test-CtsSnapshot -Snapshot $snapshotObject)
    }
}

function Write-CtsManifest {
    param([Parameter(Mandatory)][string]$Directory)
    $files = @(Get-ChildItem -LiteralPath $Directory -File | Where-Object Name -ne 'manifest.json' | Sort-Object Name | ForEach-Object {
            [ordered]@{ name = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
        })
    $manifest = [ordered]@{ createdUtc = [DateTime]::UtcNow.ToString('o'); files = $files }
    [System.IO.File]::WriteAllText((Join-Path $Directory 'manifest.json'), ($manifest | ConvertTo-Json -Depth 5), [System.Text.UTF8Encoding]::new($false))
}

function Test-CtsSnapshotManifest {
    <#
    .SYNOPSIS
    Returns errors for files whose hash or size no longer matches manifest.json.
    #>
    param([Parameter(Mandatory)][string]$Directory)
    $manifest = Get-Content -LiteralPath (Join-Path $Directory 'manifest.json') -Raw | ConvertFrom-Json
    foreach ($f in $manifest.files) {
        $path = Join-Path $Directory $f.name
        if (-not (Test-Path -LiteralPath $path)) { "missing file $($f.name)"; continue }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $f.sha256) { "hash mismatch $($f.name)" }
    }
}

function Test-CtsSnapshot {
    <#
    .SYNOPSIS
    Validates a snapshot object (or snapshot directory) against schema version 1. Returns error strings; none means valid.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, ParameterSetName = 'Object')]$Snapshot,
        [Parameter(Mandatory, ParameterSetName = 'Path')][string]$Path
    )
    if ($PSCmdlet.ParameterSetName -eq 'Path') {
        foreach ($e in @(Test-CtsSnapshotManifest -Directory $Path)) { $e }
        $Snapshot = Get-Content -LiteralPath (Join-Path $Path 'snapshot.json') -Raw | ConvertFrom-Json
    }

    foreach ($name in 'schemaVersion', 'snapshotId', 'labId', 'captureStartUtc', 'captureEndUtc', 'complete', 'topology', 'devices') {
        if (-not (Test-CtsHasProp $Snapshot $name)) { "missing field '$name'" }
    }
    if (-not (Test-CtsHasProp $Snapshot 'devices')) { return }

    if ((Get-CtsProp $Snapshot 'schemaVersion') -ne $script:CtsSnapshotSchemaVersion) { "unsupported schemaVersion '$(Get-CtsProp $Snapshot 'schemaVersion')'" }
    if ([string](Get-CtsProp $Snapshot 'snapshotId') -notmatch '^snap-\d{8}T\d{6}Z-[\w-]+$') { "malformed snapshotId '$(Get-CtsProp $Snapshot 'snapshotId')'" }
    $times = @{}
    foreach ($name in 'captureStartUtc', 'captureEndUtc') {
        $parsed = [DateTime]::MinValue
        $raw = Get-CtsProp $Snapshot $name
        if ($raw -is [DateTime]) { $times[$name] = $raw }
        elseif ([DateTime]::TryParse([string]$raw, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed)) { $times[$name] = $parsed }
        else { "malformed $name '$raw'" }
    }
    if ($times.Count -eq 2 -and $times.captureEndUtc -lt $times.captureStartUtc) { 'captureEndUtc is before captureStartUtc' }

    $devices = @(Get-CtsProp $Snapshot 'devices')
    if ($devices.Count -eq 0) { 'no devices' }
    $ids = @($devices | ForEach-Object { Get-CtsProp $_ 'deviceId' })
    foreach ($dup in @($ids | Group-Object | Where-Object Count -gt 1)) { "duplicate deviceId '$($dup.Name)'" }
    if (@($devices | Where-Object { (Get-CtsProp $_ 'role') -eq 'host' }).Count -ne 1) { 'snapshot must contain exactly one host device' }

    $topologyVms = @((Get-CtsProp (Get-CtsProp $Snapshot 'topology') 'Vms') | ForEach-Object { Get-CtsProp $_ 'Name' })
    $guestVmNames = @($devices | Where-Object { (Get-CtsProp $_ 'role') -eq 'guest' } | ForEach-Object { Get-CtsProp $_ 'vmName' })
    foreach ($vm in $topologyVms) { if ($guestVmNames -notcontains $vm) { "topology VM '$vm' has no device record" } }
    foreach ($vm in $guestVmNames) { if ($topologyVms -notcontains $vm) { "device vmName '$vm' is not in the topology" } }
    foreach ($link in @(Get-CtsProp (Get-CtsProp $Snapshot 'topology') 'Links')) {
        foreach ($end in @(Get-CtsProp $link 'Vms')) {
            if ($topologyVms -notcontains $end) { "link '$(Get-CtsProp $link 'Pair')' references unknown VM '$end'" }
        }
    }

    foreach ($d in $devices) {
        $id = Get-CtsProp $d 'deviceId'
        $role = Get-CtsProp $d 'role'
        $status = Get-CtsProp $d 'status'
        if ([string]::IsNullOrWhiteSpace([string]$id)) { 'device with empty deviceId' }
        if ($role -notin 'host', 'guest') { "device '$id' has invalid role '$role'" }
        if ($status -notin $script:CtsDeviceStatuses) { "device '$id' has invalid status '$status'" }
        if ($status -in 'failed', 'notCaptured', 'notProvisioned' -and [string]::IsNullOrWhiteSpace([string](Get-CtsProp $d 'reason'))) {
            "device '$id' has status '$status' without a reason"
        }
        $inventory = Get-CtsProp $d 'inventory'
        if ($status -in 'complete', 'partial') {
            $sections = Get-CtsProp $inventory 'sections'
            if ($null -eq $sections) { "device '$id' has status '$status' but no inventory sections"; continue }
            $sectionPairs = if ($sections -is [System.Collections.IDictionary]) { $sections.GetEnumerator() } else { $sections.PSObject.Properties }
            $anyUnavailable = $false
            foreach ($s in $sectionPairs) {
                $sv = $s.Value
                $ss = Get-CtsProp $sv 'status'
                if ($ss -notin 'ok', 'unavailable') { "device '$id' section '$($s.Name)' has invalid status '$ss'" }
                if ([string]::IsNullOrWhiteSpace([string](Get-CtsProp $sv 'source'))) { "device '$id' section '$($s.Name)' has no source" }
                if ([string]::IsNullOrWhiteSpace([string](Get-CtsProp $sv 'capturedUtc'))) { "device '$id' section '$($s.Name)' has no capturedUtc" }
                if ($ss -eq 'unavailable') {
                    $anyUnavailable = $true
                    if ([string]::IsNullOrWhiteSpace([string](Get-CtsProp $sv 'reason'))) { "device '$id' section '$($s.Name)' is unavailable without a reason" }
                }
            }
            if ($status -eq 'complete' -and $anyUnavailable) { "device '$id' is marked complete but has unavailable sections" }
            if ($role -eq 'host' -and -not (Test-CtsHasProp $sections 'hyperV')) { "host device '$id' has no hyperV section" }
        }
    }
    $allComplete = @($devices | Where-Object { (Get-CtsProp $_ 'status') -ne 'complete' }).Count -eq 0
    if ([bool](Get-CtsProp $Snapshot 'complete') -ne $allComplete) { "snapshot 'complete' flag does not match device statuses" }
}

function ConvertTo-CtsFlatMap {
    param($Value, [string]$Prefix, [System.Collections.IDictionary]$Map)
    if ($null -eq $Value) { $Map[$Prefix] = $null; return }
    if ($Value -is [string] -or $Value -is [ValueType]) { $Map[$Prefix] = [string]$Value; return }
    if ($Value -is [System.Collections.IDictionary]) {
        foreach ($k in $Value.Keys) { ConvertTo-CtsFlatMap -Value $Value[$k] -Prefix "$Prefix.$k" -Map $Map }
        return
    }
    if ($Value -is [System.Collections.IEnumerable]) {
        $i = 0
        $used = @{}
        foreach ($item in $Value) {
            $key = $null
            $alias = Get-CtsProp $item 'interfaceAlias'
            if ($alias -and (Get-CtsProp $item 'destinationPrefix')) {
                $key = "[$alias|$(Get-CtsProp $item 'destinationPrefix')|$(Get-CtsProp $item 'nextHop')]"
            }
            elseif ($alias -and (Get-CtsProp $item 'ipAddress')) {
                $key = "[$alias|$(Get-CtsProp $item 'ipAddress')]"
            }
            elseif ($alias -and (Get-CtsProp $item 'addressFamily')) {
                $key = "[$alias|$(Get-CtsProp $item 'addressFamily')]"
            }
            if (-not $key) {
                foreach ($keyName in 'name', 'Name', 'interfaceAlias', 'image', 'drive', 'path') {
                    $candidate = Get-CtsProp $item $keyName
                    if ($candidate -and ($candidate -is [string])) { $key = "[$candidate]"; break }
                }
            }
            if (-not $key) { $key = "[$i]" }
            # Disambiguate items that share a key (for example, several addresses on one interface).
            $suffix = $key; $n = 1
            while ($used.ContainsKey($suffix)) { $n++; $suffix = "$key#$n" }
            $used[$suffix] = $true
            ConvertTo-CtsFlatMap -Value $item -Prefix "$Prefix$suffix" -Map $Map
            $i++
        }
        return
    }
    foreach ($p in $Value.PSObject.Properties) { ConvertTo-CtsFlatMap -Value $p.Value -Prefix "$Prefix.$($p.Name)" -Map $Map }
}

function Compare-CtsSnapshot {
    <#
    .SYNOPSIS
    Reports configuration drift between two snapshots, per device and per value path.
    .PARAMETER IgnorePath
    Wildcard patterns of flattened paths to ignore. Defaults ignore capture times and volatile values
    (available memory, free disk, neighbor cache, boot time).
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Reference,
        [Parameter(Mandatory)]$Difference,
        [string[]]$IgnorePath = $script:CtsDefaultDriftIgnore
    )
    $refMap = [ordered]@{}; $difMap = [ordered]@{}
    foreach ($pair in @(@{ s = $Reference; m = $refMap }, @{ s = $Difference; m = $difMap })) {
        foreach ($d in @(Get-CtsProp $pair.s 'devices')) {
            ConvertTo-CtsFlatMap -Value (Get-CtsProp $d 'inventory') -Prefix ("{0}" -f (Get-CtsProp $d 'deviceId')) -Map $pair.m
            $pair.m["$(Get-CtsProp $d 'deviceId').status"] = [string](Get-CtsProp $d 'status')
        }
    }
    $keys = @($refMap.Keys) + @($difMap.Keys) | Sort-Object -Unique
    foreach ($k in $keys) {
        $ignored = $false
        foreach ($pattern in $IgnorePath) { if ($k -like $pattern) { $ignored = $true; break } }
        if ($ignored) { continue }
        $inRef = $refMap.Contains($k); $inDif = $difMap.Contains($k)
        $change = if (-not $inRef) { 'added' } elseif (-not $inDif) { 'removed' } elseif ($refMap[$k] -ne $difMap[$k]) { 'changed' } else { $null }
        if ($change) {
            [pscustomobject]@{
                DeviceId   = $k.Split('.')[0]
                Path       = $k
                Change     = $change
                Reference  = if ($inRef) { $refMap[$k] } else { $null }
                Difference = if ($inDif) { $difMap[$k] } else { $null }
            }
        }
    }
}

function ConvertTo-CtsSnapshotReport {
    <#
    .SYNOPSIS
    Renders a human-readable Markdown report of a snapshot.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Snapshot)
    # The report renders whatever was captured; partial or unavailable sections leave fields missing.
    Set-StrictMode -Off
    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine("# System snapshot $($Snapshot.snapshotId)").AppendLine()
    $null = $sb.AppendLine("- Lab: ``$($Snapshot.labId)``")
    $null = $sb.AppendLine("- Captured (UTC): $($Snapshot.captureStartUtc) to $($Snapshot.captureEndUtc)")
    $null = $sb.AppendLine("- Complete: **$($Snapshot.complete)**$(if ($Snapshot.hostOnly) { ' (host-only)' })").AppendLine()

    $null = $sb.AppendLine('## Devices').AppendLine()
    $null = $sb.AppendLine('| Device | Role | VM | Channel | Status | OS | Build | Architecture | Note |')
    $null = $sb.AppendLine('| --- | --- | --- | --- | --- | --- | --- | --- | --- |')
    foreach ($d in $Snapshot.devices) {
        $identity = if ($d.inventory) { $d.inventory.sections.identity.data } else { $null }
        $os = if ($identity) { $identity.osCaption } else { '' }
        $build = if ($identity) { "$($identity.osBuild).$($identity.ubr)" } else { '' }
        $arch = if ($identity) { $identity.processorArchitecture } else { '' }
        $null = $sb.AppendLine("| $($d.deviceId) | $($d.role) | $($d.vmName) | $($d.channel) | $($d.status) | $os | $build | $arch | $($d.reason) |")
    }
    $null = $sb.AppendLine()

    $null = $sb.AppendLine('## Expected topology').AppendLine()
    $null = $sb.AppendLine('| VM | NIC | Switch | Peer | IPv4 | IPv6 | MAC |')
    $null = $sb.AppendLine('| --- | --- | --- | --- | --- | --- | --- |')
    foreach ($vm in $Snapshot.topology.Vms) {
        $m = $vm.MgmtNic
        $null = $sb.AppendLine("| $($vm.Name) | $($m.Name) | $($m.SwitchName) | (management) | $($m.IPv4Address)/$($m.IPv4Prefix) | | $($m.MacAddress) |")
        foreach ($n in $vm.TestNics) {
            $null = $sb.AppendLine("| $($vm.Name) | $($n.Name) | $($n.SwitchName) | $($n.PeerVm) | $($n.IPv4Address)/$($n.IPv4Prefix) | $($n.IPv6Address)/$($n.IPv6Prefix) | $($n.MacAddress) |")
        }
    }
    $null = $sb.AppendLine()

    $hostDevice = $Snapshot.devices | Where-Object role -eq 'host' | Select-Object -First 1
    $hv = if ($hostDevice -and $hostDevice.inventory) { $hostDevice.inventory.sections.hyperV } else { $null }
    $null = $sb.AppendLine('## Hyper-V').AppendLine()
    if ($hv -and $hv.status -eq 'ok') {
        $null = $sb.AppendLine('| Switch | Type | Physical adapter | Extensions |')
        $null = $sb.AppendLine('| --- | --- | --- | --- |')
        foreach ($s in $hv.data.switches) {
            $null = $sb.AppendLine("| $($s.name) | $($s.switchType) | $(if ($s.physicalAdapterBound) { $s.netAdapterInterfaceDescription } else { 'none' }) | $(@($s.enabledExtensions) -join ', ') |")
        }
        $null = $sb.AppendLine()
        $null = $sb.AppendLine('| VM | State | Gen | vCPU | Memory (GiB) | Dynamic memory | Auto checkpoints | Adapters | Checkpoints |')
        $null = $sb.AppendLine('| --- | --- | --- | --- | --- | --- | --- | --- | --- |')
        foreach ($v in $hv.data.vms) {
            $adapters = @($v.networkAdapters | ForEach-Object { "$($_.name)->$($_.switchName)" }) -join '; '
            $null = $sb.AppendLine("| $($v.name) | $($v.state) | $($v.generation) | $($v.processorCount) | $([math]::Round($v.memoryStartupBytes / 1GB, 1)) | $($v.dynamicMemoryEnabled) | $($v.automaticCheckpointsEnabled) | $adapters | $(@($v.checkpoints).Count) |")
        }
        if (@($hv.data.missingVms).Count -gt 0) { $null = $sb.AppendLine().AppendLine("Not provisioned: $(@($hv.data.missingVms) -join ', ')") }
    }
    else {
        $null = $sb.AppendLine("Unavailable: $(if ($hv) { $hv.reason } else { 'not captured' })")
    }
    $null = $sb.AppendLine()

    $null = $sb.AppendLine('## Unavailable sections and errors').AppendLine()
    $count = 0
    foreach ($d in $Snapshot.devices) {
        if (-not $d.inventory) { continue }
        foreach ($p in $d.inventory.sections.PSObject.Properties) {
            if ($p.Value.status -ne 'ok') { $null = $sb.AppendLine("- $($d.deviceId) / $($p.Name): $($p.Value.reason)"); $count++ }
        }
    }
    foreach ($e in @($Snapshot.errors)) { if ($e) { $null = $sb.AppendLine("- $e"); $count++ } }
    if ($count -eq 0) { $null = $sb.AppendLine('None.') }
    return $sb.ToString()
}
