#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Tests for src\DebuggerCache.ps1 (policy: documents/reliability/debugging-tools.md).
# Unit tests build real minimal MSI databases in TestDrive through the Windows Installer COM API, so the MSI
# reader is exercised for real; only Authenticode (test MSIs are unsigned) and guest calls are mocked.
# 'Share' tests read the real the internal share debugger MSI and are excluded unless the runner passes -IncludeShare.

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Definition = Get-CtsLabDefinition

    function Invoke-Com($Object, [string]$Method, [object[]]$Arguments) {
        $Object.GetType().InvokeMember($Method, 'InvokeMethod', $null, $Object, $Arguments)
    }

    # Creates an MSI database containing only a Property table. $Padding changes the file hash without
    # changing the version.
    function New-TestMsi {
        param([string]$Path, [string]$Version, [string]$Padding = '')
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force
        if (Test-Path $Path) { Remove-Item $Path -Force }
        $installer = New-Object -ComObject WindowsInstaller.Installer
        $db = Invoke-Com $installer 'OpenDatabase' @($Path, 3)
        $view = Invoke-Com $db 'OpenView' @('CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` LONGCHAR NOT NULL PRIMARY KEY `Property`)')
        $null = Invoke-Com $view 'Execute' $null; $null = Invoke-Com $view 'Close' $null
        $props = [ordered]@{ ProductVersion = $Version; ProductCode = '{11111111-2222-3333-4444-555555555555}'; ProductName = 'Debugging Tools for Windows (test)'; Manufacturer = 'Pester'; Padding = "x$Padding" }
        foreach ($k in $props.Keys) {
            $view = Invoke-Com $db 'OpenView' @("INSERT INTO ``Property`` (``Property``, ``Value``) VALUES ('$k', '$($props[$k])')")
            $null = Invoke-Com $view 'Execute' $null; $null = Invoke-Com $view 'Close' $null
        }
        $null = Invoke-Com $db 'Commit' $null
        foreach ($o in $view, $db, $installer) { $null = [Runtime.InteropServices.Marshal]::ReleaseComObject($o) }
        [GC]::Collect(); [GC]::WaitForPendingFinalizers()
    }

    function Get-ValidSignature { [pscustomobject]@{ Status = 'Valid'; StatusMessage = 'ok'; SignerCertificate = $null } }
}

Describe 'Compare-CtsVersion' {
    It '<Left> vs <Right> is <Expected>' -ForEach @(
        @{ Left = '10.0.1'; Right = '10.0.1.0'; Expected = 0 }
        @{ Left = '10.0.26100.1'; Right = '10.0.9999.1'; Expected = 1 }
        @{ Left = '10.0.29680.1004'; Right = '10.0.29680.1010'; Expected = -1 }
        @{ Left = '2'; Right = '10'; Expected = -1 }
    ) {
        Compare-CtsVersion $Left $Right | Should -Be $Expected
    }

    It 'rejects non-numeric versions' {
        { Compare-CtsVersion '10.0.beta' '10.0.1' } | Should -Throw -ExpectedMessage '*Not a numeric version*'
    }
}

Describe 'Get-CtsMsiProperty' {
    It 'reads properties from an MSI without installing it and releases the file' {
        $msi = Join-Path $TestDrive 'read\test.msi'
        New-TestMsi -Path $msi -Version '10.0.1.5'
        $p = Get-CtsMsiProperty -Path $msi
        $p.ProductVersion | Should -Be '10.0.1.5'
        $p.ProductName | Should -Be 'Debugging Tools for Windows (test)'
        $p.ProductCode | Should -Be '{11111111-2222-3333-4444-555555555555}'
        { Remove-Item -LiteralPath $msi -ErrorAction Stop } | Should -Not -Throw -Because 'the COM handle must be released'
    }

    It 'fails for a missing file' {
        { Get-CtsMsiProperty -Path (Join-Path $TestDrive 'nope.msi') } | Should -Throw
    }
}

