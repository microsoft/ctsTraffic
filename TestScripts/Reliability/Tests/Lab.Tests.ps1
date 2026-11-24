#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Unit tests for src\Lab.ps1: lab definition validation, IPv4 helpers, and topology derivation.

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:LabJson = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\lab.v1.json') -Raw

    function New-TestDefinitionFile {
        param([scriptblock]$Mutate)
        $definition = $script:LabJson | ConvertFrom-Json
        & $Mutate $definition
        $path = Join-Path $TestDrive ("lab-{0}.json" -f [guid]::NewGuid())
        $definition | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
        return $path
    }
}

Describe 'Get-CtsLabDefinition' {
    It 'loads the checked-in lab.v1.json by default' {
        $d = Get-CtsLabDefinition
        $d.labId | Should -Be 'ctsrel-v1'
        $d.management.subnet | Should -Be '192.168.250.0/24'
    }

    It 'rejects <Case>' -ForEach @(
        @{ Case = 'a missing field'; Mutate = { param($d) $d.PSObject.Properties.Remove('guest') }; Message = "*missing field 'guest'*" }
        @{ Case = 'an unsupported schemaVersion'; Mutate = { param($d) $d.schemaVersion = 2 }; Message = '*unsupported schemaVersion*' }
        @{ Case = 'vm.count above 5'; Mutate = { param($d) $d.vm.count = 6 }; Message = '*vm.count must be 2 through 5*' }
        @{ Case = 'vm.count below 2'; Mutate = { param($d) $d.vm.count = 1 }; Message = '*vm.count must be 2 through 5*' }
        @{ Case = 'an unknown diskMode'; Mutate = { param($d) $d.vm.diskMode = 'Fixed' }; Message = '*diskMode*' }
        @{ Case = 'an invalid namePrefix'; Mutate = { param($d) $d.namePrefix = '1bad_name' }; Message = '*namePrefix*' }
        @{ Case = 'a malformed management subnet'; Mutate = { param($d) $d.management.subnet = '192.168.250.0' }; Message = '*management.subnet*' }
        @{ Case = 'a host address outside the subnet'; Mutate = { param($d) $d.management.hostAddress = '192.168.1.254' }; Message = '*hostAddress is not inside*' }
    ) {
        $path = New-TestDefinitionFile -Mutate $Mutate
        { Get-CtsLabDefinition -Path $path } | Should -Throw -ExpectedMessage $Message
    }
}

Describe 'IPv4 helpers' {
    It 'computes the network and mask for /<Prefix>' -ForEach @(
        @{ Cidr = '10.1.2.3/0'; Prefix = 0; Mask = [uint32]0; Network = [uint32]0 }
        @{ Cidr = '192.168.250.7/24'; Prefix = 24; Mask = [uint32]4294967040; Network = [uint32]3232299520 }
        @{ Cidr = '192.168.250.7/32'; Prefix = 32; Mask = [uint32]4294967295; Network = [uint32]3232299527 }
    ) {
        InModuleScope CtsReliability -Parameters @{ Cidr = $Cidr; Mask = $Mask; Network = $Network } {
            param($Cidr, $Mask, $Network)
            $n = ConvertTo-CtsIPv4Network $Cidr
            $n.Mask | Should -Be $Mask
            $n.Network | Should -Be $Network
        }
    }

    It 'rejects <Cidr>' -ForEach @(@{ Cidr = '192.168.1.0' }, @{ Cidr = 'fd00::/64' }, @{ Cidr = '192.168.1.0/33' }) {
        InModuleScope CtsReliability -Parameters @{ Cidr = $Cidr } { param($Cidr) { ConvertTo-CtsIPv4Network $Cidr } | Should -Throw }
    }

    It 'detects subnet overlap and ignores non-IPv4 entries' {
        $existing = @(
            [pscustomobject]@{ IPAddress = '192.168.1.80'; PrefixLength = 24 }
            [pscustomobject]@{ IPAddress = '172.24.208.1'; PrefixLength = 20 }
            [pscustomobject]@{ IPAddress = 'fe80::1'; PrefixLength = 64 }
        )
        @(Test-CtsSubnetOverlap -Cidr '192.168.250.0/24' -Existing $existing).Count | Should -Be 0
        @(Test-CtsSubnetOverlap -Cidr '192.168.0.0/16' -Existing $existing).IPAddress | Should -Be '192.168.1.80'
        @(Test-CtsSubnetOverlap -Cidr '172.24.210.0/24' -Existing $existing).IPAddress | Should -Be '172.24.208.1'
    }
}

Describe 'Get-CtsLabTopology' {
    BeforeAll { $script:Definition = Get-CtsLabDefinition }

    It 'derives <Links> links and <NicsPerVm> test NICs per VM for <Count> VMs' -ForEach @(
        @{ Count = 2; Links = 1; NicsPerVm = 1 }
        @{ Count = 3; Links = 3; NicsPerVm = 2 }
        @{ Count = 5; Links = 10; NicsPerVm = 4 }
    ) {
        $t = Get-CtsLabTopology -Definition $script:Definition -VmCount $Count
        $t.Vms.Count | Should -Be $Count
        $t.Links.Count | Should -Be $Links
        foreach ($vm in $t.Vms) { $vm.TestNics.Count | Should -Be $NicsPerVm }
    }

    It 'numbers links as documented in topologies.md' {
        $t = Get-CtsLabTopology -Definition $script:Definition -VmCount 5
        ($t.Links | ForEach-Object { "$($_.Pair)=$($_.Number)" }) -join ',' |
            Should -Be 'AB=1,AC=2,BC=3,AD=4,BD=5,CD=6,AE=7,BE=8,CE=9,DE=10'
    }

    It 'assigns names, management addresses, and static MACs' {
        $t = Get-CtsLabTopology -Definition $script:Definition -VmCount 5
        $a = $t.Vms[0]
        $a.Name | Should -Be 'ctsRel-A'
        $a.ComputerName | Should -Be 'CTSREL-A'
        $a.MgmtNic.IPv4Address | Should -Be '192.168.250.1'
        $a.MgmtNic.MacAddress | Should -Be '00155DCE01FE'
        $e = $t.Vms[4]
        $de = $e.TestNics | Where-Object Link -eq 'DE'
        $de.Name | Should -Be 'NIC-ED'
        $de.MacAddress | Should -Be '00155DCE050A'
        $de.IPv4Address | Should -Be '192.168.10.5'
        $de.IPv6Address | Should -Be 'fd00:10::5'
        $de.PeerIPv4 | Should -Be '192.168.10.4'
        $de.SwitchName | Should -Be 'ctsRel-Private-DE'
    }

    It 'gives every NIC a unique MAC and pairs peers symmetrically' {
        $t = Get-CtsLabTopology -Definition $script:Definition -VmCount 5
        $macs = @($t.Vms | ForEach-Object { $_.MgmtNic.MacAddress; $_.TestNics.MacAddress })
        ($macs | Sort-Object -Unique).Count | Should -Be $macs.Count
        foreach ($vm in $t.Vms) {
            foreach ($nic in $vm.TestNics) {
                $peer = ($t.Vms | Where-Object Name -eq $nic.PeerVm).TestNics | Where-Object Link -eq $nic.Link
                $peer.PeerIPv4 | Should -Be $nic.IPv4Address
                $peer.SwitchName | Should -Be $nic.SwitchName
            }
        }
    }

    It 'defaults to vm.count from the definition' {
        (Get-CtsLabTopology -Definition $script:Definition).VmCount | Should -Be $script:Definition.vm.count
    }
}
