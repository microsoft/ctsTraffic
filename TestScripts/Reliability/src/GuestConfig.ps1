# Guest OS configuration: desired state per VM (from the lab definition and topology) and an idempotent
# guest-side script that reports, and unless -DryRun applies, each setting. The script runs unchanged in a
# guest session or locally (tests use it locally with mocks).

function ConvertTo-CtsMacDisplay {
    param([Parameter(Mandatory)][string]$Mac)
    return (($Mac -replace '[^0-9A-Fa-f]', '').ToUpperInvariant() -split '(..)' | Where-Object { $_ }) -join '-'
}

function Get-CtsGuestConfiguration {
    <#
    .SYNOPSIS
    Returns the desired guest configuration for one lab VM. The object is plain data so it can be passed
    to a remote session and recorded as setup evidence.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)]$Topology,
        [Parameter(Mandatory)][string]$VmName
    )
    $vm = @($Topology.Vms | Where-Object { $_.Name -eq $VmName })
    if ($vm.Count -ne 1) { throw "VM '$VmName' is not in the topology." }
    $vm = $vm[0]
    $root = $Definition.guest.rootPath
    $group = 'ctsTraffic Reliability'
    $testNames = @($vm.TestNics | ForEach-Object Name)
    $testRemote = @($vm.TestNics | ForEach-Object { "192.168.$($_.LinkNumber).0/$($_.IPv4Prefix)"; "fd00:$($_.LinkNumber)::/$($_.IPv6Prefix)" })

    $adapters = @([ordered]@{
            name = $vm.MgmtNic.Name; role = 'Management'; mac = ConvertTo-CtsMacDisplay $vm.MgmtNic.MacAddress
            ipv4 = $vm.MgmtNic.IPv4Address; ipv4Prefix = [int]$vm.MgmtNic.IPv4Prefix; ipv6 = $null; ipv6Prefix = $null; ipv6Enabled = $false
        })
    foreach ($nic in $vm.TestNics) {
        $adapters += [ordered]@{
            name = $nic.Name; role = 'Test'; mac = ConvertTo-CtsMacDisplay $nic.MacAddress
            ipv4 = $nic.IPv4Address; ipv4Prefix = [int]$nic.IPv4Prefix; ipv6 = $nic.IPv6Address; ipv6Prefix = [int]$nic.IPv6Prefix; ipv6Enabled = $true
        }
    }

    $rule = { param($name, $display, $protocol, $port, $icmp, $program, $aliases, $remote)
        [ordered]@{ name = $name; displayName = $display; group = $group; direction = 'Inbound'; action = 'Allow'; profile = 'Any'
            protocol = $protocol; localPort = $port; icmpType = $icmp; program = $program
            interfaceAlias = @($aliases | Sort-Object); remoteAddress = @($remote | Sort-Object) }
    }
    $rules = @(
        & $rule 'ctsRel-ctsTraffic-TCP' 'ctsTraffic TCP' 'TCP' ([string]$Definition.test.ctsTrafficPort) $null (Join-Path $root 'tools\ctsTraffic.exe') $testNames $testRemote
        & $rule 'ctsRel-ICMPv4-Echo' 'ICMPv4 echo' 'ICMPv4' $null '8' $null $testNames @($testRemote | Where-Object { $_ -notlike '*:*' })
        & $rule 'ctsRel-ICMPv6-Echo' 'ICMPv6 echo' 'ICMPv6' $null '128' $null $testNames @($testRemote | Where-Object { $_ -like '*:*' })
        & $rule 'ctsRel-WinRM-HTTP' 'WinRM HTTP (management)' 'TCP' '5985' $null $null @($vm.MgmtNic.Name) @($Definition.management.subnet)
    )

    $memoryMB = [int]$Definition.vm.memoryGB * 1024
    return [pscustomobject][ordered]@{
        schemaVersion  = 1
        vmName         = $vm.Name
        computerName   = $vm.ComputerName
        rootPath       = $root
        directories    = @($root, (Join-Path $root 'tools'), (Join-Path $root 'dumps'), (Join-Path $root 'runs'))
        adapters       = $adapters
        unattendCleanup = $true
        firewall       = [ordered]@{ group = $group; rules = $rules; disableBuiltinWinRMRules = $true }
        winrm          = [ordered]@{ enable = $true }
        crashControl   = [ordered]@{
            registryPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
            values       = [ordered]@{ CrashDumpEnabled = 1; AutoReboot = 1; Overwrite = 1; NMICrashDump = 1; AlwaysKeepMemoryDump = 1 }
        }
        pageFile       = [ordered]@{ path = 'C:\pagefile.sys'; initialMB = $memoryMB + 1024; maximumMB = $memoryMB + 1024 }
        werLocalDumps  = @([ordered]@{
                registryPath = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\ctsTraffic.exe'
                image = 'ctsTraffic.exe'; folder = (Join-Path $root 'dumps'); dumpType = 2; dumpCount = 10
            })
        power          = [ordered]@{ schemeGuid = '8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c'; hibernate = $false }
        windowsUpdate  = [ordered]@{ disable = $true; policyPath = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' }
        defender       = [ordered]@{ realTimeProtection = $false }
    }
}

