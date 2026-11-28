#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
}

Describe 'finite pilot process specifications' {
    It 'resolves distinct server and client artifacts and retains workload defaults' {
        InModuleScope CtsReliability {
            $common = @{ Executable = 'C:\ctsReliability\tools\ctsTraffic.exe'; RunDirectory = 'C:\ctsReliability\runs\pilot'; ServerAddress = '192.168.1.1' }
            $server = New-CtsPilotProcessSpec -Role Server @common
            $client = New-CtsPilotProcessSpec -Role Client @common
            $server.Arguments | Should -Be @('-Listen:192.168.1.1', '-Pattern:duplex', '-ServerExitLimit:1024', '-TimeLimit:420000', '-ConsoleVerbosity:0', "-ConnectionFilename:$($server.ConnectionPath)", "-ErrorFilename:$($server.ErrorPath)", "-StatusFilename:$($server.StatusPath)")
            $client.Arguments | Should -Be @('-Target:192.168.1.1', '-Pattern:duplex', '-Iterations:128', '-TimeLimit:300000', '-ConsoleVerbosity:0', "-ConnectionFilename:$($client.ConnectionPath)", "-ErrorFilename:$($client.ErrorPath)", "-StatusFilename:$($client.StatusPath)")
            $paths = @($server.StdoutPath, $server.StderrPath, $server.ConnectionPath, $server.ErrorPath, $server.StatusPath,
                $client.StdoutPath, $client.StderrPath, $client.ConnectionPath, $client.ErrorPath, $client.StatusPath)
            @($paths | Select-Object -Unique).Count | Should -Be 10
            $server.FilePath | Should -Be $common.Executable
            $client.FilePath | Should -Be $common.Executable
        }
    }

    It 'rejects unsafe paths and non-IPv4 addresses' {
        InModuleScope CtsReliability {
            $common = @{ Role = 'Server'; Executable = 'C:\tools\ctsTraffic.exe'; RunDirectory = 'C:\runs\pilot'; ServerAddress = '192.168.1.1' }
            foreach ($path in @('relative', '\\server\share', 'C:\runs\with space', 'C:\runs\..\other', 'C:\runs\*.csv')) {
                $common.RunDirectory = $path
                { New-CtsPilotProcessSpec @common } | Should -Throw '*Pilot paths*'
            }
            $common.RunDirectory = 'C:\runs\pilot'
            $common.Executable = 'C:\Program Files\ctsTraffic.exe'
            { New-CtsPilotProcessSpec @common } | Should -Throw '*Pilot paths*'
            $common.Executable = 'C:\tools\ctsTraffic.exe'
            foreach ($ip in @('::1', 'not-an-address', '256.0.0.1', '192.168.01.1', '192.168.1.1 -extra')) {
                $common.ServerAddress = $ip
                { New-CtsPilotProcessSpec @common } | Should -Throw '*IPv4*'
            }
        }
    }

    It 'does not create the resolved run directory' {
        $run = Join-Path $TestDrive 'not-created'
        InModuleScope CtsReliability -Parameters @{ Run = $run } {
            param($Run)
            $spec = New-CtsPilotProcessSpec -Role Client -Executable 'C:\tools\ctsTraffic.exe' -RunDirectory $Run -ServerAddress '192.168.1.1'
            $spec.StdoutPath | Should -Be (Join-Path $Run 'g1b-pilot-client-stdout.txt')
            Test-Path -LiteralPath $Run | Should -BeFalse
        }
    }
}
