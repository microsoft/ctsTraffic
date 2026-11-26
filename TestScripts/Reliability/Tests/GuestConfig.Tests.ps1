#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Unit tests for src\GuestConfig.ps1.
# - The desired-configuration function is pure and tested directly.
# - The guest script is run from the test scope with only one section in the config at a time; network,
#   firewall, and Defender cmdlets are mocked, and registry sections point at a temporary HKCU key, so the
#   apply path never changes this machine.
# - The 'Live' dry run reads the real machine and must change nothing.

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Def = Get-CtsLabDefinition
    $script:Top2 = Get-CtsLabTopology -Definition $script:Def -VmCount 2
    $script:Top3 = Get-CtsLabTopology -Definition $script:Def -VmCount 3
    $script:GuestScript = [scriptblock]::Create((Get-CtsGuestConfigScript))
    function Invoke-Script($Config, [bool]$DryRun) { & $script:GuestScript -Config ([pscustomobject]$Config) -DryRun $DryRun }
    function Get-Item1($Result, $Name) { @($Result.items | Where-Object item -eq $Name)[0] }
}

Describe 'Get-CtsGuestConfiguration' {
    It 'gives VM-A in a two-VM lab one management and one test adapter with static MACs in guest format' {
        $c = Get-CtsGuestConfiguration -Definition $script:Def -Topology $script:Top2 -VmName 'ctsRel-A'
        @($c.adapters).Count | Should -Be 2
        $c.adapters[0].name | Should -Be 'NIC-Mgmt'
        $c.adapters[0].mac | Should -Be '00-15-5D-CE-01-FE'
        $c.adapters[0].ipv4 | Should -Be '192.168.250.1'
        $c.adapters[0].ipv6Enabled | Should -BeFalse
        $c.adapters[1].name | Should -Be 'NIC-AB'
        $c.adapters[1].ipv6 | Should -Be 'fd00:1::1'
        $c.adapters[1].ipv6Enabled | Should -BeTrue
    }

    It 'scopes the ctsTraffic rule to the staged program, port, test adapters, and test subnets' {
        $c = Get-CtsGuestConfiguration -Definition $script:Def -Topology $script:Top3 -VmName 'ctsRel-A'
        $tcp = @($c.firewall.rules | Where-Object name -eq 'ctsRel-ctsTraffic-TCP')[0]
        $tcp.group | Should -Be 'ctsTraffic Reliability'
        $tcp.program | Should -Be 'C:\ctsReliability\tools\ctsTraffic.exe'
        $tcp.localPort | Should -Be '4444'
        $tcp.interfaceAlias | Should -Be @('NIC-AB', 'NIC-AC')
        $tcp.remoteAddress | Should -Be @('192.168.1.0/24', '192.168.2.0/24', 'fd00:1::/64', 'fd00:2::/64')
    }

    It 'splits ICMP rules by family and limits WinRM to the management adapter and subnet' {
        $c = Get-CtsGuestConfiguration -Definition $script:Def -Topology $script:Top2 -VmName 'ctsRel-B'
        $rules = @{}; foreach ($r in $c.firewall.rules) { $rules[$r.name] = $r }
        $rules['ctsRel-ICMPv4-Echo'].remoteAddress | Should -Be @('192.168.1.0/24')
        $rules['ctsRel-ICMPv6-Echo'].remoteAddress | Should -Be @('fd00:1::/64')
        $rules['ctsRel-WinRM-HTTP'].interfaceAlias | Should -Be @('NIC-Mgmt')
        $rules['ctsRel-WinRM-HTTP'].remoteAddress | Should -Be @('192.168.250.0/24')
        $rules['ctsRel-WinRM-HTTP'].localPort | Should -Be '5985'
    }

    It 'sizes the page file for a complete dump (memory + 1 GiB) and enables complete dumps with NMI' {
        $c = Get-CtsGuestConfiguration -Definition $script:Def -Topology $script:Top2 -VmName 'ctsRel-A'
        $c.pageFile.initialMB | Should -Be 9216
        $c.pageFile.maximumMB | Should -Be 9216
        $c.crashControl.values.CrashDumpEnabled | Should -Be 1
        $c.crashControl.values.NMICrashDump | Should -Be 1
    }

    It 'round-trips through JSON (it is sent to the guest and saved as evidence)' {
        $c = Get-CtsGuestConfiguration -Definition $script:Def -Topology $script:Top2 -VmName 'ctsRel-A'
        $j = $c | ConvertTo-Json -Depth 8 | ConvertFrom-Json
        $j.adapters[1].mac | Should -Be '00-15-5D-CE-01-01'
        @($j.firewall.rules).Count | Should -Be 4
    }

    It 'throws for a VM that is not in the topology' {
        { Get-CtsGuestConfiguration -Definition $script:Def -Topology $script:Top2 -VmName 'ctsRel-C' } | Should -Throw '*not in the topology*'
    }
}

