#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Tests for src\Inventory.ps1. 'Live' tests read (never change) the local machine; Hyper-V cmdlets are mocked
# in the unit tests, and the full Hyper-V capture is tagged 'Elevated'.

BeforeDiscovery {
    $script:IsElevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:ExpectedSections = @(
        'identity', 'compute', 'storage', 'adapters', 'ipAddresses', 'ipInterfaces', 'routes', 'neighbors', 'dns',
        'networkProfiles', 'firewall', 'verifier', 'crashControl', 'power', 'windowsUpdate', 'defender',
        'networkDrivers', 'tools', 'debuggers', 'timeSync', 'health'
    )
}

Describe 'Get-CtsDeviceInventory (local machine)' -Tag 'Live' {
    BeforeAll {
        $script:Inventory = Get-CtsDeviceInventory -RootPath (Join-Path $TestDrive 'root')
    }

    It 'reports schema ctsReliability.deviceInventory/1 with all 21 sections in order' {
        $script:Inventory.schema | Should -Be 'ctsReliability.deviceInventory/1'
        @($script:Inventory.sections.Keys) | Should -Be $script:ExpectedSections
    }

    It 'gives every section a status, source, and capture time; unavailable sections carry a reason' {
        foreach ($name in $script:ExpectedSections) {
            $s = $script:Inventory.sections[$name]
            $s.status | Should -BeIn 'ok', 'unavailable' -Because $name
            $s.source | Should -Not -BeNullOrEmpty -Because $name
            [DateTime]::Parse($s.capturedUtc) | Should -BeOfType [DateTime]
            if ($s.status -eq 'unavailable') { $s.reason | Should -Not -BeNullOrEmpty -Because $name }
        }
    }

    It 'captures identity of this machine' {
        $script:Inventory.sections.identity.status | Should -Be 'ok'
        $script:Inventory.sections.identity.data.computerName | Should -Be $env:COMPUTERNAME
    }

    It 'keeps full route detail for all interfaces by default' {
        $routes = $script:Inventory.sections.routes.data
        @($routes.detailInterfaceFilter) | Should -Be @('*')
        @($routes.routes).Count | Should -BeGreaterThan 0
        @($routes.otherInterfaceRouteCounts).Count | Should -Be 0
    }

    It 'summarizes routes as per-interface counts outside -RouteDetailInterface' {
        $filtered = Get-CtsDeviceInventory -RootPath (Join-Path $TestDrive 'root') -RouteDetailInterface 'NoSuchInterface*'
        $routes = $filtered.sections.routes.data
        @($routes.routes).Count | Should -Be 0
        $total = (@($routes.otherInterfaceRouteCounts) | ForEach-Object { $_.routeCount } | Measure-Object -Sum).Sum
        $total | Should -Be @($script:Inventory.sections.routes.data.routes).Count
    }

    It 'is self-contained: the script runs in a fresh runspace without the module' {
        $ps = [powershell]::Create()
        try {
            $null = $ps.AddScript((Get-CtsDeviceInventoryScript).ToString()).AddArgument((Join-Path $TestDrive 'root'))
            $result = @($ps.Invoke())
            $ps.HadErrors | Should -BeFalse
            $result[0].schema | Should -Be 'ctsReliability.deviceInventory/1'
            $result[0].sections.Count | Should -Be 21
        }
        finally { $ps.Dispose() }
    }
}

Describe 'Get-CtsHyperVInventory (mocked Hyper-V)' {
    It 'returns unavailable with the reason when Hyper-V access is denied' {
        Mock -ModuleName CtsReliability Get-VMHost { throw 'You do not have the required permission to complete this task.' }
        $r = Get-CtsHyperVInventory -VMName 'ctsRel-A'
        $r.status | Should -Be 'unavailable'
        $r.reason | Should -BeLike '*required permission*'
        $r.data | Should -BeNullOrEmpty
    }

    It 'lists requested VMs that do not exist and other VMs by name only' {
        Mock -ModuleName CtsReliability Get-VMHost { [pscustomobject]@{ Name = 'HOST'; VirtualMachinePath = 'C:\vms'; VirtualHardDiskPath = 'C:\vhd' } }
        Mock -ModuleName CtsReliability Get-VMSwitch { }
        Mock -ModuleName CtsReliability Get-VMNetworkAdapter { }
        Mock -ModuleName CtsReliability Get-VM { [pscustomobject]@{ Name = 'SomeoneElsesVm' } }
        $r = Get-CtsHyperVInventory -VMName 'ctsRel-A', 'ctsRel-B'
        $r.status | Should -Be 'ok'
        @($r.data.missingVms) | Should -Be @('ctsRel-A', 'ctsRel-B')
        @($r.data.otherVmNames) | Should -Be @('SomeoneElsesVm')
        @($r.data.vms).Count | Should -Be 0
    }

    It 'handles a host with no VMs at all' {
        Mock -ModuleName CtsReliability Get-VMHost { [pscustomobject]@{ Name = 'HOST'; VirtualMachinePath = 'C:\vms'; VirtualHardDiskPath = 'C:\vhd' } }
        Mock -ModuleName CtsReliability Get-VMSwitch { }
        Mock -ModuleName CtsReliability Get-VMNetworkAdapter { }
        Mock -ModuleName CtsReliability Get-VM { }
        $r = Get-CtsHyperVInventory -VMName 'ctsRel-A'
        $r.status | Should -Be 'ok'
        @($r.data.missingVms) | Should -Be @('ctsRel-A')
        @($r.data.otherVmNames).Count | Should -Be 0
    }
}

Describe 'Get-CtsHyperVInventory (real Hyper-V)' -Tag 'Live', 'Elevated' {
    It 'captures host, switches, and VM names' -Skip:(-not $script:IsElevated) {
        $r = Get-CtsHyperVInventory -VMName 'ctsRel-A'
        $r.status | Should -Be 'ok' -Because $r.reason
        $r.data.host.name | Should -Not -BeNullOrEmpty
    }
}
