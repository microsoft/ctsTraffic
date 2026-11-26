#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Unit tests for src\Provision.ps1.
# - Get-CtsProvisionPlan is pure; each case builds a synthetic Hyper-V state.
# - Journal and Apply tests use a temporary lab root; Hyper-V cmdlets are mocked, so nothing is created.

BeforeDiscovery {
    $script:HasHyperV = [bool](Get-Command -Name New-VMSwitch -ErrorAction SilentlyContinue)
}

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Def = Get-CtsLabDefinition
    $script:Top = Get-CtsLabTopology -Definition $script:Def -VmCount 2
    $script:Credential = [pscredential]::new('ctsadmin', (ConvertTo-SecureString 'unused-Pw1' -AsPlainText -Force))

    function New-State {
        param($Switches = @(), $Vms = @(), $HostAddresses = @(), $Files = @(), $Journal = @(), [bool]$Golden = $true, [bool]$Metadata = $Golden)
        [pscustomobject]@{
            Switches = @($Switches); Vms = @($Vms); HostAddresses = @($HostAddresses); ExistingFiles = @($Files)
            Journal = [pscustomobject]@{ schemaVersion = 1; labId = 'ctsrel-v1'; resources = @($Journal) }
            GoldenImage = [pscustomobject]@{ Path = 'C:\ctsReliability\vhdx\golden-win11-arm64.vhdx'; Exists = $Golden; MetadataExists = $Metadata }
        }
    }
    function Get-DiskPath($vmName) { Join-Path $script:Def.labRoot "vhdx\vms\$vmName.vhdx" }
    function New-LiveVm($vm, [string]$Id = [guid]::NewGuid().ToString()) {
        [pscustomobject]@{
            Name = $vm.Name; Id = $Id; State = 'Running'; Notes = ''; Generation = 2; ProcessorCount = 4; MemoryStartupBytes = 8GB
            DynamicMemoryEnabled = $false; AutomaticCheckpointsEnabled = $false; Disks = @(Get-DiskPath $vm.Name); Checkpoints = @()
            Adapters = @(@($vm.MgmtNic) + @($vm.TestNics) | ForEach-Object { [pscustomobject]@{ Name = $_.Name; SwitchName = $_.SwitchName; MacAddress = $_.MacAddress; DynamicMacAddressEnabled = $false; DeviceNaming = 'On' } })
        }
    }
    # A fully provisioned, fully owned two-VM lab.
    function New-OwnedState {
        $switches = @([pscustomobject]@{ Name = 'ctsRel-Mgmt'; Id = [guid]::NewGuid().ToString(); SwitchType = 'Internal'; Notes = '' }) +
        @($script:Top.Links | ForEach-Object { [pscustomobject]@{ Name = $_.SwitchName; Id = [guid]::NewGuid().ToString(); SwitchType = 'Private'; Notes = '' } })
        $vms = @($script:Top.Vms | ForEach-Object { New-LiveVm $_ })
        $journal = @($switches | ForEach-Object { [pscustomobject]@{ kind = 'Switch'; name = $_.Name; id = $_.Id } }) +
        @($vms | ForEach-Object { [pscustomobject]@{ kind = 'VM'; name = $_.Name; id = $_.Id }; [pscustomobject]@{ kind = 'Disk'; name = (Get-DiskPath $_.Name); id = (Get-DiskPath $_.Name) } })
        New-State -Switches $switches -Vms $vms -Journal $journal -Files @($vms | ForEach-Object { Get-DiskPath $_.Name }) `
            -HostAddresses @([pscustomobject]@{ InterfaceAlias = 'vEthernet (ctsRel-Mgmt)'; IPAddress = '192.168.250.254'; PrefixLength = 24 })
    }
    function Get-Plan($State, $Topology = $script:Top) { Get-CtsProvisionPlan -Definition $script:Def -Topology $Topology -State $State }
    function Get-PlanItem($Plan, $Kind, $Name) { @($Plan.Items | Where-Object { $_.Kind -eq $Kind -and $_.Name -eq $Name })[0] }
}

Describe 'Get-CtsProvisionPlan' {
    It 'plans every resource as Create for an empty host, in dependency order' {
        $plan = Get-Plan (New-State)
        $plan.Blocked | Should -BeFalse
        $plan.Counts.Create | Should -Be 7
        @($plan.Items | ForEach-Object Kind) | Should -Be @('Switch', 'Switch', 'HostAddress', 'Disk', 'VM', 'Disk', 'VM')
        (Get-PlanItem $plan 'Switch' 'ctsRel-Mgmt').Desired.SwitchType | Should -Be 'Internal'
        (Get-PlanItem $plan 'Switch' 'ctsRel-Private-AB').Desired.SwitchType | Should -Be 'Private'
    }

    It 'plans one Internal and three Private switches for three VMs' {
        $plan = Get-Plan (New-State) (Get-CtsLabTopology -Definition $script:Def -VmCount 3)
        @($plan.Items | Where-Object Kind -eq 'Switch').Count | Should -Be 4
        @($plan.Items | Where-Object Kind -eq 'VM').Count | Should -Be 3
    }

    It 'blocks when disks must be created but the golden image is missing' {
        $plan = Get-Plan (New-State -Golden $false)
        $plan.Blocked | Should -BeTrue
        $plan.Reasons -join ' ' | Should -Match 'New-CtsGoldenImage'
    }

    It 'blocks when the golden image has no recorded metadata' {
        (Get-Plan (New-State -Golden $true -Metadata $false)).Reasons -join ' ' | Should -Match 'metadata is missing'
    }

    It 'keeps everything for a matching, owned lab and does not need the golden image' {
        $state = New-OwnedState
        $state.GoldenImage.Exists = $false
        $plan = Get-Plan $state
        $plan.Blocked | Should -BeFalse
        $plan.Counts.Keep | Should -Be 7
        $plan.Counts.Create | Should -Be 0
    }

    It 'keeps an owned VM whose checkpoint disk descends from its recorded disk' {
        $state = New-OwnedState
        $state.Vms[0].Disks = @('C:\checkpoint.avhdx')
        $state.Vms[0] | Add-Member -NotePropertyName DiskAncestors -NotePropertyValue @((Get-DiskPath 'ctsRel-A'))
        $plan = Get-Plan $state
        $plan.Blocked | Should -BeFalse
        $plan.Counts.Keep | Should -Be 7
    }

    It 'blocks a checkpoint disk whose ancestry does not include the owned VM disk' {
        $state = New-OwnedState
        $state.Vms[0].Disks = @('C:\checkpoint.avhdx')
        $state.Vms[0] | Add-Member -NotePropertyName DiskAncestors -NotePropertyValue @('C:\other.vhdx')
        $plan = Get-Plan $state
        $plan.Blocked | Should -BeTrue
        (Get-PlanItem $plan 'VM' 'ctsRel-A').Detail | Should -Match 'not attached'
    }

    It 'reports a same-named switch that is not in the journal as a Collision' {
        $state = New-State -Switches @([pscustomobject]@{ Name = 'ctsRel-Mgmt'; Id = 'someone-else'; SwitchType = 'Internal' })
        $plan = Get-Plan $state
        (Get-PlanItem $plan 'Switch' 'ctsRel-Mgmt').Action | Should -Be 'Collision'
        $plan.Blocked | Should -BeTrue
    }

    It 'reports a Collision when the live ID differs from the journal (replaced object)' {
        $state = New-OwnedState
        $state.Vms[0].Id = [guid]::NewGuid().ToString()
        (Get-PlanItem (Get-Plan $state) 'VM' 'ctsRel-A').Action | Should -Be 'Collision'
    }

    It 'reports duplicate names as a Collision' {
        $state = New-OwnedState
        $state.Vms = @($state.Vms) + @(New-LiveVm $script:Top.Vms[0])
        (Get-PlanItem (Get-Plan $state) 'VM' 'ctsRel-A').Detail | Should -Match '2 objects'
    }

    It 'reports owned VM differences as Drift, lists them, and blocks without repairing' {
        $state = New-OwnedState
        $state.Vms[1].ProcessorCount = 2
        $state.Vms[1].DynamicMemoryEnabled = $true
        $state.Vms[1].Adapters[1].DynamicMacAddressEnabled = $true
        $plan = Get-Plan $state
        $item = Get-PlanItem $plan 'VM' 'ctsRel-B'
        $item.Action | Should -Be 'Drift'
        $item.Detail | Should -Match 'processorCount 2'
        $item.Detail | Should -Match 'dynamic memory'
        $item.Detail | Should -Match 'dynamic'
        $plan.Blocked | Should -BeTrue
    }

    It 'reports a switch of the wrong type as Drift' {
        $state = New-OwnedState
        $state.Switches[1].SwitchType = 'Internal'
        (Get-PlanItem (Get-Plan $state) 'Switch' 'ctsRel-Private-AB').Detail | Should -Match 'expected Private'
    }

    It 'recreates an owned resource that has disappeared and says so' {
        $state = New-OwnedState
        $state.Vms = @($state.Vms | Where-Object Name -ne 'ctsRel-B')
        $item = Get-PlanItem (Get-Plan $state) 'VM' 'ctsRel-B'
        $item.Action | Should -Be 'Create'
        $item.Detail | Should -Match 'recreated'
    }

    It 'reports an existing, unowned disk file as a Collision and never overwrites it' {
        $state = New-State -Files @(Get-DiskPath 'ctsRel-A')
        (Get-PlanItem (Get-Plan $state) 'Disk' (Get-DiskPath 'ctsRel-A')).Action | Should -Be 'Collision'
    }

    It 'blocks when another host adapter overlaps the management subnet, ignoring the lab vNIC itself' {
        $state = New-OwnedState
        (Get-Plan $state).Blocked | Should -BeFalse
        $state.HostAddresses = @($state.HostAddresses) + @([pscustomobject]@{ InterfaceAlias = 'Wi-Fi'; IPAddress = '192.168.250.7'; PrefixLength = 24 })
        (Get-Plan $state).Reasons -join ' ' | Should -Match "'Wi-Fi' overlaps"
    }

    It 'plans the host address when the owned switch exists but the address is missing' {
        $state = New-OwnedState
        $state.HostAddresses = @()
        (@((Get-Plan $state).Items | Where-Object Kind -eq 'HostAddress')[0]).Action | Should -Be 'Create'
    }
}

Describe 'Ownership journal' {
    BeforeEach {
        $script:JDef = Get-CtsLabDefinition
        $script:JDef.labRoot = Join-Path $TestDrive ([guid]::NewGuid().ToString('n'))
    }

    It 'returns an empty journal for a new lab' {
        @((Get-CtsOwnershipJournal -Definition $script:JDef).resources).Count | Should -Be 0
    }

    It 'adds records, replaces a record of the same kind and name, and leaves no temp file' {
        InModuleScope CtsReliability -Parameters @{ D = $script:JDef } {
            param($D)
            Add-CtsOwnershipRecord -Definition $D -Kind Switch -Name 'ctsRel-Mgmt' -Id 'one'
            Add-CtsOwnershipRecord -Definition $D -Kind VM -Name 'ctsRel-A' -Id 'vm'
            Add-CtsOwnershipRecord -Definition $D -Kind Switch -Name 'ctsRel-Mgmt' -Id 'two'
            $j = Get-CtsOwnershipJournal -Definition $D
            @($j.resources).Count | Should -Be 2
            @($j.resources | Where-Object kind -eq 'Switch')[0].id | Should -Be 'two'
            Test-Path -LiteralPath (Join-Path $D.labRoot 'lab\ownership.json.tmp') | Should -BeFalse
        }
    }

    It 'refuses a journal that belongs to another lab' {
        $dir = Join-Path $script:JDef.labRoot 'lab'
        $null = New-Item -ItemType Directory -Path $dir -Force
        '{"schemaVersion":1,"labId":"other-lab","resources":[]}' | Set-Content -LiteralPath (Join-Path $dir 'ownership.json')
        { Get-CtsOwnershipJournal -Definition $script:JDef } | Should -Throw "*belongs to lab 'other-lab'*"
    }
}

Describe 'Get-CtsHyperVState' -Skip:(-not $script:HasHyperV) {
    It 'turns a Hyper-V access failure into a clear rights message' {
        Mock -ModuleName CtsReliability Get-VMSwitch { throw 'You do not have the required permission' }
        { Get-CtsHyperVState -Definition $script:Def -Topology $script:Top } | Should -Throw '*Hyper-V Administrators*'
    }

    It 'captures lab-prefixed resources and disk ancestry without hiding the attached disk (<DiskPath>)' -TestCases @(
        @{ DiskPath = 'C:\d.vhdx'; Parents = @() }
        @{ DiskPath = 'C:\checkpoint2.avhdx'; Parents = @('C:\checkpoint1.avhdx', 'C:\d.vhdx') }
    ) {
        param($DiskPath, $Parents)
        Mock -ModuleName CtsReliability Get-VMSwitch { @([pscustomobject]@{ Name = 'ctsRel-Mgmt'; Id = [guid]::Empty; SwitchType = 'Internal'; Notes = 'n' }, [pscustomobject]@{ Name = 'Default Switch'; Id = [guid]::Empty; SwitchType = 'Internal'; Notes = '' }) }
        Mock -ModuleName CtsReliability Get-VM { @([pscustomobject]@{ Name = 'ctsRel-A'; Id = [guid]::Empty; State = 'Off'; Notes = ''; Generation = 2; ProcessorCount = 4; MemoryStartup = 8GB; DynamicMemoryEnabled = $false; AutomaticCheckpointsEnabled = $false }, [pscustomobject]@{ Name = 'Unrelated'; Id = [guid]::Empty }) }
        Mock -ModuleName CtsReliability Get-VMHardDiskDrive { [pscustomobject]@{ Path = $DiskPath } } -RemoveParameterType VM
        Mock -ModuleName CtsReliability Get-VHD {
            [pscustomobject]@{ ParentPath = if ($Path -eq 'C:\checkpoint2.avhdx') { 'C:\checkpoint1.avhdx' } else { 'C:\d.vhdx' } }
        }
        Mock -ModuleName CtsReliability Get-VMSnapshot { } -RemoveParameterType VM
        Mock -ModuleName CtsReliability Get-VMNetworkAdapter { [pscustomobject]@{ Name = 'NIC-Mgmt'; SwitchName = 'ctsRel-Mgmt'; MacAddress = '00155DCE01FE'; DynamicMacAddressEnabled = $false; DeviceNaming = 'On' } } -RemoveParameterType VM
        $s = Get-CtsHyperVState -Definition $script:Def -Topology $script:Top
        @($s.Switches).Name | Should -Be @('ctsRel-Mgmt')
        @($s.Vms).Name | Should -Be @('ctsRel-A')
        $s.Vms[0].Disks | Should -Be @($DiskPath)
        @($s.Vms[0].DiskAncestors).Count | Should -Be @($Parents).Count
        if ($Parents.Count -gt 0) { $s.Vms[0].DiskAncestors | Should -Be $Parents }
        $s.Vms[0].Adapters[0].MacAddress | Should -Be '00155DCE01FE'
    }
}

Describe 'Invoke-CtsLabProvision' -Skip:(-not $script:HasHyperV) {
    BeforeEach {
        $script:PDef = Get-CtsLabDefinition
        $script:PDef.labRoot = Join-Path $TestDrive ([guid]::NewGuid().ToString('n'))
        Mock -ModuleName CtsReliability Get-CtsHyperVState { New-State }
        Mock -ModuleName CtsReliability Test-CtsElevated { $true }
        Mock -ModuleName CtsReliability Get-CtsStoredCredential { $script:Credential }
        Mock -ModuleName CtsReliability New-VMSwitch { [pscustomobject]@{ Id = [guid]::NewGuid() } }
        Mock -ModuleName CtsReliability New-NetIPAddress { }
        Mock -ModuleName CtsReliability New-VHD { }
        Mock -ModuleName CtsReliability Set-CtsVhdUnattend { }
        Mock -ModuleName CtsReliability New-VM { [pscustomobject]@{ Id = [guid]::NewGuid(); Name = $Name } }
        Mock -ModuleName CtsReliability Set-VM { } -RemoveParameterType VM
        Mock -ModuleName CtsReliability Set-VMFirmware { } -RemoveParameterType VM
        Mock -ModuleName CtsReliability Get-VMNetworkAdapter { [pscustomobject]@{ Name = 'Network Adapter' } } -RemoveParameterType VM
        Mock -ModuleName CtsReliability Rename-VMNetworkAdapter { } -RemoveParameterType VMNetworkAdapter
        Mock -ModuleName CtsReliability Set-VMNetworkAdapter { } -RemoveParameterType VMNetworkAdapter
        Mock -ModuleName CtsReliability Add-VMNetworkAdapter { } -RemoveParameterType VM
    }

    It 'returns the plan and changes nothing without -Apply' {
        $plan = Invoke-CtsLabProvision -Definition $script:PDef
        $plan.Counts.Create | Should -Be 7
        Should -Invoke -ModuleName CtsReliability New-VMSwitch -Times 0
        Test-Path -LiteralPath $script:PDef.labRoot | Should -BeFalse
    }

    It 'refuses to apply a blocked plan' {
        Mock -ModuleName CtsReliability Get-CtsHyperVState { New-State -Golden $false }
        { Invoke-CtsLabProvision -Definition $script:PDef -Apply } | Should -Throw '*blocked*'
        Should -Invoke -ModuleName CtsReliability New-VMSwitch -Times 0
    }

    It 'refuses to apply when not elevated' {
        Mock -ModuleName CtsReliability Test-CtsElevated { $false }
        { Invoke-CtsLabProvision -Definition $script:PDef -Apply } | Should -Throw '*elevated*'
    }

    It 'creates switches, host address, differencing disks with unattend, and VMs with static MACs, journaling each' {
        $r = Invoke-CtsLabProvision -Definition $script:PDef -Apply -SkipGuestConfiguration
        @($r.Results | Where-Object { -not $_.success }).Count | Should -Be 0
        Should -Invoke -ModuleName CtsReliability New-VMSwitch -Times 1 -Exactly -ParameterFilter { $Name -eq 'ctsRel-Mgmt' -and $SwitchType -eq 'Internal' }
        Should -Invoke -ModuleName CtsReliability New-VMSwitch -Times 1 -Exactly -ParameterFilter { $Name -eq 'ctsRel-Private-AB' -and $SwitchType -eq 'Private' }
        Should -Invoke -ModuleName CtsReliability New-NetIPAddress -Times 1 -Exactly -ParameterFilter { $IPAddress -eq '192.168.250.254' -and $InterfaceAlias -eq 'vEthernet (ctsRel-Mgmt)' }
        Should -Invoke -ModuleName CtsReliability New-VHD -Times 2 -Exactly -ParameterFilter { $Differencing -and $ParentPath -like "*golden-win11-$(Get-CtsHostArchitecture).vhdx" }
        Should -Invoke -ModuleName CtsReliability Set-CtsVhdUnattend -Times 1 -Exactly -ParameterFilter { $UnattendXml -match '<ComputerName>CTSREL-A</ComputerName>' -and $UnattendXml -notmatch 'unused-Pw1' }
        Should -Invoke -ModuleName CtsReliability New-VM -Times 2 -Exactly -ParameterFilter { $SwitchName -eq 'ctsRel-Mgmt' -and $MemoryStartupBytes -eq 8GB }
        Should -Invoke -ModuleName CtsReliability Set-VM -Times 2 -Exactly -ParameterFilter { $StaticMemory -and $AutomaticCheckpointsEnabled -eq $false -and $ProcessorCount -eq 4 }
        Should -Invoke -ModuleName CtsReliability Set-VMNetworkAdapter -Times 1 -Exactly -ParameterFilter { $StaticMacAddress -eq '00155DCE01FE' -and $DeviceNaming -eq 'On' }
        Should -Invoke -ModuleName CtsReliability Add-VMNetworkAdapter -Times 1 -Exactly -ParameterFilter { $Name -eq 'NIC-BA' -and $StaticMacAddress -eq '00155DCE0201' -and $SwitchName -eq 'ctsRel-Private-AB' }
        $journal = Get-CtsOwnershipJournal -Definition $script:PDef
        @($journal.resources | Group-Object kind | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Count)" }) | Should -Be @('Disk=2', 'HostAddress=1', 'Switch=2', 'VM=2')
        foreach ($f in 'plan.json', 'state-before.json', 'results.json') { Test-Path -LiteralPath (Join-Path $r.Evidence $f) | Should -BeTrue }
    }

    It 'stops at the first failure, keeps what it created in the journal, and records the failed step' {
        Mock -ModuleName CtsReliability New-VM { if ($Name -eq 'ctsRel-B') { throw 'simulated New-VM failure' }; [pscustomobject]@{ Id = [guid]::NewGuid(); Name = $Name } }
        { Invoke-CtsLabProvision -Definition $script:PDef -Apply -SkipGuestConfiguration } | Should -Throw "*CreateVM:ctsRel-B*simulated New-VM failure*rerun to continue*"
        $journal = Get-CtsOwnershipJournal -Definition $script:PDef
        @($journal.resources | Where-Object kind -eq 'VM').name | Should -Be @('ctsRel-A')
        $evidence = @(Get-ChildItem -LiteralPath (Join-Path $script:PDef.labRoot 'lab') -Directory -Filter 'provision-*')[0].FullName
        $results = Get-Content -LiteralPath (Join-Path $evidence 'results.json') -Raw | ConvertFrom-Json
        @($results)[-1].step | Should -Be 'CreateVM:ctsRel-B'
        @($results)[-1].success | Should -BeFalse
    }
}
