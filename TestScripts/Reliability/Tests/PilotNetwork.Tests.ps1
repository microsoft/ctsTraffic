#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Guest = [pscustomobject]@{ Channel = 'Direct'; Target = 'B'; LogPath = ''; Session = $null }
    $script:Sample = [pscustomobject]@{
        Role = 'Client'; Guest = 'client'; ObservedUtc = '2026-01-01T00:00:00Z'
        ServerAddress = '192.168.1.1'; SelectedAddress = '192.168.1.2'
        AddressState = 'Preferred'; InterfaceIndex = 4; InterfaceAlias = 'test'
        AdapterStatus = 'Up'; RoutePrefix = '192.168.1.0/24'; NextHop = '0.0.0.0'; RouteMetric = 256
    }
}

Describe 'pilot network sampling' {
    BeforeEach {
        Mock -ModuleName CtsReliability Find-NetRoute {
            [pscustomobject]@{
                CimClass = [pscustomobject]@{ CimClassName = 'MSFT_NetIPAddress' }
                IPAddress = '192.168.1.2'; AddressState = 'Preferred'; InterfaceIndex = 4; InterfaceAlias = 'test'
            }
            [pscustomobject]@{
                CimClass = [pscustomobject]@{ CimClassName = 'MSFT_NetRoute' }
                InterfaceIndex = 4; DestinationPrefix = '192.168.1.0/24'; NextHop = '0.0.0.0'; RouteMetric = 256
            }
        }
        Mock -ModuleName CtsReliability Get-NetAdapter { [pscustomobject]@{ ifIndex = 4; Status = 'Up' } }
        Mock -ModuleName CtsReliability Get-NetIPAddress {
            [pscustomobject]@{ IPAddress = '192.168.1.1'; AddressState = 'Preferred'; InterfaceIndex = 4; InterfaceAlias = 'test' }
        }
    }

    It 'records the selected client address, route, and adapter' {
        InModuleScope CtsReliability {
            $sample = & $script:CtsPilotNetworkScript 'Client' '192.168.1.1'
            $sample.SelectedAddress | Should -Be '192.168.1.2'
            $sample.RoutePrefix | Should -Be '192.168.1.0/24'
            $sample.AdapterStatus | Should -Be 'Up'
        }
        Should -Invoke -ModuleName CtsReliability Find-NetRoute -Times 1 -Exactly -ParameterFilter { $RemoteIPAddress -eq '192.168.1.1' }
    }

    It 'records the server address without querying a client route' {
        InModuleScope CtsReliability {
            $sample = & $script:CtsPilotNetworkScript 'Server' '192.168.1.1'
            $sample.SelectedAddress | Should -Be '192.168.1.1'
            $sample.RoutePrefix | Should -BeNullOrEmpty
        }
        Should -Invoke -ModuleName CtsReliability Find-NetRoute -Times 0
    }

    It 'rejects inconsistent source and route interfaces' {
        Mock -ModuleName CtsReliability Find-NetRoute {
            [pscustomobject]@{ CimClass = [pscustomobject]@{ CimClassName = 'MSFT_NetIPAddress' }; InterfaceIndex = 4 }
            [pscustomobject]@{ CimClass = [pscustomobject]@{ CimClassName = 'MSFT_NetRoute' }; InterfaceIndex = 5 }
        }
        InModuleScope CtsReliability {
            { & $script:CtsPilotNetworkScript 'Client' '192.168.1.1' } | Should -Throw '*interfaces do not match*'
        }
    }

    It 'reports remote sampling errors as unknown' {
        Mock -ModuleName CtsReliability Invoke-CtsLogged { throw 'session lost' }
        InModuleScope CtsReliability -Parameters @{ Guest = $script:Guest } {
            param($Guest)
            { Get-CtsPilotNetworkSample -GuestSession $Guest -Role Client -ServerAddress '192.168.1.1' -TimeoutSeconds 2 } |
                Should -Throw '*unknown*session lost*'
        }
    }
}

Describe 'pilot network sample checks' {
    It 'accepts a matching client observation' {
        InModuleScope CtsReliability -Parameters @{ Sample = $script:Sample } {
            param($Sample)
            @(Test-CtsPilotNetworkSample -Sample $Sample -Role Client -ServerAddress '192.168.1.1' `
                -ExpectedAddress '192.168.1.2').Count | Should -Be 0
        }
    }

    It 'reports the wrong client source and a down adapter' {
        $sample = $script:Sample.PSObject.Copy()
        $sample.SelectedAddress = '192.168.250.2'
        $sample.AdapterStatus = 'Down'
        InModuleScope CtsReliability -Parameters @{ Sample = $sample } {
            param($Sample)
            $findings = @(Test-CtsPilotNetworkSample -Sample $Sample -Role Client -ServerAddress '192.168.1.1' `
                -ExpectedAddress '192.168.1.2')
            $findings | Should -Contain 'Selected network address does not match'
            $findings | Should -Contain 'Network adapter is not up'
        }
    }

    It 'reports missing or malformed route evidence' {
        $sample = $script:Sample.PSObject.Copy()
        $sample.RoutePrefix = 'invalid'
        $sample.InterfaceIndex = 0
        InModuleScope CtsReliability -Parameters @{ Sample = $sample } {
            param($Sample)
            $findings = @(Test-CtsPilotNetworkSample -Sample $Sample -Role Client -ServerAddress '192.168.1.1' `
                -ExpectedAddress '192.168.1.2')
            $findings | Should -Contain 'Client route is missing or invalid'
            $findings | Should -Contain 'Network interface is missing or invalid'
        }
    }

    It 'rejects malformed or unrelated IPv4 routes' {
        InModuleScope CtsReliability -Parameters @{ Original = $script:Sample } {
            param($Original)
            foreach ($route in '999.1.1.0/24', '192.168.1.1/24', '192.168.2.0/24') {
                $sample = $Original.PSObject.Copy()
                $sample.RoutePrefix = $route
                @(Test-CtsPilotNetworkSample -Sample $sample -Role Client -ServerAddress '192.168.1.1' `
                    -ExpectedAddress '192.168.1.2') | Should -Contain 'Client route is missing or invalid'
            }
        }
    }
}