# Every section is optional: an absent or null section is skipped, so tests can exercise one section at a time.
$script:CtsGuestConfigScript = @'
param([Parameter(Mandatory)]$Config, [bool]$DryRun = $true)
Set-StrictMode -Off
$ErrorActionPreference = 'Stop'
$results = [System.Collections.Generic.List[object]]::new()

function Invoke-ConfigItem {
    param([string]$Name, [scriptblock]$Get, [scriptblock]$Test, [scriptblock]$Set, [switch]$NeedsReboot, [string]$BlockedHint)
    $r = [ordered]@{ item = $Name; status = $null; before = $null; after = $null; error = $null; rebootRequired = $false }
    try {
        $before = & $Get
        $r.before = $before
        if (& $Test $before) { $r.status = 'Compliant' }
        elseif ($DryRun) { $r.status = 'WouldChange' }
        else {
            & $Set $before
            $after = & $Get
            $r.after = $after
            if (& $Test $after) { $r.status = 'Changed'; $r.rebootRequired = [bool]$NeedsReboot }
            elseif ($BlockedHint) { $r.status = 'Blocked'; $r.error = $BlockedHint }
            else { $r.status = 'Failed'; $r.error = 'State still differs after apply.' }
        }
    }
    catch { $r.status = 'Failed'; $r.error = $_.Exception.Message }
    $results.Add([pscustomobject]$r)
}

function ConvertTo-MaskForm([string]$a) {
    if ($a -match '^(\d+\.\d+\.\d+\.\d+)/(\d+)$') {
        $p = [int]$Matches[2]
        $m = [uint32]([uint64]4294967296 - [uint64][math]::Pow(2, 32 - $p))
        $b = [BitConverter]::GetBytes($m); [Array]::Reverse($b)
        return "$($Matches[1])/$([System.Net.IPAddress]::new($b))"
    }
    return $a
}

function Get-RegValues([string]$Path, [string[]]$Names) {
    $o = [ordered]@{}
    $item = Get-ItemProperty -LiteralPath $Path -ErrorAction SilentlyContinue
    foreach ($n in $Names) { $o[$n] = if ($item -and $item.PSObject.Properties[$n]) { $item.$n } else { $null } }
    return [pscustomobject]$o
}

function Set-RegValues([string]$Path, $Values) {
    if (-not (Test-Path -LiteralPath $Path)) { $null = New-Item -Path $Path -Force }
    foreach ($p in $Values.GetEnumerator()) {
        $type = if ($p.Value -is [string]) { 'ExpandString' } else { 'DWord' }
        $null = New-ItemProperty -LiteralPath $Path -Name $p.Key -Value $p.Value -PropertyType $type -Force
    }
}