Describe 'Guest script: general behavior' {
    It 'defaults to a dry run when -DryRun is not given' {
        $dir = Join-Path $TestDrive 'default-dry'
        $r = & $script:GuestScript -Config ([pscustomobject]@{ directories = @($dir) })
        $r.dryRun | Should -BeTrue
        (Get-Item1 $r 'Directories').status | Should -Be 'WouldChange'
        Test-Path -LiteralPath $dir | Should -BeFalse
    }

    It 'skips absent sections, so an empty config yields no items' {
        $r = Invoke-Script @{} $false
        @($r.items).Count | Should -Be 0
    }

    It 'applies, then reports Compliant on the next run (directories)' {
        $dir = Join-Path $TestDrive 'apply\nested'
        (Get-Item1 (Invoke-Script @{ directories = @($dir) } $false) 'Directories').status | Should -Be 'Changed'
        Test-Path -LiteralPath $dir | Should -BeTrue
        (Get-Item1 (Invoke-Script @{ directories = @($dir) } $false) 'Directories').status | Should -Be 'Compliant'
    }
}

Describe 'Guest script: registry sections (HKCU test key)' {
    BeforeAll { $script:Key = "HKCU:\Software\ctsReliabilityPester-$([guid]::NewGuid().ToString('n'))" }
    AfterAll { Remove-Item -LiteralPath $script:Key -Recurse -Force -ErrorAction SilentlyContinue }

    It 'writes crash-control DWORDs, flags a reboot, and is compliant afterwards' {
        $cfg = @{ crashControl = @{ registryPath = "$script:Key\Crash"; values = [ordered]@{ CrashDumpEnabled = 1; NMICrashDump = 1 } } }
        $dry = Get-Item1 (Invoke-Script $cfg $true) 'CrashControl'
        $dry.status | Should -Be 'WouldChange'
        Test-Path -LiteralPath "$script:Key\Crash" | Should -BeFalse
        $applied = Get-Item1 (Invoke-Script $cfg $false) 'CrashControl'
        $applied.status | Should -Be 'Changed'
        $applied.rebootRequired | Should -BeTrue
        (Get-ItemProperty -LiteralPath "$script:Key\Crash").NMICrashDump | Should -Be 1
        (Get-Item -LiteralPath "$script:Key\Crash").GetValueKind('NMICrashDump') | Should -Be 'DWord'
        (Get-Item1 (Invoke-Script $cfg $false) 'CrashControl').status | Should -Be 'Compliant'
    }

    It 'writes WER LocalDumps with an expandable-string folder' {
        $cfg = @{ werLocalDumps = @(@{ registryPath = "$script:Key\Wer\ctsTraffic.exe"; image = 'ctsTraffic.exe'; folder = 'C:\ctsReliability\dumps'; dumpType = 2; dumpCount = 10 }) }
        (Get-Item1 (Invoke-Script $cfg $false) 'WerLocalDumps:ctsTraffic.exe').status | Should -Be 'Changed'
        $k = Get-Item -LiteralPath "$script:Key\Wer\ctsTraffic.exe"
        $k.GetValueKind('DumpFolder') | Should -Be 'ExpandString'
        $k.GetValue('DumpType') | Should -Be 2
    }
}

