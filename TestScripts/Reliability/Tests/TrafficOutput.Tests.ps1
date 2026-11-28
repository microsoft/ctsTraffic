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

Describe 'Get-CtsTrafficTcpConnectionResults' {
    BeforeAll {
        $script:ConnectionHeader = 'TimeSlice,LocalAddress,RemoteAddress,SendBytes,SendBps,RecvBytes,RecvBps,TimeMs,Result,ConnectionId'
    }

    It 'counts recorded success and error results from a UTF-16 TCP CSV' {
        $path = Join-Path $TestDrive 'connections.csv'
        [System.IO.File]::WriteAllLines($path, @(
                $script:ConnectionHeader
                '1.000,192.168.1.1:4444,192.168.1.2:50000,10,20,10,20,1000,Succeeded,id1'
                '2.000,192.168.1.1:4444,192.168.1.2:50001,0,0,0,0,0,10054: reset,id2'
                '3.000,192.168.1.1:4444,192.168.1.2:50002,10,20,10,20,1000,Succeeded,id3'
            ), [System.Text.Encoding]::Unicode)
        $summary = Get-CtsTrafficTcpConnectionResults -Path $path
        $summary.RecordedRows | Should -Be 3
        @($summary.ResultCounts | Where-Object Result -eq 'Succeeded')[0].Count | Should -Be 2
        @($summary.ResultCounts | Where-Object Result -eq '10054: reset')[0].Count | Should -Be 1
    }

    It 'reports no recorded results for a header-only file' {
        $path = Write-OutputFixture 'empty-connections.csv' @($script:ConnectionHeader)
        $summary = Get-CtsTrafficTcpConnectionResults -Path $path
        $summary.RecordedRows | Should -Be 0
        $summary.ResultCounts | Should -BeNullOrEmpty
    }

    It 'rejects incompatible headers and malformed or truncated records' {
        $badHeader = Write-OutputFixture 'wrong-header.csv' @('not a TCP CSV')
        { Get-CtsTrafficTcpConnectionResults -Path $badHeader } | Should -Throw '*header*'
        $truncated = Write-OutputFixture 'truncated-row.csv' @($script:ConnectionHeader, '1.000,a,b,1,2')
        { Get-CtsTrafficTcpConnectionResults -Path $truncated } | Should -Throw '*line 2*'
        $invalid = Write-OutputFixture 'invalid-row.csv' @($script:ConnectionHeader, '1.000,a,b,NaN,2,1,2,100,Succeeded,id')
        { Get-CtsTrafficTcpConnectionResults -Path $invalid } | Should -Throw '*line 2*'
        { Get-CtsTrafficTcpConnectionResults -Path (Join-Path $TestDrive 'missing.csv') } | Should -Throw
    }
}

Describe 'Get-CtsTrafficTcpStatus' {
    BeforeAll {
        $script:StatusHeader = 'TimeSlice,SendBps,RecvBps,In-Flight,Completed,NetError,DataError'
    }

    It 'reads the last sample and its cumulative counters from a UTF-16 status CSV' {
        $path = Join-Path $TestDrive 'status.csv'
        [IO.File]::WriteAllLines($path, @($script:StatusHeader, '1.000,20,30,8,2,1,0', '6.250,40,50,4,7,2,1'), [Text.Encoding]::Unicode)
        $result = Get-CtsTrafficTcpStatus -Path $path
        $result.RecordedSamples | Should -Be 2
        $result.LastSample.TimeSliceSeconds | Should -Be 6.25
        $result.LastSample.SendBytesPerSecond | Should -Be 40
        $result.LastSample.ReceiveBytesPerSecond | Should -Be 50
        $result.LastSample.InFlight | Should -Be 4
        $result.LastSample.Completed | Should -Be 7
        $result.LastSample.NetworkErrors | Should -Be 2
        $result.LastSample.DataErrors | Should -Be 1
    }

    It 'reports an empty status file without inventing a sample' {
        $path = Write-OutputFixture 'empty-status.csv' @($script:StatusHeader)
        $result = Get-CtsTrafficTcpStatus -Path $path
        $result.RecordedSamples | Should -Be 0
        $result.LastSample | Should -BeNullOrEmpty
    }

    It 'rejects incompatible, truncated, and invalid records' {
        { Get-CtsTrafficTcpStatus -Path (Write-OutputFixture 'bad-status-header.csv' @('other')) } | Should -Throw '*header*'
        foreach ($row in @('1.000,1,2', '1.000,1,2,3,4,5,-1', '1.000,1,2,3,4,5,18446744073709551616', '1x,1,2,3,4,5,6')) {
            { Get-CtsTrafficTcpStatus -Path (Write-OutputFixture 'bad-status.csv' @($script:StatusHeader, $row)) } | Should -Throw '*line 2*'
        }
        { Get-CtsTrafficTcpStatus -Path (Join-Path $TestDrive 'missing-status.csv') } | Should -Throw
    }
}
