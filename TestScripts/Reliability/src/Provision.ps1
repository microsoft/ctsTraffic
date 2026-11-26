# Lab provisioning: read Hyper-V state, plan against the lab definition, and (only with -Apply) create the
# owned resources, configure guests, install the debugger package, and take the clean checkpoint.
# Ownership is recorded in <labRoot>\lab\ownership.json and in each VM's and switch's Notes. Existing
# objects that are not recorded as owned are collisions: provisioning stops and never adopts or deletes them.

$script:CtsCleanCheckpointName = 'ctsReliability-clean'

function Get-CtsLabPaths {
    param([Parameter(Mandatory)]$Definition)
    $root = $Definition.labRoot
    return [pscustomobject]@{
        Journal  = Join-Path $root 'lab\ownership.json'
        LabDir   = Join-Path $root 'lab'
        VmConfig = Join-Path $root 'vms'
        VmDisks  = Join-Path $root 'vhdx\vms'
    }
}

function Get-CtsOwnershipJournal {
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Definition)
    $path = (Get-CtsLabPaths $Definition).Journal
    if (-not (Test-Path -LiteralPath $path)) {
        return [pscustomobject]@{ schemaVersion = 1; labId = $Definition.labId; resources = @() }
    }
    $journal = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($journal.labId -ne $Definition.labId) { throw "Ownership journal '$path' belongs to lab '$($journal.labId)', not '$($Definition.labId)'." }
    $journal.resources = @($journal.resources)
    return $journal
}

function Add-CtsOwnershipRecord {
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)][ValidateSet('Switch', 'HostAddress', 'Disk', 'VM', 'Checkpoint')][string]$Kind,
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Id,
        [string]$Detail
    )
    $paths = Get-CtsLabPaths $Definition
    $journal = Get-CtsOwnershipJournal -Definition $Definition
    $kept = @($journal.resources | Where-Object { -not ($_.kind -eq $Kind -and $_.name -eq $Name) })
    $journal.resources = $kept + @([pscustomobject]@{ kind = $Kind; name = $Name; id = $Id; createdUtc = [DateTime]::UtcNow.ToString('o'); detail = $Detail })
    if (-not (Test-Path -LiteralPath $paths.LabDir)) { $null = New-Item -ItemType Directory -Path $paths.LabDir -Force }
    $temp = "$($paths.Journal).tmp"
    [IO.File]::WriteAllText($temp, ($journal | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temp -Destination $paths.Journal -Force
}

function Get-CtsHyperVState {
    <#
    .SYNOPSIS
    Reads the Hyper-V and host state that provisioning plans against. Read-only. Requires Hyper-V rights.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)]$Topology
    )
    $prefix = "$($Definition.namePrefix)-*"
    try {
        $switches = @(Get-VMSwitch -ErrorAction Stop | Where-Object { $_.Name -like $prefix } |
            ForEach-Object { [pscustomobject]@{ Name = $_.Name; Id = $_.Id.ToString(); SwitchType = [string]$_.SwitchType; Notes = $_.Notes } })
        $vms = @(Get-VM -ErrorAction Stop | Where-Object { $_.Name -like $prefix } | ForEach-Object {
                $vm = $_
                $disks = @(Get-VMHardDiskDrive -VM $vm | ForEach-Object Path)
                $diskAncestors = @(foreach ($path in $disks) {
                    while ([IO.Path]::GetExtension($path) -eq '.avhdx') {
                        $path = (Get-VHD -Path $path -ErrorAction Stop).ParentPath
                        if (-not $path) { throw "Checkpoint disk for '$($vm.Name)' has no parent." }
                        $path
                    }
                })
                [pscustomobject]@{
                    Name                        = $vm.Name
                    Id                          = $vm.Id.ToString()
                    State                       = [string]$vm.State
                    Notes                       = $vm.Notes
                    Generation                  = [int]$vm.Generation
                    ProcessorCount              = [int]$vm.ProcessorCount
                    MemoryStartupBytes          = [long]$vm.MemoryStartup
                    DynamicMemoryEnabled        = [bool]$vm.DynamicMemoryEnabled
                    AutomaticCheckpointsEnabled = [bool]$vm.AutomaticCheckpointsEnabled
                    Disks                       = $disks
                    DiskAncestors               = $diskAncestors
                    Checkpoints                 = @(Get-VMSnapshot -VM $vm -ErrorAction SilentlyContinue | ForEach-Object Name)
                    Adapters                    = @(Get-VMNetworkAdapter -VM $vm | ForEach-Object {
                            [pscustomobject]@{ Name = $_.Name; SwitchName = $_.SwitchName; MacAddress = $_.MacAddress; DynamicMacAddressEnabled = [bool]$_.DynamicMacAddressEnabled; DeviceNaming = [string]$_.DeviceNaming }
                        })
                }
            })
    }
    catch {
        throw "Cannot read Hyper-V state ($($_.Exception.Message)). Run elevated or as a member of Hyper-V Administrators."
    }
    $paths = Get-CtsLabPaths $Definition
    $golden = Get-CtsGoldenImagePath -Definition $Definition
    return [pscustomobject]@{
        CapturedUtc   = [DateTime]::UtcNow.ToString('o')
        Switches      = $switches
        Vms           = $vms
        HostAddresses = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | ForEach-Object { [pscustomobject]@{ InterfaceAlias = $_.InterfaceAlias; IPAddress = $_.IPAddress; PrefixLength = [int]$_.PrefixLength } })
        ExistingFiles = @($Topology.Vms | ForEach-Object { Join-Path $paths.VmDisks "$($_.Name).vhdx" } | Where-Object { Test-Path -LiteralPath $_ })
        Journal       = Get-CtsOwnershipJournal -Definition $Definition
        GoldenImage   = [pscustomobject]@{ Path = $golden.Vhdx; Exists = (Test-Path -LiteralPath $golden.Vhdx); MetadataExists = (Test-Path -LiteralPath $golden.Metadata) }
    }
}

