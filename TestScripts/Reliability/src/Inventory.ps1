# Device and Hyper-V inventory used by system snapshots. Read-only: nothing here changes host or guest state.

$script:CtsDeviceInventoryScript = {
    # Self-contained: runs locally or inside a guest (Windows PowerShell 5.1 compatible).
    param([string]$RootPath = 'C:\ctsReliability', [string[]]$RouteDetailInterface = @('*'))

    $ErrorActionPreference = 'Stop'
    function Section {
        param([string]$Source, [scriptblock]$Body)
        $captured = [DateTime]::UtcNow.ToString('o')
        try {
            $data = & $Body
            [ordered]@{ status = 'ok'; source = $Source; capturedUtc = $captured; data = $data }
        }
        catch {
            [ordered]@{ status = 'unavailable'; source = $Source; capturedUtc = $captured; reason = $_.Exception.Message; data = $null }
        }
    }
    function FileInfo {
        param([string]$Path)
        if (-not (Test-Path -LiteralPath $Path)) { return [ordered]@{ path = $Path; present = $false } }
        $item = Get-Item -LiteralPath $Path
        [ordered]@{
            path        = $Path
            present     = $true
            fileVersion = [string]$item.VersionInfo.FileVersion
            productVersion = [string]$item.VersionInfo.ProductVersion
            bytes       = $item.Length
            sha256      = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        }
    }
    function RegValues {
        param([string]$Key)
        if (-not (Test-Path -LiteralPath $Key)) { return $null }
        $props = Get-ItemProperty -LiteralPath $Key
        $result = [ordered]@{}
        foreach ($p in ($props.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | Sort-Object Name)) { $result[$p.Name] = $p.Value }
        $result
    }

    $sections = [ordered]@{}

    $sections.identity = Section 'Win32_OperatingSystem, Win32_ComputerSystem, HKLM CurrentVersion' {
        $os = Get-CimInstance Win32_OperatingSystem
        $cs = Get-CimInstance Win32_ComputerSystem
        $cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
        [ordered]@{
            computerName   = $env:COMPUTERNAME
            osCaption      = $os.Caption
            osVersion      = $os.Version
            osBuild        = $os.BuildNumber
            ubr            = $cv.UBR
            displayVersion = $cv.DisplayVersion
            editionId      = $cv.EditionID
            osArchitecture = $os.OSArchitecture
            processorArchitecture = $env:PROCESSOR_ARCHITECTURE
            manufacturer   = $cs.Manufacturer
            model          = $cs.Model
            lastBootUtc    = $os.LastBootUpTime.ToUniversalTime().ToString('o')
            installDateUtc = $os.InstallDate.ToUniversalTime().ToString('o')
        }
    }

    $sections.compute = Section 'Win32_Processor, Win32_ComputerSystem, Win32_OperatingSystem' {
        $cpus = @(Get-CimInstance Win32_Processor)
        $cs = Get-CimInstance Win32_ComputerSystem
        $os = Get-CimInstance Win32_OperatingSystem
        [ordered]@{
            processorModels  = @($cpus | ForEach-Object { $_.Name } | Sort-Object -Unique)
            physicalCores    = [int](($cpus | Measure-Object NumberOfCores -Sum).Sum)
            logicalProcessors = [int](($cpus | Measure-Object NumberOfLogicalProcessors -Sum).Sum)
            l2CacheKB        = @($cpus | ForEach-Object { $_.L2CacheSize })
            l3CacheKB        = @($cpus | ForEach-Object { $_.L3CacheSize })
            totalMemoryBytes = [int64]$cs.TotalPhysicalMemory
            availableMemoryBytes = [int64]$os.FreePhysicalMemory * 1024
        }
    }

    $sections.storage = Section 'Win32_LogicalDisk (DriveType=3)' {
        @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType = 3' | Sort-Object DeviceID | ForEach-Object {
                [ordered]@{ drive = $_.DeviceID; fileSystem = $_.FileSystem; sizeBytes = [int64]$_.Size; freeBytes = [int64]$_.FreeSpace }
            })
    }

    $sections.adapters = Section 'Get-NetAdapter, Get-NetAdapterAdvancedProperty' {
        @(Get-NetAdapter -IncludeHidden:$false | Sort-Object Name | ForEach-Object {
                $hvName = $null
                try {
                    $hvName = (Get-NetAdapterAdvancedProperty -Name $_.Name -ErrorAction Stop |
                        Where-Object { $_.DisplayName -eq 'Hyper-V Network Adapter Name' } | Select-Object -First 1).DisplayValue
                }
                catch { }
                [ordered]@{
                    name = $_.Name; interfaceDescription = $_.InterfaceDescription; ifIndex = $_.ifIndex
                    macAddress = $_.MacAddress; status = [string]$_.Status; linkSpeed = [string]$_.LinkSpeed
                    mtu = $_.MtuSize; driverVersion = $_.DriverVersion; hyperVAdapterName = $hvName
                }
            })
    }

    $sections.ipAddresses = Section 'Get-NetIPAddress' {
        @(Get-NetIPAddress | Where-Object { $_.InterfaceAlias -notlike 'Loopback*' } | Sort-Object InterfaceAlias, AddressFamily, IPAddress | ForEach-Object {
                [ordered]@{
                    interfaceAlias = $_.InterfaceAlias; addressFamily = [string]$_.AddressFamily; ipAddress = $_.IPAddress
                    prefixLength = $_.PrefixLength; prefixOrigin = [string]$_.PrefixOrigin; suffixOrigin = [string]$_.SuffixOrigin
                    addressState = [string]$_.AddressState
                }
            })
    }

    $sections.ipInterfaces = Section 'Get-NetIPInterface' {
        @(Get-NetIPInterface | Where-Object { $_.InterfaceAlias -notlike 'Loopback*' } | Sort-Object InterfaceAlias, AddressFamily | ForEach-Object {
                [ordered]@{
                    interfaceAlias = $_.InterfaceAlias; addressFamily = [string]$_.AddressFamily; dhcp = [string]$_.Dhcp
                    connectionState = [string]$_.ConnectionState; nlMtu = $_.NlMtu; interfaceMetric = $_.InterfaceMetric
                }
            })
    }

    $sections.routes = Section 'Get-NetRoute' {
        # Hosts on corporate networks can carry thousands of VPN/Wi-Fi routes; keep full detail only for
        # interfaces matching $RouteDetailInterface and record a per-interface count for the rest.
        $all = @(Get-NetRoute | Where-Object { $_.InterfaceAlias -notlike 'Loopback*' })
        $detail = @(); $other = @()
        foreach ($route in $all) {
            $match = $false
            foreach ($pattern in $RouteDetailInterface) { if ($route.InterfaceAlias -like $pattern) { $match = $true; break } }
            if ($match) { $detail += $route } else { $other += $route }
        }
        [ordered]@{
            detailInterfaceFilter = @($RouteDetailInterface)
            routes = @($detail | Sort-Object InterfaceAlias, AddressFamily, DestinationPrefix, NextHop | ForEach-Object {
                    [ordered]@{
                        interfaceAlias = $_.InterfaceAlias; addressFamily = [string]$_.AddressFamily; destinationPrefix = $_.DestinationPrefix
                        nextHop = $_.NextHop; routeMetric = $_.RouteMetric; protocol = [string]$_.Protocol; store = [string]$_.Store
                    }
                })
            otherInterfaceRouteCounts = @($other | Group-Object InterfaceAlias | Sort-Object Name | ForEach-Object {
                    [ordered]@{ name = $_.Name; routeCount = $_.Count }
                })
        }
    }

    $sections.neighbors = Section 'Get-NetNeighbor (volatile)' {
        @(Get-NetNeighbor | Where-Object { $_.InterfaceAlias -notlike 'Loopback*' -and [string]$_.State -ne 'Permanent' } |
            Sort-Object InterfaceAlias, IPAddress | ForEach-Object {
                [ordered]@{ interfaceAlias = $_.InterfaceAlias; ipAddress = $_.IPAddress; linkLayerAddress = $_.LinkLayerAddress; state = [string]$_.State }
            })
    }

    $sections.dns = Section 'Get-DnsClientServerAddress, Get-DnsClient' {
        @(Get-DnsClient | Sort-Object InterfaceAlias | ForEach-Object {
                $alias = $_.InterfaceAlias
                [ordered]@{
                    interfaceAlias = $alias
                    registerThisConnectionsAddress = $_.RegisterThisConnectionsAddress
                    servers = @(Get-DnsClientServerAddress -InterfaceAlias $alias | ForEach-Object { $_.ServerAddresses } | Sort-Object)
                }
            })
    }

    $sections.networkProfiles = Section 'Get-NetConnectionProfile' {
        @(Get-NetConnectionProfile | Sort-Object InterfaceAlias | ForEach-Object {
                [ordered]@{ interfaceAlias = $_.InterfaceAlias; networkCategory = [string]$_.NetworkCategory; name = $_.Name }
            })
    }

    $sections.firewall = Section 'Get-NetFirewallProfile, Get-NetFirewallRule -Group "ctsTraffic Reliability"' {
        [ordered]@{
            profiles = @(Get-NetFirewallProfile | Sort-Object Name | ForEach-Object {
                    [ordered]@{ name = $_.Name; enabled = [string]$_.Enabled; defaultInboundAction = [string]$_.DefaultInboundAction; defaultOutboundAction = [string]$_.DefaultOutboundAction }
                })
            labRules = @(Get-NetFirewallRule -Group 'ctsTraffic Reliability' -ErrorAction SilentlyContinue | Sort-Object DisplayName | ForEach-Object {
                    $port = $_ | Get-NetFirewallPortFilter
                    $app = $_ | Get-NetFirewallApplicationFilter
                    $if = $_ | Get-NetFirewallInterfaceFilter
                    $addr = $_ | Get-NetFirewallAddressFilter
                    [ordered]@{
                        displayName = $_.DisplayName; enabled = [string]$_.Enabled; direction = [string]$_.Direction; action = [string]$_.Action
                        profile = [string]$_.Profile; protocol = [string]$port.Protocol; localPort = @($port.LocalPort); icmpType = @($port.IcmpType)
                        program = $app.Program; interfaceAlias = @($if.InterfaceAlias); remoteAddress = @($addr.RemoteAddress)
                    }
                })
        }
    }

    $sections.verifier = Section 'HKLM Image File Execution Options, verifier.exe /querysettings' {
        $ifeo = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options'
        $appVerifier = @(Get-ChildItem -LiteralPath $ifeo -ErrorAction SilentlyContinue | ForEach-Object {
                $p = Get-ItemProperty -LiteralPath $_.PSPath
                $names = $p.PSObject.Properties.Name
                $globalFlag = if ($names -contains 'GlobalFlag') { [int64]$p.GlobalFlag } else { 0 }
                if (($names -contains 'VerifierDlls') -or ($names -contains 'VerifierFlags') -or ($globalFlag -band 0x100)) {
                    [ordered]@{
                        image = $_.PSChildName
                        globalFlag = $globalFlag
                        verifierFlags = if ($names -contains 'VerifierFlags') { $p.VerifierFlags } else { $null }
                        verifierDlls = if ($names -contains 'VerifierDlls') { $p.VerifierDlls } else { $null }
                        verifierActivationFilter = if ($names -contains 'VerifierActivationFilter') { $p.VerifierActivationFilter } else { $null }
                    }
                }
            } | Sort-Object { $_.image })
        $dv = RegValues 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management'
        [ordered]@{
            applicationVerifierImages = $appVerifier
            driverVerifierDrivers     = if ($dv -and $dv.Contains('VerifyDrivers')) { $dv['VerifyDrivers'] } else { $null }
            driverVerifierLevel       = if ($dv -and $dv.Contains('VerifyDriverLevel')) { $dv['VerifyDriverLevel'] } else { $null }
        }
    }

    $sections.crashControl = Section 'HKLM CrashControl, Win32_PageFileSetting, WER LocalDumps' {
        $cc = RegValues 'HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
        [ordered]@{
            crashControl = $cc
            automaticManagedPagefile = (Get-CimInstance Win32_ComputerSystem).AutomaticManagedPagefile
            pageFiles = @(Get-CimInstance Win32_PageFileSetting | Sort-Object Name | ForEach-Object { [ordered]@{ name = $_.Name; initialMB = $_.InitialSize; maximumMB = $_.MaximumSize } })
            werLocalDumps = @(Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps' -ErrorAction SilentlyContinue | Sort-Object PSChildName | ForEach-Object {
                    [ordered]@{ image = $_.PSChildName; values = (RegValues $_.PSPath) }
                })
        }
    }

    $sections.power = Section 'powercfg /getactivescheme' {
        [ordered]@{ activeScheme = ((& powercfg.exe /getactivescheme) -join ' ').Trim() }
    }

    $sections.windowsUpdate = Section 'HKLM Policies WindowsUpdate\AU, Win32_Service wuauserv' {
        $svc = Get-CimInstance Win32_Service -Filter "Name='wuauserv'"
        [ordered]@{
            policy = RegValues 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU'
            wuauservStartMode = if ($svc) { $svc.StartMode } else { $null }
        }
    }

    $sections.defender = Section 'Get-MpComputerStatus' {
        $s = Get-MpComputerStatus
        [ordered]@{
            realTimeProtectionEnabled = $s.RealTimeProtectionEnabled
            antivirusEnabled = $s.AntivirusEnabled
            isTamperProtected = $s.IsTamperProtected
            amProductVersion = $s.AMProductVersion
        }
    }

    $sections.networkDrivers = Section 'File versions of networking drivers in System32\drivers' {
        $drivers = 'tcpip.sys', 'afd.sys', 'netio.sys', 'ndis.sys', 'netvsc.sys', 'vmswitch.sys', 'wfplwfs.sys'
        @($drivers | ForEach-Object { FileInfo (Join-Path $env:SystemRoot "System32\drivers\$_") } | Where-Object { $_.present })
    }

    $sections.tools = Section "Files under $RootPath\tools" {
        $dir = Join-Path $RootPath 'tools'
        if (-not (Test-Path -LiteralPath $dir)) { return @() }
        @(Get-ChildItem -LiteralPath $dir -File -Recurse | Sort-Object FullName | ForEach-Object { FileInfo $_.FullName })
    }

    $sections.debuggers = Section 'cdb.exe/ntsd.exe/appverif.exe search (install path to be confirmed by pilot)' {
        $roots = @(
            (Join-Path ${env:ProgramFiles} 'Debugging Tools for Windows'),
            (Join-Path ${env:ProgramFiles} 'Windows Kits\10\Debuggers'),
            'C:\Debuggers',
            (Join-Path $env:SystemRoot 'System32')
        ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
        $found = foreach ($name in 'cdb.exe', 'ntsd.exe', 'appverif.exe') {
            $hit = $null
            foreach ($r in $roots) {
                $hit = Get-ChildItem -LiteralPath $r -Filter $name -File -Recurse -Depth 2 -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($hit) { break }
            }
            if ($hit) { FileInfo $hit.FullName } else { [ordered]@{ path = $name; present = $false } }
        }
        @($found)
    }

    $sections.timeSync = Section 'w32tm /query /source' {
        [ordered]@{ source = ((& w32tm.exe /query /source) -join ' ').Trim() }
    }

    $sections.health = Section 'Pending-reboot registry keys, dump files' {
        [ordered]@{
            rebootPending = (Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending') -or
                (Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired')
            memoryDumpPresent = Test-Path (Join-Path $env:SystemRoot 'MEMORY.DMP')
            minidumpCount = @(Get-ChildItem (Join-Path $env:SystemRoot 'Minidump') -File -ErrorAction SilentlyContinue).Count
        }
    }

    [ordered]@{
        schema   = 'ctsReliability.deviceInventory/1'
        sections = $sections
    }
}

function Get-CtsDeviceInventoryScript {
    [CmdletBinding()]
    param()
    return $script:CtsDeviceInventoryScript
}

function Get-CtsDeviceInventory {
    <#
    .SYNOPSIS
    Runs the device inventory on the local machine.
    #>
    [CmdletBinding()]
    param([string]$RootPath = 'C:\ctsReliability', [string[]]$RouteDetailInterface = @('*'))
    return (& $script:CtsDeviceInventoryScript $RootPath $RouteDetailInterface)
}

function Get-CtsHyperVInventory {
    <#
    .SYNOPSIS
    Captures Hyper-V host, switch, host vNIC, and VM configuration. Requires Hyper-V administrator rights;
    returns status 'unavailable' with a reason otherwise.
    .PARAMETER VMName
    VMs to capture in full. Names of all other VMs are listed but not inspected.
    #>
    [CmdletBinding()]
    param([string[]]$VMName = @())

    $captured = [DateTime]::UtcNow.ToString('o')
    try {
        $vmHost = Get-VMHost -ErrorAction Stop
        $switches = @(Get-VMSwitch -ErrorAction Stop | Sort-Object Name | ForEach-Object {
                $sw = $_
                [ordered]@{
                    name = $sw.Name; id = [string]$sw.Id; switchType = [string]$sw.SwitchType
                    netAdapterInterfaceDescription = $sw.NetAdapterInterfaceDescription
                    physicalAdapterBound = -not [string]::IsNullOrEmpty($sw.NetAdapterInterfaceDescription)
                    allowManagementOS = $sw.AllowManagementOS
                    enabledExtensions = @(Get-VMSwitchExtension -VMSwitch $sw -ErrorAction SilentlyContinue | Where-Object Enabled | ForEach-Object Name | Sort-Object)
                }
            })
        $hostVnics = @(Get-VMNetworkAdapter -ManagementOS -ErrorAction Stop | Sort-Object Name | ForEach-Object {
                [ordered]@{ name = $_.Name; switchName = $_.SwitchName; macAddress = $_.MacAddress }
            })
        $allVms = @(Get-VM -ErrorAction Stop)
        $vms = @($allVms | Where-Object { $VMName -contains $_.Name } | Sort-Object Name | ForEach-Object {
                $vm = $_
                $fw = if ($vm.Generation -eq 2) { Get-VMFirmware -VM $vm } else { $null }
                [ordered]@{
                    name = $vm.Name; id = [string]$vm.Id; state = [string]$vm.State; generation = $vm.Generation; version = [string]$vm.Version
                    notes = $vm.Notes; processorCount = $vm.ProcessorCount
                    memoryStartupBytes = $vm.MemoryStartup; dynamicMemoryEnabled = $vm.DynamicMemoryEnabled
                    automaticCheckpointsEnabled = $vm.AutomaticCheckpointsEnabled; checkpointType = [string]$vm.CheckpointType
                    secureBoot = if ($fw) { [string]$fw.SecureBoot } else { $null }
                    secureBootTemplate = if ($fw) { $fw.SecureBootTemplate } else { $null }
                    integrationServices = @(Get-VMIntegrationService -VM $vm | Sort-Object Name | ForEach-Object { [ordered]@{ name = $_.Name; enabled = $_.Enabled } })
                    hardDrives = @(Get-VMHardDiskDrive -VM $vm | Sort-Object Path | ForEach-Object {
                            $vhd = Get-VHD -Path $_.Path -ErrorAction SilentlyContinue
                            [ordered]@{ path = $_.Path; controller = "$($_.ControllerType)$($_.ControllerNumber):$($_.ControllerLocation)"
                                vhdType = if ($vhd) { [string]$vhd.VhdType } else { $null }; parentPath = if ($vhd) { $vhd.ParentPath } else { $null } }
                        })
                    networkAdapters = @(Get-VMNetworkAdapter -VM $vm | Sort-Object Name | ForEach-Object {
                            $vlan = Get-VMNetworkAdapterVlan -VMNetworkAdapter $_
                            [ordered]@{
                                name = $_.Name; macAddress = $_.MacAddress; dynamicMacAddressEnabled = $_.DynamicMacAddressEnabled
                                switchName = $_.SwitchName; switchId = [string]$_.SwitchId; deviceNaming = [string]$_.DeviceNaming
                                vlanMode = [string]$vlan.OperationMode; accessVlanId = $vlan.AccessVlanId
                                bandwidthMinimum = if ($_.BandwidthSetting) { $_.BandwidthSetting.MinimumBandwidthAbsolute } else { $null }
                                bandwidthMaximum = if ($_.BandwidthSetting) { $_.BandwidthSetting.MaximumBandwidth } else { $null }
                            }
                        })
                    checkpoints = @(Get-VMSnapshot -VM $vm | Sort-Object CreationTime | ForEach-Object {
                            [ordered]@{ name = $_.Name; id = [string]$_.Id; creationUtc = $_.CreationTime.ToUniversalTime().ToString('o') }
                        })
                }
            })
        $allNames = @($allVms | ForEach-Object { $_.Name })
        return [ordered]@{
            status = 'ok'; source = 'Hyper-V module'; capturedUtc = $captured
            data = [ordered]@{
                host = [ordered]@{ name = $vmHost.Name; virtualMachinePath = $vmHost.VirtualMachinePath; virtualHardDiskPath = $vmHost.VirtualHardDiskPath }
                switches = $switches
                hostVnics = $hostVnics
                vms = $vms
                missingVms = @($VMName | Where-Object { $allNames -notcontains $_ } | Sort-Object)
                otherVmNames = @($allNames | Where-Object { $VMName -notcontains $_ } | Sort-Object)
            }
        }
    }
    catch {
        return [ordered]@{ status = 'unavailable'; source = 'Hyper-V module'; capturedUtc = $captured; reason = $_.Exception.Message; data = $null }
    }
}