Describe 'Update-CtsDebuggerCache refresh rule' {
    BeforeAll {
        function Update([string]$Source = $script:Source) {
            Update-CtsDebuggerCache -Definition $script:Definition -Architecture arm64 -SourcePath $Source -CacheRoot $script:Cache -LogPath $script:Log
        }
    }

    BeforeEach {
        $script:Root = Join-Path $TestDrive ([guid]::NewGuid().ToString('N'))
        $script:Cache = Join-Path $script:Root 'cache'
        $script:Source = Join-Path $script:Root 'share\dbg_arm64.msi'
        $script:Log = Join-Path $script:Root 'setup.jsonl'
        Mock -ModuleName CtsReliability Get-AuthenticodeSignature { Get-ValidSignature }
    }

    It 'fills an empty cache from a reachable source and records metadata' {
        New-TestMsi -Path $script:Source -Version '10.0.100.1'
        $r = Update
        $r.refreshed | Should -BeTrue
        $r.shareReachable | Should -BeTrue
        $r.cachedVersionBefore | Should -BeNullOrEmpty
        $r.cachedVersion | Should -Be '10.0.100.1'
        Test-Path $r.msiPath | Should -BeTrue
        $meta = Get-Content (Join-Path $script:Cache 'arm64\current.json') -Raw | ConvertFrom-Json
        $meta.sha256 | Should -Be (Get-FileHash $script:Source -Algorithm SHA256).Hash
        $meta.signatureStatus | Should -Be 'Valid'
        $meta.sourcePath | Should -Be $script:Source
        @(Get-ChildItem $script:Cache -Recurse -Filter '*.partial').Count | Should -Be 0
        (Get-Content $script:Log | ConvertFrom-Json).operation | Should -Be 'DebuggerCacheCheck'
    }

    It 'does nothing when the source has the same version and hash' {
        New-TestMsi -Path $script:Source -Version '10.0.100.1'; $null = Update
        $r = Update
        $r.refreshed | Should -BeFalse
        @($r.warnings).Count | Should -Be 0
    }

    It 'keeps the cache and warns when the same version has a different hash' {
        New-TestMsi -Path $script:Source -Version '10.0.100.1'; $null = Update
        $before = (Get-CtsDebuggerCacheEntry -Definition $script:Definition -Architecture arm64 -CacheRoot $script:Cache).sha256
        New-TestMsi -Path $script:Source -Version '10.0.100.1' -Padding 'rebuilt'
        $r = Update
        $r.refreshed | Should -BeFalse
        $r.warnings | Should -BeLike '*same version*different SHA-256*'
        (Get-CtsDebuggerCacheEntry -Definition $script:Definition -Architecture arm64 -CacheRoot $script:Cache).sha256 | Should -Be $before
    }

    It 'refreshes to a newer version, keeps the previous one, and prunes older ones' {
        New-TestMsi -Path $script:Source -Version '10.0.100.1'; $null = Update
        New-TestMsi -Path $script:Source -Version '10.0.200.1'; $r = Update
        $r.refreshed | Should -BeTrue
        $r.cachedVersionBefore | Should -Be '10.0.100.1'
        @((Get-ChildItem (Join-Path $script:Cache 'arm64') -Directory).Name | Sort-Object) | Should -Be @('10.0.100.1', '10.0.200.1')
        New-TestMsi -Path $script:Source -Version '10.0.300.1'; $null = Update
        @((Get-ChildItem (Join-Path $script:Cache 'arm64') -Directory).Name | Sort-Object) | Should -Be @('10.0.200.1', '10.0.300.1')
    }

    It 'never downgrades' {
        New-TestMsi -Path $script:Source -Version '10.0.200.1'; $null = Update
        New-TestMsi -Path $script:Source -Version '10.0.100.1'
        $r = Update
        $r.refreshed | Should -BeFalse
        $r.cachedVersion | Should -Be '10.0.200.1'
        $r.warnings | Should -BeLike '*older than cached*not downgrading*'
    }

    It 'uses the cache and records the share as unreachable when the source is missing' {
        New-TestMsi -Path $script:Source -Version '10.0.100.1'; $null = Update
        $r = Update -Source (Join-Path $script:Root 'offline\dbg_arm64.msi')
        $r.shareReachable | Should -BeFalse
        $r.refreshed | Should -BeFalse
        $r.cachedVersion | Should -Be '10.0.100.1'
        $r.warnings | Should -BeLike '*not accessible*'
    }

    It 'fails when nothing is cached and the source is unreachable' {
        { Update -Source (Join-Path $script:Root 'offline\dbg_arm64.msi') } | Should -Throw -ExpectedMessage '*No cached debugger MSI*'
    }

    It 'rejects an MSI whose signature is not valid and leaves no partial file' {
        Mock -ModuleName CtsReliability Get-AuthenticodeSignature { [pscustomobject]@{ Status = 'NotSigned'; StatusMessage = 'unsigned'; SignerCertificate = $null } }
        New-TestMsi -Path $script:Source -Version '10.0.100.1'
        { Update } | Should -Throw -ExpectedMessage '*signature is not valid*'
        @(Get-ChildItem $script:Cache -Recurse -File -ErrorAction SilentlyContinue).Count | Should -Be 0
        Get-CtsDebuggerCacheEntry -Definition $script:Definition -Architecture arm64 -CacheRoot $script:Cache | Should -BeNullOrEmpty
    }

    It 'rejects a copy whose version differs from the version read from the source' {
        New-TestMsi -Path $script:Source -Version '10.0.100.1'
        # Unfiltered mock (Pester 6 does not fall through to the real command): the copy reads a different version.
        Mock -ModuleName CtsReliability Get-CtsMsiProperty {
            $v = if ($Path -like '*.partial') { '9.9.9.9' } else { '10.0.100.1' }
            [pscustomobject]@{ ProductVersion = $v; ProductCode = 'x'; ProductName = 'x'; Manufacturer = 'x' }
        }
        { Update } | Should -Throw -ExpectedMessage '*does not match source version*'
        @(Get-ChildItem $script:Cache -Recurse -Filter '*.partial').Count | Should -Be 0
    }
}