function Get-CtsProvisionPlan {
    <#
    .SYNOPSIS
    Compares the desired lab with the current state and returns ordered plan items. Pure: no side effects.
    .DESCRIPTION
    Actions: Create (absent; also when an owned resource has disappeared), Keep (owned and matching),
    Drift (owned but different; blocks; no automatic repair), Collision (exists but not owned; blocks).
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)]$Topology,
        [Parameter(Mandatory)]$State
    )
    $paths = Get-CtsLabPaths $Definition
    $items = [System.Collections.Generic.List[object]]::new()
    $reasons = [System.Collections.Generic.List[string]]::new()
    $journal = @($State.Journal.resources)
    $record = { param($kind, $name) @($journal | Where-Object { $_.kind -eq $kind -and $_.name -eq $name }) | Select-Object -First 1 }
    $add = { param($kind, $name, $action, $detail, $desired)
        $items.Add([pscustomobject]@{ Kind = $kind; Name = $name; Action = $action; Detail = $detail; Desired = $desired })
        if ($action -in 'Drift', 'Collision') { $reasons.Add("${kind} '$name': $action - $detail") }
    }
    $classify = { param($kind, $name, $live, $differences)
        $rec = & $record $kind $name
        $live = @($live)
        if ($live.Count -gt 1) { return @('Collision', "$($live.Count) objects share this name") }
        if ($live.Count -eq 0) { return @('Create', $(if ($rec) { 'owned resource is missing; it will be recreated' } else { $null })) }
        if (-not $rec -or $rec.id -ne $live[0].Id) { return @('Collision', 'exists but is not recorded as owned by this lab') }
        if (@($differences).Count -gt 0) { return @('Drift', (@($differences) -join '; ')) }
        return @('Keep', $null)
    }

    $desiredSwitches = @([pscustomobject]@{ Name = $Topology.Management.SwitchName; SwitchType = 'Internal' }) +
    @($Topology.Links | ForEach-Object { [pscustomobject]@{ Name = $_.SwitchName; SwitchType = 'Private' } })
    $switchActions = @{}
    foreach ($s in $desiredSwitches) {
        $live = @($State.Switches | Where-Object { $_.Name -eq $s.Name })
        $diff = @($live | Where-Object { $_.SwitchType -ne $s.SwitchType } | ForEach-Object { "type $($_.SwitchType), expected $($s.SwitchType)" })
        $r = & $classify 'Switch' $s.Name $live $diff
        $switchActions[$s.Name] = $r[0]
        & $add 'Switch' $s.Name $r[0] $r[1] $s
    }

    $mgmtAlias = "vEthernet ($($Topology.Management.SwitchName))"
    $hostAddr = [pscustomobject]@{ InterfaceAlias = $mgmtAlias; IPAddress = $Topology.Management.HostAddress; PrefixLength = $Topology.Management.HostPrefix }
    $present = @($State.HostAddresses | Where-Object { $_.InterfaceAlias -eq $mgmtAlias -and $_.IPAddress -eq $hostAddr.IPAddress -and $_.PrefixLength -eq $hostAddr.PrefixLength })
    $hostAction = if ($switchActions[$Topology.Management.SwitchName] -eq 'Create' -or $present.Count -eq 0) { 'Create' } else { 'Keep' }
    & $add 'HostAddress' "$mgmtAlias $($hostAddr.IPAddress)/$($hostAddr.PrefixLength)" $hostAction $null $hostAddr
    $overlap = @(Test-CtsSubnetOverlap -Cidr $Topology.Management.Subnet -Existing @($State.HostAddresses | Where-Object { $_.InterfaceAlias -ne $mgmtAlias }))
    foreach ($o in $overlap) { $reasons.Add("Host address $($o.IPAddress)/$($o.PrefixLength) on '$($o.InterfaceAlias)' overlaps management subnet $($Topology.Management.Subnet)") }

    $needGolden = $false
    foreach ($vm in $Topology.Vms) {
        $diskPath = Join-Path $paths.VmDisks "$($vm.Name).vhdx"
        $liveVm = @($State.Vms | Where-Object { $_.Name -eq $vm.Name })
        $diskRec = & $record 'Disk' $diskPath
        $diskAction = if (@($State.ExistingFiles) -contains $diskPath) {
            if ($diskRec) { 'Keep' } else { 'Collision' }
        }
        else { 'Create' }
        if ($diskAction -eq 'Create') { $needGolden = $true }
        & $add 'Disk' $diskPath $diskAction $(if ($diskAction -eq 'Collision') { 'file exists but is not recorded as owned by this lab' }) ([pscustomobject]@{ Path = $diskPath; VmName = $vm.Name; ComputerName = $vm.ComputerName })

        $nics = @($vm.MgmtNic) + @($vm.TestNics)
        $diff = [System.Collections.Generic.List[string]]::new()
        if ($liveVm.Count -eq 1) {
            $l = $liveVm[0]
            if ($l.Generation -ne [int]$Definition.vm.generation) { $diff.Add("generation $($l.Generation)") }
            if ($l.ProcessorCount -ne [int]$Definition.vm.processorCount) { $diff.Add("processorCount $($l.ProcessorCount)") }
            if ($l.MemoryStartupBytes -ne [long]$Definition.vm.memoryGB * 1GB) { $diff.Add("memoryStartupBytes $($l.MemoryStartupBytes)") }
            if ($l.DynamicMemoryEnabled) { $diff.Add('dynamic memory enabled') }
            if ($l.AutomaticCheckpointsEnabled) { $diff.Add('automatic checkpoints enabled') }
            $diskChain = @($l.Disks) + @(Get-CtsProp $l 'DiskAncestors')
            if ($diskChain -notcontains $diskPath) { $diff.Add("disk $diskPath not attached") }
            $want = @($nics | ForEach-Object { "$($_.Name)|$($_.SwitchName)|$($_.MacAddress)|static" } | Sort-Object)
            $have = @($l.Adapters | ForEach-Object { "$($_.Name)|$($_.SwitchName)|$($_.MacAddress)|$(if ($_.DynamicMacAddressEnabled) { 'dynamic' } else { 'static' })" } | Sort-Object)
            if (($want -join ',') -ne ($have -join ',')) { $diff.Add("adapters [$($have -join ', ')] expected [$($want -join ', ')]") }
        }
        $r = & $classify 'VM' $vm.Name $liveVm $diff
        & $add 'VM' $vm.Name $r[0] $r[1] ([pscustomobject]@{ Name = $vm.Name; DiskPath = $diskPath; Nics = $nics })
    }
    if ($needGolden -and -not $State.GoldenImage.Exists) { $reasons.Add("Golden image '$($State.GoldenImage.Path)' does not exist. Run New-CtsGoldenImage.ps1 -Apply first.") }
    elseif ($needGolden -and -not $State.GoldenImage.MetadataExists) { $reasons.Add("Golden image metadata is missing next to '$($State.GoldenImage.Path)'; the image is not a recorded build.") }

    return [pscustomobject]@{
        LabId   = $Definition.labId
        VmCount = $Topology.VmCount
        Items   = @($items)
        Blocked = ($reasons.Count -gt 0)
        Reasons = @($reasons)
        Counts  = [pscustomobject]@{
            Create    = @($items | Where-Object Action -eq 'Create').Count
            Keep      = @($items | Where-Object Action -eq 'Keep').Count
            Drift     = @($items | Where-Object Action -eq 'Drift').Count
            Collision = @($items | Where-Object Action -eq 'Collision').Count
        }
    }
}

