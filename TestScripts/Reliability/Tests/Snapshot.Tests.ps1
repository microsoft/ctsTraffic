#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }
# Tests for src\Snapshot.ps1. Unit tests use synthetic snapshots; the 'Live' test captures a host-only
# snapshot of this machine into TestDrive.

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Definition = Get-CtsLabDefinition

    function New-Section([object]$Data, [string]$Status = 'ok', [string]$Reason) {
        $s = [ordered]@{ status = $Status; source = 'synthetic'; capturedUtc = '2026-01-01T00:00:00.0000000Z'; data = $Data }
        if ($Reason) { $s.reason = $Reason }
        $s
    }

    # Builds a valid snapshot (round-tripped through JSON, like one read from disk). $Mutate edits the
    # ordered-dictionary form before serialization.
    function New-SyntheticSnapshot {
        param([scriptblock]$Mutate)
        $topology = Get-CtsLabTopology -Definition $script:Definition -VmCount 2
        $hostSections = [ordered]@{
            identity = New-Section ([ordered]@{ computerName = 'HOST1'; lastBootUtc = '2026-01-01T00:00:00Z' })
            routes   = New-Section ([ordered]@{
                    detailInterfaceFilter = @('vEthernet (ctsRel*')
                    routes = @(
                        [ordered]@{ interfaceAlias = 'vEthernet (ctsRel-Mgmt)'; destinationPrefix = '192.168.250.0/24'; nextHop = '0.0.0.0'; routeMetric = 256 }
                        [ordered]@{ interfaceAlias = 'vEthernet (ctsRel-Mgmt)'; destinationPrefix = '192.168.250.254/32'; nextHop = '0.0.0.0'; routeMetric = 256 }
                    )
                    otherInterfaceRouteCounts = @([ordered]@{ name = 'Wi-Fi'; routeCount = 1549 })
                })
            hyperV   = New-Section ([ordered]@{ missingVms = @() })
        }
        $devices = @([ordered]@{ deviceId = 'HOST1'; role = 'host'; vmName = $null; channel = 'local'; status = 'complete'; reason = $null; inventory = [ordered]@{ schema = 'ctsReliability.deviceInventory/1'; sections = $hostSections } })
        foreach ($vm in $topology.Vms) {
            $devices += [ordered]@{ deviceId = $vm.ComputerName; role = 'guest'; vmName = $vm.Name; channel = 'Direct'; status = 'complete'; reason = $null
                inventory = [ordered]@{ schema = 'ctsReliability.deviceInventory/1'; sections = [ordered]@{ identity = New-Section ([ordered]@{ computerName = $vm.ComputerName }) } } }
        }
        $snapshot = [ordered]@{
            schemaVersion = 1; snapshotId = 'snap-20260101T000000Z-HOST1'; labId = 'ctsrel-v1'
            captureStartUtc = '2026-01-01T00:00:00.0000000Z'; captureEndUtc = '2026-01-01T00:01:00.0000000Z'
            hostOnly = $false; complete = $true; topology = $topology; devices = $devices; errors = @()
        }
        if ($Mutate) { & $Mutate $snapshot }
        return ($snapshot | ConvertTo-Json -Depth 30 | ConvertFrom-Json)
    }
}