Describe 'Guest script: adapters (mocked)' {
    BeforeEach {
        $script:Nic = [pscustomobject]@{ Name = 'Ethernet 2'; ifIndex = 7; MacAddress = '00-15-5D-CE-01-01' }
        $script:S = @{ Dhcp = 'Enabled'; V4 = @(); V6 = @(); Register = $true; V6Bound = $true }
        $script:Adapter = [pscustomobject]@{ name = 'NIC-AB'; role = 'Test'; mac = '00-15-5D-CE-01-01'; ipv4 = '192.168.1.1'; ipv4Prefix = 24; ipv6 = 'fd00:1::1'; ipv6Prefix = 64; ipv6Enabled = $true }
        Mock Get-NetAdapter { $script:Nic }
        Mock Get-NetIPAddress { if ($AddressFamily -eq 'IPv4') { $script:S.V4 } else { $script:S.V6 } }
        Mock Get-NetIPInterface { [pscustomobject]@{ Dhcp = $script:S.Dhcp } }
        Mock Get-DnsClient { [pscustomobject]@{ RegisterThisConnectionsAddress = $script:S.Register } }
        Mock Get-NetAdapterBinding { [pscustomobject]@{ Enabled = $script:S.V6Bound } }
        Mock Rename-NetAdapter { $script:Nic.Name = $NewName }
        Mock Set-NetIPInterface { $script:S.Dhcp = [string]$Dhcp }
        Mock Set-DnsClient { $script:S.Register = $RegisterThisConnectionsAddress }
        Mock Enable-NetAdapterBinding { $script:S.V6Bound = $true }
        Mock Disable-NetAdapterBinding { $script:S.V6Bound = $false }
        Mock Remove-NetIPAddress { $script:S.V4 = @($script:S.V4 | Where-Object IPAddress -ne $IPAddress) }
        Mock New-NetIPAddress {
            $o = [pscustomobject]@{ IPAddress = $IPAddress; PrefixLength = $PrefixLength; PrefixOrigin = 'Manual' }
            if ($IPAddress -like '*:*') { $script:S.V6 = @($script:S.V6) + $o } else { $script:S.V4 = @($script:S.V4) + $o }
        }
    }

    It 'renames by MAC, disables DHCP and DNS registration, removes stray addresses, and adds the static ones' {
        $script:S.V4 = @([pscustomobject]@{ IPAddress = '10.9.9.9'; PrefixLength = 8; PrefixOrigin = 'Manual' })
        $r = Get-Item1 (Invoke-Script @{ adapters = @($script:Adapter) } $false) 'Adapter:NIC-AB'
        $r.status | Should -Be 'Changed'
        $script:Nic.Name | Should -Be 'NIC-AB'
        Should -Invoke Remove-NetIPAddress -Times 1 -Exactly -ParameterFilter { $IPAddress -eq '10.9.9.9' }
        Should -Invoke New-NetIPAddress -Times 2 -Exactly
        $r.after.ipv4 | Should -Be @('192.168.1.1/24')
        $r.after.ipv6 | Should -Be @('fd00:1::1/64')
    }

    It 'disables IPv6 on the management adapter' {
        $mgmt = [pscustomobject]@{ name = 'NIC-Mgmt'; role = 'Management'; mac = '00-15-5D-CE-01-01'; ipv4 = '192.168.250.1'; ipv4Prefix = 24; ipv6 = $null; ipv6Prefix = $null; ipv6Enabled = $false }
        (Get-Item1 (Invoke-Script @{ adapters = @($mgmt) } $false) 'Adapter:NIC-Mgmt').status | Should -Be 'Changed'
        Should -Invoke Disable-NetAdapterBinding -Times 1 -Exactly
        Should -Invoke New-NetIPAddress -Times 1 -Exactly
    }

    It 'is Compliant and changes nothing when the adapter already matches' {
        $script:Nic.Name = 'NIC-AB'
        $script:S = @{ Dhcp = 'Disabled'; Register = $false; V6Bound = $true
            V4 = @([pscustomobject]@{ IPAddress = '192.168.1.1'; PrefixLength = 24 })
            V6 = @([pscustomobject]@{ IPAddress = 'fd00:1::1'; PrefixLength = 64; PrefixOrigin = 'Manual' }) }
        (Get-Item1 (Invoke-Script @{ adapters = @($script:Adapter) } $false) 'Adapter:NIC-AB').status | Should -Be 'Compliant'
        Should -Invoke New-NetIPAddress -Times 0
        Should -Invoke Rename-NetAdapter -Times 0
    }

    It 'fails the item when no adapter has the MAC' {
        Mock Get-NetAdapter { @() }
        $r = Get-Item1 (Invoke-Script @{ adapters = @($script:Adapter) } $false) 'Adapter:NIC-AB'
        $r.status | Should -Be 'Failed'
        $r.error | Should -Match 'found 0'
    }

    It 'changes nothing in a dry run' {
        (Get-Item1 (Invoke-Script @{ adapters = @($script:Adapter) } $true) 'Adapter:NIC-AB').status | Should -Be 'WouldChange'
        Should -Invoke Rename-NetAdapter -Times 0
        Should -Invoke New-NetIPAddress -Times 0
    }
}