function Write-CtsJsonFile {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$InputObject)
    [IO.File]::WriteAllText($Path, ($InputObject | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}

function Invoke-CtsLabProvision {
    <#
    .SYNOPSIS
    Plans (default) or applies (-Apply) lab provisioning. Apply requires elevation and the guest credential.
    .DESCRIPTION
    Apply creates only planned Create items, then for every lab VM: starts it, waits for PowerShell Direct,
    applies and verifies the guest configuration (rebooting when needed), installs the debugger package,
    and takes the clean checkpoint if it does not exist. Evidence goes to <labRoot>\lab\provision-<utc>.
    Stops at the first failure; created resources stay recorded in the ownership journal.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [ValidateRange(2, 5)][int]$VmCount = $Definition.vm.count,
        [switch]$Apply,
        [switch]$SkipGuestConfiguration,
        [switch]$SkipSnapshot,
        [int]$GuestReadyTimeoutSeconds = 1800
    )
    $topology = Get-CtsLabTopology -Definition $Definition -VmCount $VmCount
    $state = Get-CtsHyperVState -Definition $Definition -Topology $topology
    $plan = Get-CtsProvisionPlan -Definition $Definition -Topology $topology -State $state
    if (-not $Apply) { return $plan }
    if ($plan.Blocked) { throw "Provisioning is blocked: $($plan.Reasons -join ' | ')" }
    if (-not (Test-CtsElevated)) { throw 'Applying provisioning requires an elevated PowerShell session.' }
    $credential = Get-CtsStoredCredential -Target $Definition.guest.credentialTarget

    $paths = Get-CtsLabPaths $Definition
    $evidence = Join-Path $paths.LabDir ('provision-{0}' -f [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
    $null = New-Item -ItemType Directory -Path $evidence -Force
    $logPath = Join-Path $evidence 'provision.jsonl'
    Write-CtsJsonFile -Path (Join-Path $evidence 'state-before.json') -InputObject $state
    Write-CtsJsonFile -Path (Join-Path $evidence 'plan.json') -InputObject $plan
    $results = [System.Collections.Generic.List[object]]::new()
    $step = { param($name, [scriptblock]$action)
        $start = [DateTime]::UtcNow
        try {
            $out = & $action
            $results.Add([pscustomobject]@{ step = $name; success = $true; startUtc = $start.ToString('o'); detail = $out })
            Write-CtsLogEntry -LogPath $logPath -Entry ([ordered]@{ startUtc = $start.ToString('o'); endUtc = [DateTime]::UtcNow.ToString('o'); channel = 'Host'; operation = $name; success = $true })
            return $out
        }
        catch {
            $results.Add([pscustomobject]@{ step = $name; success = $false; startUtc = $start.ToString('o'); detail = $_.Exception.Message })
            Write-CtsLogEntry -LogPath $logPath -Entry ([ordered]@{ startUtc = $start.ToString('o'); endUtc = [DateTime]::UtcNow.ToString('o'); channel = 'Host'; operation = $name; success = $false; error = $_.Exception.Message })
            Write-CtsJsonFile -Path (Join-Path $evidence 'results.json') -InputObject @($results)
            throw "Provisioning step '$name' failed: $($_.Exception.Message). Evidence: $evidence. Created resources are recorded in $($paths.Journal); rerun to continue."
        }
    }
    $notes = "ctsReliability labId=$($Definition.labId). Owned by TestScripts\Reliability; do not edit."
    $golden = Get-CtsGoldenImagePath -Definition $Definition
    $arch = Get-CtsHostArchitecture

    foreach ($item in @($plan.Items | Where-Object Action -eq 'Create')) {
        switch ($item.Kind) {
            'Switch' {
                & $step "CreateSwitch:$($item.Name)" {
                    $sw = New-VMSwitch -Name $item.Name -SwitchType $item.Desired.SwitchType -Notes $notes -ErrorAction Stop
                    Add-CtsOwnershipRecord -Definition $Definition -Kind Switch -Name $item.Name -Id $sw.Id.ToString() -Detail $item.Desired.SwitchType
                    $sw.Id.ToString()
                }
            }
            'HostAddress' {
                & $step "SetHostAddress:$($item.Name)" {
                    $d = $item.Desired
                    $null = New-NetIPAddress -InterfaceAlias $d.InterfaceAlias -IPAddress $d.IPAddress -PrefixLength $d.PrefixLength -ErrorAction Stop
                    Add-CtsOwnershipRecord -Definition $Definition -Kind HostAddress -Name $item.Name -Id "$($d.IPAddress)/$($d.PrefixLength)"
                }
            }
            'Disk' {
                & $step "CreateDisk:$($item.Desired.VmName)" {
                    $d = $item.Desired
                    if (-not (Test-Path -LiteralPath $paths.VmDisks)) { $null = New-Item -ItemType Directory -Path $paths.VmDisks -Force }
                    $null = New-VHD -Path $d.Path -ParentPath $golden.Vhdx -Differencing -ErrorAction Stop
                    Add-CtsOwnershipRecord -Definition $Definition -Kind Disk -Name $d.Path -Id $d.Path -Detail "parent $($golden.Vhdx)"
                    $unattend = New-CtsUnattendXml -ComputerName $d.ComputerName -Credential $credential -Architecture $arch
                    Set-CtsVhdUnattend -VhdPath $d.Path -UnattendXml $unattend -SetupCompleteCmd (New-CtsSetupCompleteCmd -GuestRootPath $Definition.guest.rootPath)
                }
            }
            'VM' {
                & $step "CreateVM:$($item.Name)" {
                    $d = $item.Desired
                    $vm = New-VM -Name $d.Name -Generation ([int]$Definition.vm.generation) -MemoryStartupBytes ([long]$Definition.vm.memoryGB * 1GB) -VHDPath $d.DiskPath -SwitchName $topology.Management.SwitchName -Path $paths.VmConfig -ErrorAction Stop
                    Add-CtsOwnershipRecord -Definition $Definition -Kind VM -Name $d.Name -Id $vm.Id.ToString()
                    Set-VM -VM $vm -ProcessorCount ([int]$Definition.vm.processorCount) -StaticMemory -AutomaticCheckpointsEnabled $false -CheckpointType Production -AutomaticStartAction Nothing -AutomaticStopAction ShutDown -Notes $notes -ErrorAction Stop
                    Set-VMFirmware -VM $vm -EnableSecureBoot On -SecureBootTemplate $Definition.vm.secureBootTemplate -ErrorAction Stop
                    $mgmt = @(Get-VMNetworkAdapter -VM $vm)[0]
                    Rename-VMNetworkAdapter -VMNetworkAdapter $mgmt -NewName $d.Nics[0].Name -ErrorAction Stop
                    Set-VMNetworkAdapter -VMNetworkAdapter $mgmt -StaticMacAddress $d.Nics[0].MacAddress -DeviceNaming On -ErrorAction Stop
                    foreach ($nic in @($d.Nics | Select-Object -Skip 1)) {
                        Add-VMNetworkAdapter -VM $vm -Name $nic.Name -SwitchName $nic.SwitchName -StaticMacAddress $nic.MacAddress -DeviceNaming On -ErrorAction Stop
                    }
                    $vm.Id.ToString()
                }
            }
        }
    }
    Write-CtsJsonFile -Path (Join-Path $evidence 'results.json') -InputObject @($results)
    if ($SkipGuestConfiguration) { return [pscustomobject]@{ Plan = $plan; Results = @($results); Evidence = $evidence } }

    & $step "UpdateDebuggerCache:$arch" { Update-CtsDebuggerCache -Definition $Definition -Architecture $arch -LogPath $logPath }
    $findings = [System.Collections.Generic.List[string]]::new()
    foreach ($vm in $topology.Vms) {
        $config = Get-CtsGuestConfiguration -Definition $Definition -Topology $topology -VmName $vm.Name
        Write-CtsJsonFile -Path (Join-Path $evidence "guestconfig-desired-$($vm.Name).json") -InputObject $config
        & $step "StartVM:$($vm.Name)" { if ((Get-VM -Name $vm.Name).State -ne 'Running') { Start-VM -Name $vm.Name -ErrorAction Stop } }
        & $step "WaitGuest:$($vm.Name)" { Wait-CtsGuestReady -VMName $vm.Name -Credential $credential -TimeoutSeconds $GuestReadyTimeoutSeconds -LogPath $logPath }
        $session = New-CtsGuestSession -VMName $vm.Name -Channel Direct -Credential $credential -LogPath $logPath
        try {
            $applied = & $step "GuestConfigApply:$($vm.Name)" { Invoke-CtsGuestConfiguration -Configuration $config -GuestSession $session -Apply }
            Write-CtsJsonFile -Path (Join-Path $evidence "guestconfig-apply-$($vm.Name).json") -InputObject $applied
            $debugger = & $step "InstallDebugger:$($vm.Name)" { Install-CtsDebuggerPackage -Definition $Definition -Architecture $arch -GuestSession $session -EvidenceDirectory $evidence }
            foreach ($t in @($debugger.missingTools)) { $findings.Add("$($vm.Name) debugger tool not found after install: $t") }
            if ($applied.rebootRequired -or ($debugger -and $debugger.PSObject.Properties['rebootRequired'] -and $debugger.rebootRequired)) {
                Remove-PSSession -Session $session.Session -ErrorAction SilentlyContinue
                & $step "Reboot:$($vm.Name)" {
                    Restart-VM -Name $vm.Name -Force -Wait -For Heartbeat -Timeout $GuestReadyTimeoutSeconds -ErrorAction Stop
                    Wait-CtsGuestReady -VMName $vm.Name -Credential $credential -TimeoutSeconds $GuestReadyTimeoutSeconds -LogPath $logPath
                }
                $session = New-CtsGuestSession -VMName $vm.Name -Channel Direct -Credential $credential -LogPath $logPath
            }
            $verify = & $step "GuestConfigVerify:$($vm.Name)" { Invoke-CtsGuestConfiguration -Configuration $config -GuestSession $session }
            Write-CtsJsonFile -Path (Join-Path $evidence "guestconfig-verify-$($vm.Name).json") -InputObject $verify
            $bad = @($verify.items | Where-Object { $_.status -notin 'Compliant' })
            foreach ($b in $bad) { $findings.Add("$($vm.Name) $($b.item): $($b.status) $($b.error)") }
            if (@($bad | Where-Object { $_.status -ne 'Blocked' -and $_.item -ne 'DefenderRealTimeProtection' }).Count -gt 0) {
                & $step "GuestConfigVerified:$($vm.Name)" { throw "Guest configuration is not compliant: $(@($bad | ForEach-Object item) -join ', ')" }
            }
        }
        finally { Remove-PSSession -Session $session.Session -ErrorAction SilentlyContinue }
        & $step "CleanCheckpoint:$($vm.Name)" {
            $existing = @(Get-VMSnapshot -VMName $vm.Name -ErrorAction SilentlyContinue | Where-Object Name -eq $script:CtsCleanCheckpointName)
            if ($existing.Count -eq 0) {
                Checkpoint-VM -Name $vm.Name -SnapshotName $script:CtsCleanCheckpointName -ErrorAction Stop
                $cp = @(Get-VMSnapshot -VMName $vm.Name | Where-Object Name -eq $script:CtsCleanCheckpointName)[0]
                Add-CtsOwnershipRecord -Definition $Definition -Kind Checkpoint -Name "$($vm.Name)/$($script:CtsCleanCheckpointName)" -Id $cp.Id.ToString()
                'created'
            }
            else { 'exists' }
        }
    }
    $snapshot = $null
    if (-not $SkipSnapshot) {
        $snapshot = & $step 'PostProvisionSnapshot' { New-CtsSnapshot -Definition $Definition -VmCount $VmCount -Credential $credential -LogPath $logPath }
    }
    Write-CtsJsonFile -Path (Join-Path $evidence 'results.json') -InputObject @($results)
    return [pscustomobject]@{ Plan = $plan; Results = @($results); Findings = @($findings); Evidence = $evidence; Snapshot = $snapshot }
}

function Set-CtsVhdUnattend {
    <#
    .SYNOPSIS
    Mounts a VHDX, writes Windows\Panther\unattend.xml and Windows\Setup\Scripts\SetupComplete.cmd, and dismounts.
    #>
    param(
        [Parameter(Mandatory)][string]$VhdPath,
        [Parameter(Mandatory)][string]$UnattendXml,
        [Parameter(Mandatory)][string]$SetupCompleteCmd
    )
    $mounted = $false
    try {
        $disk = Mount-VHD -Path $VhdPath -Passthru -ErrorAction Stop | Get-Disk
        $mounted = $true
        $data = @(Get-Partition -DiskNumber $disk.Number | Where-Object { $_.GptType -eq '{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}' })
        foreach ($p in $data) { if (-not $p.DriveLetter) { Add-PartitionAccessPath -DiskNumber $disk.Number -PartitionNumber $p.PartitionNumber -AssignDriveLetter -ErrorAction Stop } }
        $windows = @(Get-Partition -DiskNumber $disk.Number | Where-Object { $_.DriveLetter -and (Test-Path -LiteralPath "$($_.DriveLetter):\Windows\System32") })
        if ($windows.Count -ne 1) { throw "Expected one Windows partition in '$VhdPath'; found $($windows.Count)." }
        $letter = $windows[0].DriveLetter
        $panther = "${letter}:\Windows\Panther"
        $scripts = "${letter}:\Windows\Setup\Scripts"
        foreach ($d in $panther, $scripts) { if (-not (Test-Path -LiteralPath $d)) { $null = New-Item -ItemType Directory -Path $d -Force } }
        [IO.File]::WriteAllText((Join-Path $panther 'unattend.xml'), $UnattendXml, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $scripts 'SetupComplete.cmd'), $SetupCompleteCmd, [Text.Encoding]::ASCII)
    }
    finally {
        if ($mounted) { Dismount-VHD -Path $VhdPath -ErrorAction SilentlyContinue }
    }
}
