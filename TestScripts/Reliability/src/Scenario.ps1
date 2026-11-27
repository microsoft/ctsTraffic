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

    $snapshotCount = Get-CtsProp $instrumentation 'noiseFloorSnapshotCount'
    if (($snapshotCount -isnot [int] -and $snapshotCount -isnot [long]) -or $snapshotCount -le 0) {
        'instrumentation.noiseFloorSnapshotCount must be a positive integer'
    }
    $noiseInterval = Get-CtsProp $instrumentation 'noiseFloorIntervalMs'
    if ($null -ne $noiseInterval -and
        (($noiseInterval -isnot [int] -and $noiseInterval -isnot [long]) -or $noiseInterval -le 0)) {
        'instrumentation.noiseFloorIntervalMs must be a positive integer'
    }
    if ((Get-CtsProp $workload 'persistentServer') -isnot [bool] -or
        -not (Get-CtsProp $workload 'persistentServer')) {
        'workload.persistentServer must be true'
    }
    $iterations = Get-CtsProp $workload 'measuredIterations'
    if (($iterations -is [int] -or $iterations -is [long]) -and $iterations -gt 0) {
        $snapshots = @(Get-CtsProp $instrumentation 'leakSnapshots')
        $validSnapshots = $snapshots.Count -eq ($iterations + 1)
        if ($validSnapshots) {
            for ($i = 0; $i -lt $snapshots.Count; $i++) {
                if ($snapshots[$i] -cne "S$i") { $validSnapshots = $false; break }
            }
        }
        if (-not $validSnapshots) {
            'instrumentation.leakSnapshots must contain S0 through S<measuredIterations> in order'
        }
    }

    $quiescence = Get-CtsProp $instrumentation 'quiescence'
    if ((Get-CtsProp $quiescence 'clientsExited') -isnot [bool] -or
        -not (Get-CtsProp $quiescence 'clientsExited')) {
        'instrumentation.quiescence.clientsExited must be true'
    }
    $connectionsRemaining = Get-CtsProp $quiescence 'testPortConnectionsRemaining'
    if (($connectionsRemaining -isnot [int] -and $connectionsRemaining -isnot [long]) -or $connectionsRemaining -ne 0) {
        'instrumentation.quiescence.testPortConnectionsRemaining must be zero'
    }
    $cpuLimit = Get-CtsProp $quiescence 'cpuBelowPercent'
    if (($cpuLimit -isnot [int] -and $cpuLimit -isnot [long] -and $cpuLimit -isnot [double] -and $cpuLimit -isnot [decimal]) -or
        [double]::IsNaN([double]$cpuLimit) -or [double]::IsInfinity([double]$cpuLimit) -or
        $cpuLimit -le 0 -or $cpuLimit -gt 100) {
        'instrumentation.quiescence.cpuBelowPercent must be greater than zero and at most 100'
    }
    $settle = Get-CtsProp $quiescence 'settleSeconds'
    $timeout = Get-CtsProp $quiescence 'timeoutSeconds'
    foreach ($check in @(@{ name = 'settleSeconds'; value = $settle }, @{ name = 'timeoutSeconds'; value = $timeout })) {
        if (($check.value -isnot [int] -and $check.value -isnot [long]) -or $check.value -le 0) {
            "instrumentation.quiescence.$($check.name) must be a positive integer"
        }
    }
    if (($settle -is [int] -or $settle -is [long]) -and ($timeout -is [int] -or $timeout -is [long]) -and
        $settle -gt 0 -and $timeout -gt 0 -and $timeout -lt $settle) {
        'instrumentation.quiescence.timeoutSeconds must be at least settleSeconds'
    }
    if ((Get-CtsProp $quiescence 'onTimeout') -ne 'capture-not-quiescent-and-mark-leak-analysis-inconclusive') {
        'instrumentation.quiescence.onTimeout must capture and mark the leak analysis inconclusive'
    }

    $measurements = @(Get-CtsProp $Scenario 'measurements')
    $expectedIds = @(0..8 | ForEach-Object { 'G1B-{0:00}' -f $_ })
    foreach ($expectedId in $expectedIds) {
        $matchingMeasurements = @($measurements | Where-Object { (Get-CtsProp $_ 'id') -eq $expectedId })
        if ($matchingMeasurements.Count -ne 1) {
            "measurements must contain exactly one '$expectedId'"
            continue
        }
        $measurement = $matchingMeasurements[0]
        foreach ($field in 'source', 'artifactPath', 'missingDataBehavior') {
            $value = Get-CtsProp $measurement $field
            if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) {
                "measurements.$expectedId.$field is unresolved"
            }
        }
        foreach ($field in 'units', 'capturePoints') {
            $values = @(Get-CtsProp $measurement $field)
            if ($values.Count -eq 0 -or @($values | Where-Object { $_ -isnot [string] -or [string]::IsNullOrWhiteSpace($_) }).Count -gt 0) {
                "measurements.$expectedId.$field must contain nonempty strings"
            }
        }
    }
    foreach ($measurement in $measurements) {
        $measurementId = Get-CtsProp $measurement 'id'
        if ($measurementId -notin $expectedIds) { "unsupported measurement id '$measurementId'" }
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