Describe 'Test-CtsSnapshot' {
    It 'accepts a valid snapshot' {
        @(Test-CtsSnapshot -Snapshot (New-SyntheticSnapshot)) | Should -BeNullOrEmpty
    }

    It 'reports <Case>' -ForEach @(
        @{ Case = 'a missing required field'; Expected = "missing field 'labId'"; Mutate = { param($s) $s.Remove('labId') } }
        @{ Case = 'an unsupported schema version'; Expected = "unsupported schemaVersion*"; Mutate = { param($s) $s.schemaVersion = 2 } }
        @{ Case = 'a malformed snapshotId'; Expected = 'malformed snapshotId*'; Mutate = { param($s) $s.snapshotId = 'snapshot-1' } }
        @{ Case = 'an end time before the start time'; Expected = 'captureEndUtc is before captureStartUtc'; Mutate = { param($s) $s.captureEndUtc = '2025-12-31T00:00:00Z' } }
        @{ Case = 'a malformed time'; Expected = 'malformed captureStartUtc*'; Mutate = { param($s) $s.captureStartUtc = 'yesterday-ish' } }
        @{ Case = 'two host devices'; Expected = 'snapshot must contain exactly one host device'; Mutate = { param($s) $s.devices[1].role = 'host' } }
        @{ Case = 'a duplicate deviceId'; Expected = "duplicate deviceId 'CTSREL-A'"; Mutate = { param($s) $s.devices[2].deviceId = 'CTSREL-A' } }
        @{ Case = 'an invalid role'; Expected = "*invalid role 'router'"; Mutate = { param($s) $s.devices[1].role = 'router' } }
        @{ Case = 'an invalid status'; Expected = "*invalid status 'done'"; Mutate = { param($s) $s.devices[1].status = 'done'; $s.complete = $false } }
        @{ Case = 'a topology VM without a device'; Expected = "topology VM 'ctsRel-B' has no device record"; Mutate = { param($s) $s.devices = @($s.devices[0], $s.devices[1]) } }
        @{ Case = 'a device that is not in the topology'; Expected = "device vmName 'ctsRel-Z' is not in the topology"; Mutate = { param($s) $s.devices[2].vmName = 'ctsRel-Z' } }
        @{ Case = 'a failed device without a reason'; Expected = "*status 'failed' without a reason"; Mutate = { param($s) $s.devices[1].status = 'failed'; $s.devices[1].inventory = $null; $s.complete = $false } }
        @{ Case = 'a complete device with no sections'; Expected = "*no inventory sections"; Mutate = { param($s) $s.devices[1].inventory = $null } }
        @{ Case = 'an unavailable section without a reason'; Expected = "*section 'identity' is unavailable without a reason"; Mutate = { param($s) $s.devices[1].status = 'partial'; $s.devices[1].inventory.sections.identity.status = 'unavailable'; $s.complete = $false } }
        @{ Case = 'a complete device with an unavailable section'; Expected = "*marked complete but has unavailable sections"; Mutate = { param($s) $s.devices[1].inventory.sections.identity.status = 'unavailable'; $s.devices[1].inventory.sections.identity.reason = 'x' } }
        @{ Case = 'a section without a source'; Expected = "*section 'identity' has no source"; Mutate = { param($s) $s.devices[1].inventory.sections.identity.source = '' } }
        @{ Case = 'a complete flag that contradicts device statuses'; Expected = "snapshot 'complete' flag does not match device statuses"; Mutate = { param($s) $s.devices[1].status = 'notCaptured'; $s.devices[1].reason = 'host-only'; $s.devices[1].inventory = $null } }
        @{ Case = 'a host without a hyperV section'; Expected = "host device 'HOST1' has no hyperV section"; Mutate = { param($s) $s.devices[0].inventory.sections.Remove('hyperV') } }
    ) {
        $errors = @(Test-CtsSnapshot -Snapshot (New-SyntheticSnapshot -Mutate $Mutate))
        $errors | Where-Object { $_ -like $Expected } | Should -Not -BeNullOrEmpty -Because ($errors -join ' | ')
    }
}

Describe 'Snapshot files and manifest' {
    BeforeAll {
        $script:Dir = Join-Path $TestDrive 'snap'
        $null = New-Item -ItemType Directory -Path $script:Dir
        $snapshot = New-SyntheticSnapshot
        Set-Content -LiteralPath (Join-Path $script:Dir 'snapshot.json') -Value ($snapshot | ConvertTo-Json -Depth 30) -Encoding UTF8
        Set-Content -LiteralPath (Join-Path $script:Dir 'report.md') -Value (ConvertTo-CtsSnapshotReport -Snapshot $snapshot) -Encoding UTF8
        InModuleScope CtsReliability -Parameters @{ Dir = $script:Dir } { param($Dir) Write-CtsManifest -Directory $Dir }
    }

    It 'validates a directory whose manifest matches' {
        @(Test-CtsSnapshot -Path $script:Dir) | Should -BeNullOrEmpty
        $manifest = Get-Content (Join-Path $script:Dir 'manifest.json') -Raw | ConvertFrom-Json
        @($manifest.files.name) | Should -Be @('report.md', 'snapshot.json')
    }

    It 'reports a modified file as a hash mismatch' {
        Add-Content -LiteralPath (Join-Path $script:Dir 'report.md') -Value 'tampered'
        @(Test-CtsSnapshot -Path $script:Dir) | Should -Contain 'hash mismatch report.md'
    }

    It 'reports a deleted file' {
        Remove-Item -LiteralPath (Join-Path $script:Dir 'report.md')
        @(Test-CtsSnapshot -Path $script:Dir) | Should -Contain 'missing file report.md'
    }

    It 'renders a report with the snapshot id and device table' {
        $report = ConvertTo-CtsSnapshotReport -Snapshot (New-SyntheticSnapshot)
        $report | Should -BeLike '*snap-20260101T000000Z-HOST1*'
        $report | Should -BeLike '*CTSREL-A*'
    }
}