Describe 'Install decision' {
    It '<Installed> with cache <Cached> -> <Expected>' -ForEach @(
        @{ Installed = $null; Cached = '10.0.2.0'; Expected = 'Install' }
        @{ Installed = '10.0.1.0'; Cached = '10.0.2.0'; Expected = 'Install' }
        @{ Installed = '10.0.2.0'; Cached = '10.0.2.0'; Expected = 'Skip' }
        @{ Installed = '10.0.3.0'; Cached = '10.0.2.0'; Expected = 'Drift' }
    ) {
        InModuleScope CtsReliability -Parameters @{ I = $Installed; C = $Cached; E = $Expected } {
            param($I, $C, $E)
            Get-CtsDebuggerInstallDecision -InstalledVersion $I -CachedVersion $C | Should -Be $E
        }
    }
}

Describe 'Install-CtsDebuggerPackage (mocked guest)' {
    BeforeAll {
        $script:Cache = Join-Path $TestDrive 'install-cache'
        New-TestMsi -Path (Join-Path $TestDrive 'install-src\dbg_arm64.msi') -Version '10.0.200.1'
        Mock -ModuleName CtsReliability Get-AuthenticodeSignature { Get-ValidSignature }
        $null = Update-CtsDebuggerCache -Definition $script:Definition -Architecture arm64 -SourcePath (Join-Path $TestDrive 'install-src\dbg_arm64.msi') -CacheRoot $script:Cache
        $script:Guest = [pscustomobject]@{ VMName = 'ctsRel-A'; Channel = 'Direct'; Target = 'ctsRel-A'; Session = $null; LogPath = $null }
        $script:AllTools = @{ 'cdb.exe' = @{ path = 'C:\Debuggers\cdb.exe'; fileVersion = '10.0.200.1' }; 'ntsd.exe' = @{ path = 'C:\Debuggers\ntsd.exe'; fileVersion = '10.0.200.1' }; 'appverif.exe' = @{ path = 'C:\Windows\System32\appverif.exe'; fileVersion = '10.0.200.1' } }
    }

    BeforeEach {
        Mock -ModuleName CtsReliability Copy-CtsToGuest { }
        Mock -ModuleName CtsReliability Copy-CtsFromGuest { }
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { } -ParameterFilter { $Operation -eq 'EnsureLogDir' }
    }

    It 'installs when the guest has no package and reports tools' {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { @{ installedVersion = $null; tools = @{ 'cdb.exe' = $null; 'ntsd.exe' = $null; 'appverif.exe' = $null } } } -ParameterFilter { $Operation -eq 'DebuggerQuery' }
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { @{ installedVersion = '10.0.200.1'; exitCode = 0; tools = $script:AllTools } } -ParameterFilter { $Operation -eq 'DebuggerInstall' }
        $r = Install-CtsDebuggerPackage -Definition $script:Definition -Architecture arm64 -GuestSession $script:Guest -CacheRoot $script:Cache -EvidenceDirectory (Join-Path $TestDrive 'evidence')
        $r.decision | Should -Be 'Install'
        $r.exitCode | Should -Be 0
        $r.rebootRequired | Should -BeFalse
        $r.versionAfter | Should -Be '10.0.200.1'
        @($r.missingTools).Count | Should -Be 0
        Should -Invoke -ModuleName CtsReliability Copy-CtsToGuest -Times 1 -Exactly -ParameterFilter { $Destination -eq 'C:\ctsReliability\tools\dbg' }
        Should -Invoke -ModuleName CtsReliability Copy-CtsFromGuest -Times 1 -Exactly
        Should -Invoke -ModuleName CtsReliability Invoke-CtsGuestCommand -Times 1 -Exactly -ParameterFilter {
            $Operation -eq 'DebuggerInstall' -and $ArgumentList[0] -eq 'Install' -and $ArgumentList[2] -eq 'C:\ctsReliability\tools\dbg\dbg_arm64.msi'
        }
    }

    It 'reports a required reboot for exit code 3010' {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { @{ installedVersion = '10.0.100.1'; tools = $script:AllTools } } -ParameterFilter { $Operation -eq 'DebuggerQuery' }
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { @{ installedVersion = '10.0.200.1'; exitCode = 3010; tools = $script:AllTools } } -ParameterFilter { $Operation -eq 'DebuggerInstall' }
        $r = Install-CtsDebuggerPackage -Definition $script:Definition -Architecture arm64 -GuestSession $script:Guest -CacheRoot $script:Cache
        $r.decision | Should -Be 'Install'
        $r.rebootRequired | Should -BeTrue
    }

    It 'fails setup for any other exit code' {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { @{ installedVersion = $null; tools = @{} } } -ParameterFilter { $Operation -eq 'DebuggerQuery' }
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { @{ installedVersion = $null; exitCode = 1603; tools = @{} } } -ParameterFilter { $Operation -eq 'DebuggerInstall' }
        { Install-CtsDebuggerPackage -Definition $script:Definition -Architecture arm64 -GuestSession $script:Guest -CacheRoot $script:Cache } | Should -Throw -ExpectedMessage '*exit code 1603*'
    }

    It 'skips when the same version is installed and still reports missing tools' {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { @{ installedVersion = '10.0.200.1'; tools = @{ 'cdb.exe' = @{ path = 'C:\Debuggers\cdb.exe' }; 'ntsd.exe' = $null; 'appverif.exe' = $null } } } -ParameterFilter { $Operation -eq 'DebuggerQuery' }
        $r = Install-CtsDebuggerPackage -Definition $script:Definition -Architecture arm64 -GuestSession $script:Guest -CacheRoot $script:Cache
        $r.decision | Should -Be 'Skip'
        @($r.missingTools | Sort-Object) | Should -Be @('appverif.exe', 'ntsd.exe')
        Should -Invoke -ModuleName CtsReliability Copy-CtsToGuest -Times 0 -Exactly
    }

    It 'records drift and does not downgrade a newer installed version' {
        Mock -ModuleName CtsReliability Invoke-CtsGuestCommand { @{ installedVersion = '10.0.300.1'; tools = $script:AllTools } } -ParameterFilter { $Operation -eq 'DebuggerQuery' }
        $r = Install-CtsDebuggerPackage -Definition $script:Definition -Architecture arm64 -GuestSession $script:Guest -CacheRoot $script:Cache
        $r.decision | Should -Be 'Drift'
        Should -Invoke -ModuleName CtsReliability Copy-CtsToGuest -Times 0 -Exactly
    }

    It 'requires a cached MSI' {
        { Install-CtsDebuggerPackage -Definition $script:Definition -Architecture amd64 -GuestSession $script:Guest -CacheRoot $script:Cache } | Should -Throw -ExpectedMessage '*Update-CtsDebuggerCache*'
    }
}

