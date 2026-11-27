#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:ScenarioPath = Join-Path $PSScriptRoot '..\scenarios\g1b-clean-baseline.v1.json'
    $script:ScenarioJson = Get-Content -LiteralPath $script:ScenarioPath -Raw
}

Describe 'Test-CtsScenario' {
    It 'blocks the checked-in draft and reports unresolved safety values' {
        $errors = @(Test-CtsScenario -Path $script:ScenarioPath)
        $errors | Should -Contain 'scenario execution validation is incomplete; execution is not supported'
        $errors | Should -Contain 'safety.resourceBounds is unresolved'
        $errors | Should -Contain 'safety.stopConditions is unresolved'
        $errors | Should -Contain 'artifacts.paths is unresolved'
        @($errors | Where-Object { $_ -like 'workload.*Arguments must contain*' }) | Should -BeNullOrEmpty
    }

    It 'still blocks execution if a caller fills the known fields and changes the status' {
        $scenario = $script:ScenarioJson | ConvertFrom-Json
        $scenario.status = 'ready'
        $scenario.scope.requiredSnapshotFields = @('identity')
        $scenario.instrumentation.noiseFloorIntervalMs = 1000
        foreach ($property in $scenario.safety.PSObject.Properties) { $property.Value = 'filled' }
        $scenario.artifacts.paths = 'filled'
        $scenario.artifacts.completenessRules = 'filled'
        $errors = @(Test-CtsScenario -Scenario $scenario)
        $errors | Should -Contain 'scenario execution validation is incomplete; execution is not supported'
        $errors | Should -Not -Contain 'safety.resourceBounds is unresolved'
    }

    It 'reports unsafe verifier, fault, and verification settings' {
        $scenario = $script:ScenarioJson | ConvertFrom-Json
        $scenario.instrumentation.driverVerifier = 'enabled'
        $scenario.instrumentation.faultInjection = 'link-loss'
        $scenario.workload.verification = 'connection'
        $errors = @(Test-CtsScenario -Scenario $scenario)
        $errors | Should -Contain "instrumentation.driverVerifier must be 'disabled'"
        $errors | Should -Contain "instrumentation.faultInjection must be 'none'"
        $errors | Should -Contain "workload.verification must be 'data'"
    }

    It 'detects inconsistent workload settings and argument templates' {
        $scenario = $script:ScenarioJson | ConvertFrom-Json
        $scenario.workload.connections = 10
        $scenario.workload.serverArguments += '-Verify:connection'
        $scenario.workload.clientArguments = @($scenario.workload.clientArguments | Where-Object { $_ -notlike '-ConnectionFilename:*' })
        $errors = @(Test-CtsScenario -Scenario $scenario)
        $errors | Should -Contain "workload.clientArguments must contain exactly one '-Connections:10'"
        $errors | Should -Contain "workload.serverArguments must contain exactly one '-Verify:data'"
        $errors | Should -Contain "workload.clientArguments must contain exactly one '-ConnectionFilename:{clientConnectionCsv}'"
    }

    It 'reports invalid numeric values and mismatched sampling intervals' {
        $scenario = $script:ScenarioJson | ConvertFrom-Json
        $scenario.workload.connections = 0
        $scenario.workload.transferBytesPerConnection = 1.5
        $scenario.instrumentation.sampleIntervalMs = 1000
        $errors = @(Test-CtsScenario -Scenario $scenario)
        $errors | Should -Contain 'workload.connections must be a positive integer'
        $errors | Should -Contain 'workload.transferBytesPerConnection must be a positive integer'
        $errors | Should -Contain 'workload.statusUpdateMs must match instrumentation.sampleIntervalMs'
    }

    It 'treats empty safety values as unresolved' {
        $scenario = $script:ScenarioJson | ConvertFrom-Json
        $scenario.safety.resourceBounds = [pscustomobject]@{}
        $scenario.safety.stopConditions = @()
        $scenario.safety.monitorLossBehavior = '  '
        $errors = @(Test-CtsScenario -Scenario $scenario)
        $errors | Should -Contain 'safety.resourceBounds is unresolved'
        $errors | Should -Contain 'safety.stopConditions is unresolved'
        $errors | Should -Contain 'safety.monitorLossBehavior is unresolved'
    }

    It 'reports unsupported versions and missing nested settings without throwing' {
        $scenario = [pscustomobject]@{ schemaVersion = 2; scenarioId = 'other' }
        $errors = @(Test-CtsScenario -Scenario $scenario)
        $errors | Should -Contain "unsupported schemaVersion '2'"
        $errors | Should -Contain "unsupported scenarioId 'other'"
        $errors | Should -Contain 'safety.resourceBounds is unresolved'
        $errors | Should -Contain "instrumentation.applicationVerifier must be 'disabled'"
    }

    It 'rejects malformed JSON' {
        $path = Join-Path $TestDrive 'malformed.json'
        Set-Content -LiteralPath $path -Value '{'
        { Test-CtsScenario -Path $path } | Should -Throw
    }
}
