#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Banner = @(
        '  Historic Connection Statistics (all connections over the complete lifetime)  '
        '-------------------------------------------------------------------------------'
        '  SuccessfulConnections [12]   NetworkErrors [1]   ProtocolErrors [0]'
        ''
        '  Total Bytes Recv : 100'
        '  Total Bytes Sent : 200'
        '  Total Time : 300 ms.'
    )
    function Write-OutputFixture([string]$Name, [string[]]$Lines) {
        $path = Join-Path $TestDrive $Name
        [System.IO.File]::WriteAllLines($path, $Lines)
        return $path
    }
}

Describe 'Get-CtsTrafficTcpSummary' {
    It 'extracts a complete TCP summary without classifying the run' {
        $path = Write-OutputFixture 'complete.txt' (@('earlier status') + $script:Banner + @('later output'))
        $result = Get-CtsTrafficTcpSummary -Path $path
        $result.Found | Should -BeTrue
        $result.Complete | Should -BeTrue
        $result.Issue | Should -BeNullOrEmpty
        $result.Summary | Should -Be ([string]::Join([Environment]::NewLine, $script:Banner))
    }

    It 'reports absent and truncated summaries' {
        $absent = Get-CtsTrafficTcpSummary -Path (Write-OutputFixture 'absent.txt' @('TCP usage options'))
        $absent.Found | Should -BeFalse
        $absent.Complete | Should -BeFalse
        $absent.Issue | Should -Be 'No historic connection summary found'
        $partial = Get-CtsTrafficTcpSummary -Path (Write-OutputFixture 'partial.txt' $script:Banner[0..4])
        $partial.Found | Should -BeTrue
        $partial.Complete | Should -BeFalse
        $partial.Issue | Should -Be 'Historic connection summary is truncated'
    }

    It 'does not treat a summary missing a TCP total as complete' {
        $lines = @($script:Banner | Where-Object { $_ -notlike '*Total Bytes Sent*' })
        $result = Get-CtsTrafficTcpSummary -Path (Write-OutputFixture 'missing-total.txt' $lines)
        $result.Found | Should -BeTrue
        $result.Complete | Should -BeFalse
        $result.Issue | Should -Be 'Historic TCP summary is missing or repeats expected lines'
    }

    It 'rejects multiple banners rather than selecting one' {
        $result = Get-CtsTrafficTcpSummary -Path (Write-OutputFixture 'multiple.txt' ($script:Banner + $script:Banner))
        $result.Found | Should -BeTrue
        $result.Complete | Should -BeFalse
        $result.Summary | Should -BeNullOrEmpty
        $result.Issue | Should -Be 'Multiple historic connection summaries found'
    }

    It 'limits captured summary size and reports missing files' {
        $result = Get-CtsTrafficTcpSummary -Path (Write-OutputFixture 'large.txt' @($script:Banner[0], ('x' * 65536)))
        $result.Complete | Should -BeFalse
        $result.Issue | Should -Be 'Historic connection summary exceeds the extraction limit'
        { Get-CtsTrafficTcpSummary -Path (Join-Path $TestDrive 'missing.txt') } | Should -Throw
    }
}