Describe 'Guest script: firewall rules (mocked)' {
    BeforeAll {
        $script:Rule = [pscustomobject]@{ name = 'ctsRel-ctsTraffic-TCP'; displayName = 'ctsTraffic TCP'; group = 'ctsTraffic Reliability'; direction = 'Inbound'; action = 'Allow'; profile = 'Any'
            protocol = 'TCP'; localPort = '4444'; icmpType = $null; program = 'C:\ctsReliability\tools\ctsTraffic.exe'
            interfaceAlias = @('NIC-AB'); remoteAddress = @('192.168.1.0/24', 'fd00:1::/64') }
        $script:Cfg = @{ firewall = @{ rules = @($script:Rule); disableBuiltinWinRMRules = $false } }
    }
    BeforeEach {
        $script:Fw = $null
        Mock Get-NetFirewallRule { $script:Fw }
        Mock Get-NetFirewallPortFilter { [pscustomobject]@{ Protocol = 'TCP'; LocalPort = '4444'; IcmpType = 'Any' } } -RemoveParameterType AssociatedNetFirewallRule
        Mock Get-NetFirewallApplicationFilter { [pscustomobject]@{ Program = 'C:\ctsReliability\tools\ctsTraffic.exe' } } -RemoveParameterType AssociatedNetFirewallRule
        Mock Get-NetFirewallInterfaceFilter { [pscustomobject]@{ InterfaceAlias = @('NIC-AB') } } -RemoveParameterType AssociatedNetFirewallRule
        Mock Get-NetFirewallAddressFilter { [pscustomobject]@{ RemoteAddress = @('192.168.1.0/255.255.255.0', 'fd00:1::/64') } } -RemoveParameterType AssociatedNetFirewallRule
        Mock Remove-NetFirewallRule { $script:Fw = $null }
        Mock New-NetFirewallRule { $script:Fw = [pscustomobject]@{ Enabled = 'True'; Group = $Group; Direction = $Direction; Action = $Action; Profile = $Profile } }
    }

    It 'creates a missing rule with program, port, interface, and remote-address scope' {
        (Get-Item1 (Invoke-Script $script:Cfg $false) 'Firewall:ctsRel-ctsTraffic-TCP').status | Should -Be 'Changed'
        Should -Invoke New-NetFirewallRule -Times 1 -Exactly -ParameterFilter {
            $Program -eq 'C:\ctsReliability\tools\ctsTraffic.exe' -and ($LocalPort -join ',') -eq '4444' -and $Protocol -eq 'TCP' -and
            @($InterfaceAlias).Count -eq 1 -and ($RemoteAddress -join ',') -eq '192.168.1.0/24,fd00:1::/64' -and $Group -eq 'ctsTraffic Reliability'
        }
    }

    It 'treats an existing rule as Compliant when Windows reports the IPv4 subnet in mask form' {
        $script:Fw = [pscustomobject]@{ Enabled = 'True'; Group = 'ctsTraffic Reliability'; Direction = 'Inbound'; Action = 'Allow'; Profile = 'Any' }
        (Get-Item1 (Invoke-Script $script:Cfg $false) 'Firewall:ctsRel-ctsTraffic-TCP').status | Should -Be 'Compliant'
        Should -Invoke New-NetFirewallRule -Times 0
    }

    It 'replaces a rule whose scope drifted' {
        $script:Fw = [pscustomobject]@{ Enabled = 'True'; Group = 'ctsTraffic Reliability'; Direction = 'Inbound'; Action = 'Allow'; Profile = 'Any' }
        Mock Get-NetFirewallAddressFilter { [pscustomobject]@{ RemoteAddress = @('Any') } } -RemoveParameterType AssociatedNetFirewallRule
        $r = Get-Item1 (Invoke-Script $script:Cfg $false) 'Firewall:ctsRel-ctsTraffic-TCP'
        Should -Invoke Remove-NetFirewallRule -Times 1 -Exactly
        Should -Invoke New-NetFirewallRule -Times 1 -Exactly
        $r.status | Should -Be 'Failed' -Because 'the mocked address filter still reports Any after the re-create'
    }

    It 'disables enabled built-in WinRM rules' {
        Mock Get-NetFirewallRule { [pscustomobject]@{ Name = 'WINRM-HTTP-In-TCP'; Enabled = 'True' } } -ParameterFilter { $Group -eq '@FirewallAPI.dll,-30267' }
        Mock Disable-NetFirewallRule { }
        $r = Get-Item1 (Invoke-Script @{ firewall = @{ rules = @(); disableBuiltinWinRMRules = $true } } $false) 'Firewall:BuiltinWinRMRulesDisabled'
        Should -Invoke Disable-NetFirewallRule -Times 1 -Exactly -ParameterFilter { $Name -eq 'WINRM-HTTP-In-TCP' }
        $r.status | Should -Be 'Failed' -Because 'the mock keeps reporting the rule as enabled'
    }
}

