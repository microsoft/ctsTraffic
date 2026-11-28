#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

Describe 'reliability test runner' {
    It 'fails when Pester cannot discover a test file' {
        $fixture = Join-Path $TestDrive 'invalid.Tests.ps1'
        [IO.File]::WriteAllText($fixture, "Describe 'invalid' {")
        $runner = Join-Path $PSScriptRoot 'Invoke-CtsReliabilityTests.ps1'
        $edition = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh' } else { 'powershell' }
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $edition -NoProfile -NonInteractive -File $runner -Edition $edition -Path $fixture `
                -OutputDirectory (Join-Path $TestDrive 'results') *> $null
            $exitCode = $LASTEXITCODE
        }
        finally { $ErrorActionPreference = $previousPreference }
        $exitCode | Should -Be 1
        $summary = Get-ChildItem -LiteralPath (Join-Path $TestDrive 'results') -Recurse -Filter "$edition.summary.json"
        $report = Get-Content -LiteralPath $summary.FullName -Raw | ConvertFrom-Json
        $report.failedTests | Should -Contain 'Pester run result: Failed; failed containers: 1; failed blocks: 0'
    }
}
