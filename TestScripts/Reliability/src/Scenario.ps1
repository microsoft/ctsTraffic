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
            @{ name = 'workload.protocol'; value = (Get-CtsProp $workload 'protocol'); expected = 'tcp' }
            @{ name = 'workload.io'; value = (Get-CtsProp $workload 'io'); expected = 'iocp' }
            @{ name = 'workload.pattern'; value = (Get-CtsProp $workload 'pattern'); expected = 'duplex' }
            @{ name = 'instrumentation.applicationVerifier'; value = (Get-CtsProp $instrumentation 'applicationVerifier'); expected = 'disabled' }
            @{ name = 'instrumentation.driverVerifier'; value = (Get-CtsProp $instrumentation 'driverVerifier'); expected = 'disabled' }
            @{ name = 'instrumentation.faultInjection'; value = (Get-CtsProp $instrumentation 'faultInjection'); expected = 'none' }
        )) {
        if ($check.value -ne $check.expected) { "$($check.name) must be '$($check.expected)'" }
    }

    foreach ($name in 'connections', 'transferBytesPerConnection', 'measuredTimeLimitMs', 'pilotTimeLimitMs', 'warmupIterations', 'measuredIterations', 'statusUpdateMs') {
        $value = Get-CtsProp $workload $name
        if (($value -isnot [int] -and $value -isnot [long]) -or $value -le 0) {
            "workload.$name must be a positive integer"
        }
    }

    $interval = Get-CtsProp $instrumentation 'sampleIntervalMs'
    if (($interval -isnot [int] -and $interval -isnot [long]) -or $interval -le 0) {
        'instrumentation.sampleIntervalMs must be a positive integer'
    }
    elseif ($interval -ne (Get-CtsProp $workload 'statusUpdateMs')) {
        'workload.statusUpdateMs must match instrumentation.sampleIntervalMs'
    }

    $argumentChecks = @(
        @{ role = 'server'; option = 'Listen'; value = '{serverAddress}' }
        @{ role = 'client'; option = 'Target'; value = '{serverAddress}' }
        @{ role = 'client'; option = 'Bind'; value = '{clientAddress}' }
        @{ role = 'client'; option = 'Connections'; value = (Get-CtsProp $workload 'connections') }
        @{ role = 'client'; option = 'TimeLimit'; value = '{timeLimitMs}' }
        @{ role = 'server'; option = 'ConnectionFilename'; value = '{serverConnectionCsv}' }
        @{ role = 'client'; option = 'ConnectionFilename'; value = '{clientConnectionCsv}' }
        @{ role = 'server'; option = 'StatusFilename'; value = '{serverStatusCsv}' }
        @{ role = 'client'; option = 'StatusFilename'; value = '{clientStatusCsv}' }
    )
    foreach ($role in 'server', 'client') {
        $argumentChecks += @{ role = $role; option = 'Pattern'; value = 'duplex' }
        $argumentChecks += @{ role = $role; option = 'IO'; value = 'iocp' }
        $argumentChecks += @{ role = $role; option = 'Transfer'; value = (Get-CtsProp $workload 'transferBytesPerConnection') }
        $argumentChecks += @{ role = $role; option = 'Verify'; value = 'data' }
        $argumentChecks += @{ role = $role; option = 'StatusUpdate'; value = (Get-CtsProp $workload 'statusUpdateMs') }
    }
    foreach ($check in $argumentChecks) {
        $arguments = @(Get-CtsProp $workload "$($check.role)Arguments")
        $prefix = '-{0}:' -f $check.option
        $matching = @($arguments | Where-Object { $_ -is [string] -and $_.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
        $expected = '{0}{1}' -f $prefix, $check.value
        if ($matching.Count -ne 1 -or $matching[0] -ine $expected) {
            "workload.$($check.role)Arguments must contain exactly one '$expected'"
        }
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