Describe 'Debugger package query script (this machine)' -Tag 'Live' {
    It 'reports the installed version (if any) and a path or null for each tool without changing anything' {
        $r = & (Get-CtsDebuggerPackageScript) 'Query' 'Debugging Tools for Windows (arm64)'
        $r.computerName | Should -Be $env:COMPUTERNAME
        $r.exitCode | Should -BeNullOrEmpty
        @($r.tools.Keys) | Should -Be @('cdb.exe', 'ntsd.exe', 'appverif.exe')
    }
}

Describe 'Real debugger share' -Tag 'Share' {
    It 'reads the ARM64 MSI version from the share' {
        $p = Get-CtsMsiProperty -Path $script:Definition.debugger.sources.arm64
        $p.ProductName | Should -BeLike 'Debugging Tools for Windows*'
        { [version]$p.ProductVersion } | Should -Not -Throw
    }

    It 'fills a fresh cache from the share with a valid Authenticode signature' {
        $r = Update-CtsDebuggerCache -Definition $script:Definition -Architecture arm64 -CacheRoot (Join-Path $TestDrive 'share-cache')
        $r.refreshed | Should -BeTrue
        (Get-Content (Join-Path $TestDrive 'share-cache\arm64\current.json') -Raw | ConvertFrom-Json).signatureStatus | Should -Be 'Valid'
    }
}