function ConvertTo-Hashtable($o) {
    $h = [ordered]@{}
    if ($o -is [System.Collections.IDictionary]) { foreach ($k in $o.Keys) { $h[$k] = $o[$k] } }
    else { foreach ($p in $o.PSObject.Properties) { $h[$p.Name] = $p.Value } }
    return $h
}

if ($Config.directories) {
    Invoke-ConfigItem -Name 'Directories' -Get { @($Config.directories | Where-Object { -not (Test-Path -LiteralPath $_) }) } `
        -Test { param($s) -not $s } -Set { param($s) foreach ($d in $s) { $null = New-Item -ItemType Directory -Path $d -Force } }
}

if ($Config.unattendCleanup) {
    Invoke-ConfigItem -Name 'UnattendCleanup' -Get {
        @(Get-ChildItem -LiteralPath (Join-Path $env:WINDIR 'Panther') -Filter '*.xml' -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { (Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue) -match '<(Administrator)?Password>' } | ForEach-Object FullName)
    } -Test { param($s) -not $s } -Set { param($s) foreach ($f in $s) { Remove-Item -LiteralPath $f -Force } }
}

foreach ($adapter in @($Config.adapters | Where-Object { $_ })) {
    Invoke-ConfigItem -Name "Adapter:$($adapter.name)" -Get {
        $na = @(Get-NetAdapter | Where-Object { $_.MacAddress -eq $adapter.mac })
        if ($na.Count -ne 1) { throw "Expected one adapter with MAC $($adapter.mac); found $($na.Count)." }
        $na = $na[0]
        $v4 = @(Get-NetIPAddress -InterfaceIndex $na.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -notlike '169.254.*' } | ForEach-Object { "$($_.IPAddress)/$($_.PrefixLength)" })
        $v6 = @(Get-NetIPAddress -InterfaceIndex $na.ifIndex -AddressFamily IPv6 -ErrorAction SilentlyContinue | Where-Object { $_.PrefixOrigin -eq 'Manual' } | ForEach-Object { "$($_.IPAddress)/$($_.PrefixLength)" })
        $ipif = Get-NetIPInterface -InterfaceIndex $na.ifIndex -AddressFamily IPv4
        [pscustomobject]@{
            ifIndex     = $na.ifIndex
            name        = $na.Name
            dhcp        = [string]$ipif.Dhcp
            ipv4        = $v4
            ipv6        = $v6
            registerDns = [bool](Get-DnsClient -InterfaceIndex $na.ifIndex).RegisterThisConnectionsAddress
            ipv6Enabled = [bool](Get-NetAdapterBinding -Name $na.Name -ComponentID ms_tcpip6).Enabled
        }
    } -Test { param($s)
        $wantV6 = if ($adapter.ipv6) { @("$($adapter.ipv6)/$($adapter.ipv6Prefix)") } else { @() }
        $s.name -eq $adapter.name -and $s.dhcp -eq 'Disabled' -and -not $s.registerDns -and $s.ipv6Enabled -eq [bool]$adapter.ipv6Enabled -and
        (@($s.ipv4) -join ',') -eq "$($adapter.ipv4)/$($adapter.ipv4Prefix)" -and (@($s.ipv6) -join ',') -eq ($wantV6 -join ',')
    } -Set { param($s)
        if ($s.name -ne $adapter.name) { Rename-NetAdapter -Name $s.name -NewName $adapter.name }
        $i = $s.ifIndex
        Set-NetIPInterface -InterfaceIndex $i -AddressFamily IPv4 -Dhcp Disabled
        Set-DnsClient -InterfaceIndex $i -RegisterThisConnectionsAddress $false
        if ([bool]$adapter.ipv6Enabled) { Enable-NetAdapterBinding -Name $adapter.name -ComponentID ms_tcpip6 }
        else { Disable-NetAdapterBinding -Name $adapter.name -ComponentID ms_tcpip6 }
        $want = @("$($adapter.ipv4)/$($adapter.ipv4Prefix)")
        if ($adapter.ipv6) { $want += "$($adapter.ipv6)/$($adapter.ipv6Prefix)" }
        foreach ($have in @($s.ipv4) + @($s.ipv6)) {
            if ($have -notin $want) { Remove-NetIPAddress -InterfaceIndex $i -IPAddress ($have.Split('/')[0]) -Confirm:$false }
        }
        foreach ($w in $want) {
            if ($w -notin (@($s.ipv4) + @($s.ipv6))) {
                $parts = $w.Split('/')
                $null = New-NetIPAddress -InterfaceIndex $i -IPAddress $parts[0] -PrefixLength ([int]$parts[1]) -SkipAsSource $false
            }
        }
    }
}

if ($Config.winrm.enable) {
    Invoke-ConfigItem -Name 'WinRM' -Get {
        $svc = Get-Service -Name WinRM
        [pscustomobject]@{ status = [string]$svc.Status; startType = [string]$svc.StartType; listeners = @(Get-ChildItem -Path WSMan:\localhost\Listener -ErrorAction SilentlyContinue).Count }
    } -Test { param($s) $s.status -eq 'Running' -and $s.startType -eq 'Automatic' -and $s.listeners -gt 0 } `
      -Set { param($s) $null = Enable-PSRemoting -SkipNetworkProfileCheck -Force }
}

