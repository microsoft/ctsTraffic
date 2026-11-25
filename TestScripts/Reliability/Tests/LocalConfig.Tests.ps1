#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Tests for src\LocalConfig.ps1. All files are written under TestDrive; the share paths are fictitious.

BeforeAll {
    Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'CtsReliability.psm1') -Force
    $script:Definition = Get-CtsLabDefinition -LocalPath (Join-Path $TestDrive 'none.json')
    function New-LocalPath { Join-Path $TestDrive ("{0}\lab.local.json" -f [guid]::NewGuid().ToString('N')) }
    $script:Arm = '\\server.example\share\dbg_arm64.msi'
    $script:Amd = '\\server.example\share\dbg_amd64.msi'
}

Describe 'Set-CtsLocalConfig' {
    It 'plans the change, notes an unreachable share, and writes nothing' {
        $path = New-LocalPath
        $plan = Set-CtsLocalConfig -Definition $script:Definition -LocalPath $path -DebuggerSourceArm64 $script:Arm
        $plan.Action | Should -Be 'Write'
        $plan.Changes | Should -Be @("debugger.sources.arm64: (not set) -> $script:Arm")
        $plan.Notes -join ' ' | Should -Match 'not reachable now'
        Test-Path $path | Should -BeFalse
    }
    It 'writes a file that Get-CtsLabDefinition merges' {
        $path = New-LocalPath
        (Set-CtsLocalConfig -Definition $script:Definition -LocalPath $path -DebuggerSourceArm64 $script:Arm -Apply).Applied | Should -BeTrue
        $d = Get-CtsLabDefinition -LocalPath $path
        $d.debugger.sources.arm64 | Should -Be $script:Arm
        $d.debugger.sources.amd64 | Should -BeNullOrEmpty
    }
    It 'keeps other values already in the file' {
        $path = New-LocalPath
        $null = New-Item -ItemType Directory -Path (Split-Path $path) -Force
        '{ "vm": { "memoryGB": 4 }, "debugger": { "sources": { "amd64": "\\\\old\\dbg_amd64.msi" } } }' | Set-Content -LiteralPath $path
        $null = Set-CtsLocalConfig -Definition $script:Definition -LocalPath $path -DebuggerSourceArm64 $script:Arm -Apply
        $d = Get-CtsLabDefinition -LocalPath $path
        $d.vm.memoryGB | Should -Be 4
        $d.debugger.sources.amd64 | Should -Be '\\old\dbg_amd64.msi'
        $d.debugger.sources.arm64 | Should -Be $script:Arm
    }
    It 'reports no change when the path is already set' {
        $path = New-LocalPath
        $null = Set-CtsLocalConfig -Definition $script:Definition -LocalPath $path -DebuggerSourceAmd64 $script:Amd -Apply
        $plan = Set-CtsLocalConfig -Definition $script:Definition -LocalPath $path -DebuggerSourceAmd64 $script:Amd -Apply
        $plan.Action | Should -Be 'NoChange'
        $plan.Applied | Should -BeFalse
    }
    It 'adds no note for a reachable source' {
        $msi = Join-Path $TestDrive 'dbg_arm64.msi'; 'x' | Set-Content -LiteralPath $msi
        (Set-CtsLocalConfig -Definition $script:Definition -LocalPath (New-LocalPath) -DebuggerSourceArm64 $msi).Notes | Should -BeNullOrEmpty
    }
    It 'blocks <Case>, and -Apply throws' -ForEach @(
        @{ Case = 'a relative path'; Arguments = @{ DebuggerSourceArm64 = 'share\dbg_arm64.msi' }; Message = 'full UNC or drive path' }
        @{ Case = 'a path that is not an MSI'; Arguments = @{ DebuggerSourceArm64 = '\\server\share\dbg_arm64.zip' }; Message = 'full UNC or drive path' }
        @{ Case = 'no source'; Arguments = @{}; Message = 'Nothing to set' }
    ) {
        $path = New-LocalPath
        $plan = Set-CtsLocalConfig -Definition $script:Definition -LocalPath $path @Arguments
        $plan.Blocked | Should -BeTrue
        $plan.Reasons -join ' ' | Should -Match $Message
        { Set-CtsLocalConfig -Definition $script:Definition -LocalPath $path @Arguments -Apply } | Should -Throw "*$Message*"
        Test-Path $path | Should -BeFalse
    }
    It 'refuses a file inside the repository' {
        $inside = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'lab.local.json'
        (Set-CtsLocalConfig -Definition $script:Definition -LocalPath $inside -DebuggerSourceArm64 $script:Arm).Reasons -join ' ' | Should -Match 'inside the repository'
        { Set-CtsLocalConfig -Definition $script:Definition -LocalPath $inside -DebuggerSourceArm64 $script:Arm -Apply } | Should -Throw
        Test-Path $inside | Should -BeFalse
    }
    It 'defaults to lab.local.json under labRoot' {
        $def = Get-CtsLabDefinition -LocalPath (Join-Path $TestDrive 'none.json')
        $def.labRoot = Join-Path $TestDrive 'labroot'
        (Set-CtsLocalConfig -Definition $def -DebuggerSourceArm64 $script:Arm).Path | Should -Be (Join-Path $def.labRoot 'lab.local.json')
    }
}