Describe 'Guest script: WinRM (mocked)' {
    BeforeEach {
        $script:Winrm = @{ Status = 'Running'; StartType = 'Automatic'; DelayedAutoStart = 1 }
        Mock Get-Service { [pscustomobject]@{ Status = $script:Winrm.Status; StartType = $script:Winrm.StartType } }
        Mock Get-ItemProperty { [pscustomobject]@{ DelayedAutoStart = $script:Winrm.DelayedAutoStart } }
        Mock Get-ChildItem { [pscustomobject]@{ Name = 'Listener' } }
        Mock Enable-PSRemoting { }
        Mock Set-Service { $script:Winrm.StartType = [string]$StartupType }
        Mock Test-Path { $true }
        Mock New-ItemProperty { $script:Winrm.DelayedAutoStart = $Value }
        Mock Start-Service { $script:Winrm.Status = 'Running' }
    }

    It 'disables delayed startup and starts WinRM so post-reboot verification need not wait for delayed services' {
        $script:Winrm.Status = 'Stopped'
        $cfg = @{ winrm = @{ enable = $true } }
        $r = Get-Item1 (Invoke-Script $cfg $false) 'WinRM'
        $r.status | Should -Be 'Changed'
        $r.after.delayedAutoStart | Should -Be 0
        Should -Invoke Start-Service -Times 1 -Exactly -ParameterFilter { $Name -eq 'WinRM' }
        (Get-Item1 (Invoke-Script $cfg $true) 'WinRM').status | Should -Be 'Compliant'
    }

    It 'reports delayed startup as drift without changing the service in a dry run' {
        (Get-Item1 (Invoke-Script @{ winrm = @{ enable = $true } } $true) 'WinRM').status | Should -Be 'WouldChange'
        Should -Invoke Set-Service -Times 0
        Should -Invoke New-ItemProperty -Times 0
        Should -Invoke Start-Service -Times 0
    }
}

Describe 'Guest script: page file (mocked)' {
    BeforeEach {
        $script:Computer = New-CimInstance -ClassName Win32_ComputerSystem -ClientOnly -Property @{ AutomaticManagedPagefile = $true }
        $script:PageFiles = @()
        $script:PageConfig = @{ pageFile = @{ path = 'C:\pagefile.sys'; initialMB = 9216; maximumMB = 9216 } }
        Mock Get-CimInstance {
            if ($ClassName -eq 'Win32_ComputerSystem') { $script:Computer } else { $script:PageFiles }
        }
        Mock Set-CimInstance { $script:Computer.AutomaticManagedPagefile = $false }
        Mock New-ItemProperty {
            $script:PageFiles = @([pscustomobject]@{ Name = 'C:\pagefile.sys'; InitialSize = 9216; MaximumSize = 9216 })
        }
    }

    It 'persists complete-dump sizing without using the failing PageFileSetting update provider' {
        $r = Get-Item1 (Invoke-Script $script:PageConfig $false) 'PageFile'
        $r.status | Should -Be 'Changed'
        $r.rebootRequired | Should -BeTrue
        Should -Invoke New-ItemProperty -Times 1 -Exactly -ParameterFilter {
            $LiteralPath -eq 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management' -and
            $Name -eq 'PagingFiles' -and $PropertyType -eq 'MultiString' -and
            ($Value -join ';') -eq 'C:\pagefile.sys 9216 9216'
        }
        Should -Invoke Set-CimInstance -Times 1 -Exactly -ParameterFilter { $Property.ContainsKey('AutomaticManagedPagefile') }
        (Get-Item1 (Invoke-Script $script:PageConfig $true) 'PageFile').status | Should -Be 'Compliant'
    }

    It 'does not change page-file settings in a dry run' {
        (Get-Item1 (Invoke-Script $script:PageConfig $true) 'PageFile').status | Should -Be 'WouldChange'
        Should -Invoke Set-CimInstance -Times 0
        Should -Invoke New-ItemProperty -Times 0
    }
}