if ($Config.firewall.disableBuiltinWinRMRules) {
    Invoke-ConfigItem -Name 'Firewall:BuiltinWinRMRulesDisabled' -Get {
        @(Get-NetFirewallRule -Group '@FirewallAPI.dll,-30267' -ErrorAction SilentlyContinue | Where-Object { $_.Enabled -eq 'True' } | ForEach-Object Name)
    } -Test { param($s) -not $s } -Set { param($s) foreach ($n in $s) { Disable-NetFirewallRule -Name $n } }
}

foreach ($rule in $Config.firewall.rules) {
    Invoke-ConfigItem -Name "Firewall:$($rule.name)" -Get {
        $fw = Get-NetFirewallRule -Name $rule.name -ErrorAction SilentlyContinue
        if (-not $fw) { return [pscustomobject]@{ exists = $false } }
        $port = $fw | Get-NetFirewallPortFilter
        $app = $fw | Get-NetFirewallApplicationFilter
        [pscustomobject]@{
            exists         = $true
            enabled        = [string]$fw.Enabled
            group          = $fw.Group
            direction      = [string]$fw.Direction
            action         = [string]$fw.Action
            profile        = [string]$fw.Profile
            protocol       = [string]$port.Protocol
            localPort      = (@($port.LocalPort) -join ',')
            icmpType       = (@($port.IcmpType) -join ',')
            program        = [string]$app.Program
            interfaceAlias = (@(($fw | Get-NetFirewallInterfaceFilter).InterfaceAlias) | Sort-Object) -join ','
            remoteAddress  = (@(($fw | Get-NetFirewallAddressFilter).RemoteAddress) | Sort-Object) -join ','
        }
    } -Test { param($s)
        $s.exists -and $s.enabled -eq 'True' -and $s.group -eq $rule.group -and $s.direction -eq $rule.direction -and $s.action -eq $rule.action -and
        $s.profile -eq $rule.profile -and $s.protocol -eq $rule.protocol -and
        ($null -eq $rule.localPort -or $s.localPort -eq $rule.localPort) -and
        ($null -eq $rule.icmpType -or $s.icmpType -eq $rule.icmpType -or $s.icmpType -like "$($rule.icmpType):*") -and
        ($null -eq $rule.program -or $s.program -eq $rule.program) -and
        $s.interfaceAlias -eq ((@($rule.interfaceAlias) | Sort-Object) -join ',') -and
        $s.remoteAddress -eq ((@($rule.remoteAddress | ForEach-Object { ConvertTo-MaskForm $_ }) | Sort-Object) -join ',')
    } -Set { param($s)
        if ($s.exists) { Remove-NetFirewallRule -Name $rule.name }
        $p = @{ Name = $rule.name; DisplayName = $rule.displayName; Group = $rule.group; Direction = $rule.direction; Action = $rule.action
            Profile = $rule.profile; Protocol = $rule.protocol; InterfaceAlias = @($rule.interfaceAlias); RemoteAddress = @($rule.remoteAddress); Enabled = 'True' }
        if ($rule.localPort) { $p.LocalPort = $rule.localPort }
        if ($rule.icmpType) { $p.IcmpType = $rule.icmpType }
        if ($rule.program) { $p.Program = $rule.program }
        $null = New-NetFirewallRule @p
    }
}