Describe 'Compare-CtsSnapshot' {
    It 'reports no drift between identical snapshots' {
        @(Compare-CtsSnapshot -Reference (New-SyntheticSnapshot) -Difference (New-SyntheticSnapshot)) | Should -BeNullOrEmpty
    }

    It 'reports a changed value with both sides' {
        $b = New-SyntheticSnapshot -Mutate { param($s) $s.devices[1].inventory.sections.identity.data.computerName = 'RENAMED' }
        $drift = @(Compare-CtsSnapshot -Reference (New-SyntheticSnapshot) -Difference $b)
        $drift.Count | Should -Be 1
        $drift[0].DeviceId | Should -Be 'CTSREL-A'
        $drift[0].Change | Should -Be 'changed'
        $drift[0].Reference | Should -Be 'CTSREL-A'
        $drift[0].Difference | Should -Be 'RENAMED'
    }

    It 'reports only the inserted route, not every route after it' {
        $b = New-SyntheticSnapshot -Mutate {
            param($s)
            $r = $s.devices[0].inventory.sections.routes.data
            $r.routes = @([ordered]@{ interfaceAlias = 'vEthernet (ctsRel-Mgmt)'; destinationPrefix = '10.0.0.0/8'; nextHop = '192.168.250.1'; routeMetric = 1 }) + $r.routes
        }
        $drift = @(Compare-CtsSnapshot -Reference (New-SyntheticSnapshot) -Difference $b)
        @($drift | Where-Object Change -ne 'added') | Should -BeNullOrEmpty
        @($drift.Path | ForEach-Object { $_ -replace '\]\..*$', ']' } | Sort-Object -Unique) |
            Should -Be @('HOST1.sections.routes.data.routes[vEthernet (ctsRel-Mgmt)|10.0.0.0/8|192.168.250.1]')
    }

    It 'reports a removed section and a device status change' {
        $b = New-SyntheticSnapshot -Mutate { param($s) $s.devices[0].inventory.sections.Remove('identity'); $s.devices[2].status = 'partial'; $s.complete = $false }
        $drift = @(Compare-CtsSnapshot -Reference (New-SyntheticSnapshot) -Difference $b)
        $drift | Where-Object { $_.Path -like 'HOST1.sections.identity.*' -and $_.Change -eq 'removed' } | Should -Not -BeNullOrEmpty
        ($drift | Where-Object Path -eq 'CTSREL-B.status').Difference | Should -Be 'partial'
    }

    It 'ignores volatile values by default and honors -IgnorePath' {
        $b = New-SyntheticSnapshot -Mutate {
            param($s)
            $s.devices[0].inventory.sections.identity.data.lastBootUtc = '2026-02-02T00:00:00Z'
            $s.devices[0].inventory.sections.identity.capturedUtc = '2026-02-02T00:00:00Z'
            $s.devices[0].inventory.sections.routes.data.otherInterfaceRouteCounts[0].routeCount = 1600
        }
        $drift = @(Compare-CtsSnapshot -Reference (New-SyntheticSnapshot) -Difference $b)
        @($drift.Path) | Should -Be @('HOST1.sections.routes.data.otherInterfaceRouteCounts[Wi-Fi].routeCount')
        @(Compare-CtsSnapshot -Reference (New-SyntheticSnapshot) -Difference $b -IgnorePath '*.capturedUtc', '*.lastBootUtc', '*.otherInterfaceRouteCounts*') | Should -BeNullOrEmpty
    }
}

Describe 'New-CtsSnapshot -HostOnly (this machine)' -Tag 'Live' {
    BeforeAll {
        $script:Out = Join-Path $TestDrive 'snapshots'
        $script:First = New-CtsSnapshot -Definition $script:Definition -HostOnly -OutputRoot $script:Out
        Start-Sleep -Seconds 1
        $script:Second = New-CtsSnapshot -Definition $script:Definition -HostOnly -OutputRoot $script:Out
    }

    It 'writes snapshot.json, report.md, and manifest.json into a snapshot-id directory' {
        Split-Path -Leaf $script:First.Path | Should -Match '^snap-\d{8}T\d{6}Z-'
        @(Get-ChildItem -LiteralPath $script:First.Path -File).Name | Sort-Object | Should -Be @('manifest.json', 'report.md', 'snapshot.json')
    }

    It 'passes validation in memory and on disk' {
        @($script:First.ValidationErrors) | Should -BeNullOrEmpty
        @(Test-CtsSnapshot -Path $script:First.Path) | Should -BeNullOrEmpty
    }

    It 'records guests as notCaptured with a reason and the snapshot as incomplete' {
        $guests = @($script:First.Snapshot.devices | Where-Object role -eq 'guest')
        $guests.Count | Should -Be $script:Definition.vm.count
        foreach ($g in $guests) { $g.status | Should -Be 'notCaptured'; $g.reason | Should -Not -BeNullOrEmpty }
        $script:First.Snapshot.complete | Should -BeFalse
        $script:First.Snapshot.hostOnly | Should -BeTrue
    }

    It 'includes a hyperV section on the host (ok, or unavailable with a reason)' {
        $hv = ($script:First.Snapshot.devices | Where-Object role -eq 'host').inventory.sections.hyperV
        $hv.status | Should -BeIn 'ok', 'unavailable'
        if ($hv.status -eq 'unavailable') { $hv.reason | Should -Not -BeNullOrEmpty }
    }

    It 'shows no drift between two back-to-back snapshots of an unchanged host' {
        $drift = @(Compare-CtsSnapshot -Reference $script:First.Snapshot -Difference $script:Second.Snapshot)
        $drift | Should -BeNullOrEmpty -Because (($drift | ForEach-Object { "$($_.Path): $($_.Reference) -> $($_.Difference)" }) -join '; ')
    }
}