Describe 'Guest script: Defender (mocked)' {
    It 'reports Tamper Protection as Blocked during verification without trying to disable protection' {
        Mock Get-MpComputerStatus { [pscustomobject]@{ RealTimeProtectionEnabled = $true; IsTamperProtected = $true } }
        Mock Set-MpPreference { }
        $r = Get-Item1 (Invoke-Script @{ defender = @{ realTimeProtection = $false } } $true) 'DefenderRealTimeProtection'
        $r.status | Should -Be 'Blocked'
        $r.error | Should -Match 'Tamper Protection'
        Should -Invoke Set-MpPreference -Times 0
    }

    It 'does not classify enabling protection as blocked by Tamper Protection' {
        Mock Get-MpComputerStatus { [pscustomobject]@{ RealTimeProtectionEnabled = $false; IsTamperProtected = $true } }
        Mock Set-MpPreference { }
        (Get-Item1 (Invoke-Script @{ defender = @{ realTimeProtection = $true } } $true) 'DefenderRealTimeProtection').status | Should -Be 'WouldChange'
        Should -Invoke Set-MpPreference -Times 0
    }

    It 'reports Blocked with the Tamper Protection hint when real-time protection stays on' {
        Mock Get-MpComputerStatus { [pscustomobject]@{ RealTimeProtectionEnabled = $true; IsTamperProtected = $true } }
        Mock Set-MpPreference { }
        $r = Get-Item1 (Invoke-Script @{ defender = @{ realTimeProtection = $false } } $false) 'DefenderRealTimeProtection'
        $r.status | Should -Be 'Blocked'
        $r.error | Should -Match 'Tamper Protection'
        Should -Invoke Set-MpPreference -Times 1 -Exactly -ParameterFilter { $DisableRealtimeMonitoring -eq $true }
    }

    It 'reports Changed when real-time protection turns off' {
        $script:Rtp = $true
        Mock Get-MpComputerStatus { [pscustomobject]@{ RealTimeProtectionEnabled = $script:Rtp; IsTamperProtected = $false } }
        Mock Set-MpPreference { $script:Rtp = $false }
        (Get-Item1 (Invoke-Script @{ defender = @{ realTimeProtection = $false } } $false) 'DefenderRealTimeProtection').status | Should -Be 'Changed'
    }
}

Describe 'Guest script: live dry run on this machine' -Tag 'Live' {
    It 'reads every section of a real VM configuration and changes nothing' {
        $c = Get-CtsGuestConfiguration -Definition $script:Def -Topology $script:Top2 -VmName 'ctsRel-A'
        $r = Invoke-CtsGuestConfiguration -Configuration $c
        $r.dryRun | Should -BeTrue
        @($r.items | Where-Object status -eq 'Changed').Count | Should -Be 0
        foreach ($blocked in @($r.items | Where-Object status -eq 'Blocked')) {
            $blocked.item | Should -Be 'DefenderRealTimeProtection'
            $blocked.before.tamperProtected | Should -BeTrue
        }
        @($r.items).Count | Should -Be ($r.counts.compliant + $r.counts.wouldChange + $r.counts.blocked + $r.counts.failed)
        @($r.items | ForEach-Object item) | Should -Contain 'PageFile'
        @($r.items | ForEach-Object item) | Should -Contain 'Firewall:ctsRel-ctsTraffic-TCP'
        foreach ($f in @($r.items | Where-Object status -eq 'Failed')) { $f.item | Should -BeLike 'Adapter:*' -Because 'only the lab MACs are absent on the host' }
    }
}