if ($Config.crashControl) {
    $crashPath = $Config.crashControl.registryPath
    $crashWant = ConvertTo-Hashtable $Config.crashControl.values
    Invoke-ConfigItem -Name 'CrashControl' -NeedsReboot -Get { Get-RegValues $crashPath @($crashWant.Keys) } `
        -Test { param($s) foreach ($k in $crashWant.Keys) { if ($s.$k -ne $crashWant[$k]) { return $false } }; $true } `
        -Set { param($s) Set-RegValues $crashPath $crashWant }
}

if ($Config.pageFile) {
    Invoke-ConfigItem -Name 'PageFile' -NeedsReboot -Get {
        [pscustomobject]@{
            automatic = [bool](Get-CimInstance -ClassName Win32_ComputerSystem).AutomaticManagedPagefile
            settings  = @(Get-CimInstance -ClassName Win32_PageFileSetting | ForEach-Object { "$($_.Name)|$($_.InitialSize)|$($_.MaximumSize)" })
        }
    } -Test { param($s)
        -not $s.automatic -and (@($s.settings) -join ';') -eq ('{0}|{1}|{2}' -f $Config.pageFile.path, $Config.pageFile.initialMB, $Config.pageFile.maximumMB)
    } -Set { param($s)
        if ($s.automatic) { $null = Get-CimInstance -ClassName Win32_ComputerSystem | Set-CimInstance -Property @{ AutomaticManagedPagefile = $false } -PassThru }
        foreach ($pf in @(Get-CimInstance -ClassName Win32_PageFileSetting)) { if ($pf.Name -ne $Config.pageFile.path) { Remove-CimInstance -InputObject $pf } }
        $pf = Get-CimInstance -ClassName Win32_PageFileSetting | Where-Object { $_.Name -eq $Config.pageFile.path }
        if (-not $pf) { $pf = New-CimInstance -ClassName Win32_PageFileSetting -Property @{ Name = $Config.pageFile.path } }
        $null = $pf | Set-CimInstance -Property @{ InitialSize = [uint32]$Config.pageFile.initialMB; MaximumSize = [uint32]$Config.pageFile.maximumMB } -PassThru
    }
}

foreach ($wer in @($Config.werLocalDumps | Where-Object { $_ })) {
    $werPath = $wer.registryPath
    $werWant = [ordered]@{ DumpFolder = [string]$wer.folder; DumpType = [int]$wer.dumpType; DumpCount = [int]$wer.dumpCount }
    Invoke-ConfigItem -Name "WerLocalDumps:$($wer.image)" -Get { Get-RegValues $werPath @($werWant.Keys) } `
        -Test { param($s) foreach ($k in $werWant.Keys) { if ($s.$k -ne $werWant[$k]) { return $false } }; $true } `
        -Set { param($s) Set-RegValues $werPath $werWant }
}

