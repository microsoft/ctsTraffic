# Preliminary scenario checks. Execution remains blocked until the complete contract is implemented.

function Test-CtsScenario {
    <#
    .SYNOPSIS
    Reports preliminary scenario blockers.
    .DESCRIPTION
    Reads a scenario file or object and reports known problems. This incomplete
    check always reports that execution is unsupported; it is not a substitute
    for the full scenario, run-definition, and preflight validation.
    #>
    [CmdletBinding(DefaultParameterSetName = 'Object')]
    param(
        [Parameter(Mandatory, ParameterSetName = 'Object')]$Scenario,
        [Parameter(Mandatory, ParameterSetName = 'Path')][string]$Path
    )

    if ($PSCmdlet.ParameterSetName -eq 'Path') {
        $Scenario = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    }

    'scenario execution validation is incomplete; execution is not supported'

    $version = Get-CtsProp $Scenario 'schemaVersion'
    if ($version -ne 1) { "unsupported schemaVersion '$version'" }
    $id = Get-CtsProp $Scenario 'scenarioId'
    if ($id -ne 'G1B-CLEAN-BASELINE') { "unsupported scenarioId '$id'" }

    $scope = Get-CtsProp $Scenario 'scope'
    $workload = Get-CtsProp $Scenario 'workload'
    $instrumentation = Get-CtsProp $Scenario 'instrumentation'
    $safety = Get-CtsProp $Scenario 'safety'
    $artifacts = Get-CtsProp $Scenario 'artifacts'

    foreach ($check in @(
            @{ name = 'workload.verification'; value = (Get-CtsProp $workload 'verification'); expected = 'data' }
            @{ name = 'instrumentation.applicationVerifier'; value = (Get-CtsProp $instrumentation 'applicationVerifier'); expected = 'disabled' }
            @{ name = 'instrumentation.driverVerifier'; value = (Get-CtsProp $instrumentation 'driverVerifier'); expected = 'disabled' }
            @{ name = 'instrumentation.faultInjection'; value = (Get-CtsProp $instrumentation 'faultInjection'); expected = 'none' }
        )) {
        if ($check.value -ne $check.expected) { "$($check.name) must be '$($check.expected)'" }
    }

    foreach ($check in @(
            @{ name = 'scope.requiredSnapshotFields'; value = (Get-CtsProp $scope 'requiredSnapshotFields') }
            @{ name = 'instrumentation.noiseFloorIntervalMs'; value = (Get-CtsProp $instrumentation 'noiseFloorIntervalMs') }
            @{ name = 'safety.resourceBounds'; value = (Get-CtsProp $safety 'resourceBounds') }
            @{ name = 'safety.stopConditions'; value = (Get-CtsProp $safety 'stopConditions') }
            @{ name = 'safety.monitorLossBehavior'; value = (Get-CtsProp $safety 'monitorLossBehavior') }
            @{ name = 'safety.cleanupAndRecovery'; value = (Get-CtsProp $safety 'cleanupAndRecovery') }
            @{ name = 'safety.allowedOperatorActions'; value = (Get-CtsProp $safety 'allowedOperatorActions') }
            @{ name = 'artifacts.paths'; value = (Get-CtsProp $artifacts 'paths') }
            @{ name = 'artifacts.completenessRules'; value = (Get-CtsProp $artifacts 'completenessRules') }
        )) {
        $value = $check.value
        if ($null -eq $value -or
            ($value -is [string] -and [string]::IsNullOrWhiteSpace($value)) -or
            ($value -is [System.Collections.ICollection] -and $value.Count -eq 0) -or
            ($value -is [pscustomobject] -and @($value.PSObject.Properties).Count -eq 0)) {
            "$($check.name) is unresolved"
        }
    }
}