if ($Config.power) {
    Invoke-ConfigItem -Name 'PowerPlan' -Get {
        $active = (& powercfg.exe /getactivescheme) -join ' '
        $hib = Get-RegValues 'HKLM:\SYSTEM\CurrentControlSet\Control\Power' @('HibernateEnabled')
        [pscustomobject]@{ activeScheme = if ($active -match '([0-9a-fA-F-]{36})') { $Matches[1].ToLowerInvariant() } else { $active }; hibernateEnabled = [int]$hib.HibernateEnabled }
    } -Test { param($s) $s.activeScheme -eq $Config.power.schemeGuid -and $s.hibernateEnabled -eq [int][bool]$Config.power.hibernate } -Set { param($s)
        $g = $Config.power.schemeGuid
        & powercfg.exe /setactive $g 2>$null
        if ($LASTEXITCODE -ne 0) { $null = & powercfg.exe /duplicatescheme $g $g; & powercfg.exe /setactive $g }
        & powercfg.exe /hibernate $(if ($Config.power.hibernate) { 'on' } else { 'off' })
        & powercfg.exe /change standby-timeout-ac 0
        & powercfg.exe /change monitor-timeout-ac 0
    }
}

if ($Config.windowsUpdate -and $Config.windowsUpdate.disable) {
    $wuPolicy = $Config.windowsUpdate.policyPath
    Invoke-ConfigItem -Name 'WindowsUpdate' -Get {
        $svc = Get-Service -Name wuauserv
        [pscustomobject]@{ startType = [string]$svc.StartType; noAutoUpdate = (Get-RegValues $wuPolicy @('NoAutoUpdate')).NoAutoUpdate }
    } -Test { param($s) $s.startType -eq 'Disabled' -and $s.noAutoUpdate -eq 1 } -Set { param($s)
        Set-RegValues $wuPolicy ([ordered]@{ NoAutoUpdate = 1 })
        Stop-Service -Name wuauserv -Force -ErrorAction SilentlyContinue
        Set-Service -Name wuauserv -StartupType Disabled
    }
}

if ($Config.defender) {
    Invoke-ConfigItem -Name 'DefenderRealTimeProtection' -Get {
        $mp = Get-MpComputerStatus
        [pscustomobject]@{ realTimeProtection = [bool]$mp.RealTimeProtectionEnabled; tamperProtected = [bool]$mp.IsTamperProtected }
    } -Test { param($s) $s.realTimeProtection -eq [bool]$Config.defender.realTimeProtection } `
      -Set { param($s) Set-MpPreference -DisableRealtimeMonitoring (-not [bool]$Config.defender.realTimeProtection) } `
      -BlockedHint 'Real-time protection did not change; Tamper Protection is probably on. Turn it off in Windows Security, then rerun.'
}

$all = @($results)
[pscustomobject]@{
    computerName   = $env:COMPUTERNAME
    capturedUtc    = [DateTime]::UtcNow.ToString('o')
    dryRun         = [bool]$DryRun
    items          = $all
    rebootRequired = [bool](@($all | Where-Object rebootRequired).Count)
    counts         = [pscustomobject]@{
        compliant   = @($all | Where-Object status -eq 'Compliant').Count
        changed     = @($all | Where-Object status -eq 'Changed').Count
        wouldChange = @($all | Where-Object status -eq 'WouldChange').Count
        blocked     = @($all | Where-Object status -eq 'Blocked').Count
        failed      = @($all | Where-Object status -eq 'Failed').Count
    }
}
'@

function Get-CtsGuestConfigScript {
    <#
    .SYNOPSIS
    Returns the guest configuration script text (param: -Config <object> [-DryRun <bool>, default $true]).
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return $script:CtsGuestConfigScript
}

function Invoke-CtsGuestConfiguration {
    <#
    .SYNOPSIS
    Runs the guest configuration script in a guest session (or locally when no session is given).
    Without -Apply it is a dry run that only reports Compliant / WouldChange per item.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Configuration,
        [object]$GuestSession,
        [switch]$Apply
    )
    $block = [scriptblock]::Create($script:CtsGuestConfigScript)
    $dry = -not $Apply
    if ($GuestSession) {
        $op = if ($dry) { 'GuestConfigDryRun' } else { 'GuestConfigApply' }
        return Invoke-CtsGuestCommand -GuestSession $GuestSession -ScriptBlock $block -ArgumentList @($Configuration, $dry) -Operation $op
    }
    return & $block -Config $Configuration -DryRun $dry
}
