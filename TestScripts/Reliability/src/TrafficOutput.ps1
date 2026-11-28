# Read captured ctsTraffic TCP output without starting a process or changing evidence.

function Get-CtsTrafficTcpSummary {
    <#
    .SYNOPSIS
    Finds the final TCP summary in a captured ctsTraffic stdout file.
    .DESCRIPTION
    Returns the summary for display and reports whether its expected lines are
    present. This does not classify the run or replace the original stdout
    file and exit code. Multiple banners and truncated summaries are incomplete.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)

    $reader = [System.IO.StreamReader]::new($Path, [System.Text.Encoding]::UTF8, $true)
    $lines = [System.Collections.Generic.List[string]]::new()
    $headerCount = 0
    $capturing = $false
    $sawEnd = $false
    $tooLarge = $false
    $characters = 0
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            if ($line -cmatch '^\s*Historic Connection Statistics \(all connections over the complete lifetime\)\s*$') {
                $headerCount++
                if ($headerCount -eq 1) { $capturing = $true }
            }

            if (-not $capturing) { continue }
            $characters += $line.Length + 2
            if ($characters -gt 65536) {
                $tooLarge = $true
                $capturing = $false
                continue
            }
            $lines.Add($line)
            if ($line -cmatch '^\s*Total Time\s*:\s*-?\d+\s+ms\.\s*$') {
                $sawEnd = $true
                $capturing = $false
            }
        }
    }
    finally {
        $reader.Dispose()
    }

    $summary = if ($headerCount -eq 1) { [string]::Join([Environment]::NewLine, $lines) } else { $null }
    $issue = if ($headerCount -eq 0) {
        'No historic connection summary found'
    }
    elseif ($headerCount -gt 1) {
        'Multiple historic connection summaries found'
    }
    elseif ($tooLarge) {
        'Historic connection summary exceeds the extraction limit'
    }
    elseif (-not $sawEnd) {
        'Historic connection summary is truncated'
    }
    else {
        $patterns = @(
            '(?m)^\s*SuccessfulConnections\s+\[-?\d+\]\s+NetworkErrors\s+\[-?\d+\]\s+ProtocolErrors\s+\[-?\d+\]\s*$'
            '(?m)^\s*Total Bytes Recv\s*:\s*-?\d+\s*$'
            '(?m)^\s*Total Bytes Sent\s*:\s*-?\d+\s*$'
        )
        if (@($patterns | Where-Object { [regex]::Matches($summary, $_).Count -ne 1 }).Count -gt 0) {
            'Historic TCP summary is missing or repeats expected lines'
        }
        else {
            $null
        }
    }

    [pscustomobject]@{
        Path       = $Path
        Found      = $headerCount -gt 0
        Complete   = $null -eq $issue
        Summary    = $summary
        Issue      = $issue
    }
}

function Get-CtsTrafficTcpConnectionResults {
    <#
    .SYNOPSIS
    Summarizes recorded results from a collected ctsTraffic TCP connection CSV.
    .DESCRIPTION
    Counts the result strings in the native TCP CSV. This does not establish
    that the file is complete, count all attempts, or classify a run outcome.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)

    $header = 'TimeSlice,LocalAddress,RemoteAddress,SendBytes,SendBps,RecvBytes,RecvBps,TimeMs,Result,ConnectionId'
    $results = [System.Collections.Generic.Dictionary[string, long]]::new([StringComparer]::Ordinal)
    $reader = [System.IO.StreamReader]::new($Path, [System.Text.Encoding]::UTF8, $true)
    $count = [long]0
    try {
        if ($reader.ReadLine() -cne $header) {
            throw 'Not a supported ctsTraffic TCP connection CSV header.'
        }
        $lineNumber = 1
        while ($null -ne ($line = $reader.ReadLine())) {
            $lineNumber++
            $fields = $line.Split(',')
            if ($fields.Count -ne 10 -or
                $fields[0] -cnotmatch '^\d+(\.\d+)?$' -or
                @($fields[3..7] | Where-Object { $_ -cnotmatch '^\d+$' }).Count -ne 0 -or
                [string]::IsNullOrWhiteSpace($fields[8])) {
                throw "Invalid TCP connection CSV record on line $lineNumber."
            }
            $result = $fields[8]
            if (-not $results.ContainsKey($result)) { $results[$result] = 0 }
            $results[$result]++
            $count++
        }
    }
    finally {
        $reader.Dispose()
    }

    [pscustomobject]@{
        Path          = $Path
        RecordedRows  = $count
        ResultCounts  = @($results.GetEnumerator() | Sort-Object Key | ForEach-Object {
                [pscustomobject]@{ Result = $_.Key; Count = $_.Value }
            })
    }
}

function Get-CtsTrafficTcpStatus {
    <#
    .SYNOPSIS
    Reads the last sample from a collected ctsTraffic TCP status CSV.
    .DESCRIPTION
    Rates are bytes per second for the interval; completed and error counts
    are cumulative. This does not establish file completeness or run outcome.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)

    $header = 'TimeSlice,SendBps,RecvBps,In-Flight,Completed,NetError,DataError'
    $reader = [System.IO.StreamReader]::new($Path, [System.Text.Encoding]::UTF8, $true)
    $count = [long]0
    $last = $null
    try {
        if ($reader.ReadLine() -cne $header) {
            throw 'Not a supported ctsTraffic TCP status CSV header.'
        }
        $lineNumber = 1
        while ($null -ne ($line = $reader.ReadLine())) {
            $lineNumber++
            $fields = $line.Split(',')
            $seconds = [decimal]0
            $values = [System.Collections.Generic.List[uint64]]::new()
            $valid = $fields.Count -eq 7 -and $fields[0] -cmatch '^\d+(\.\d+)?$' -and
                [decimal]::TryParse($fields[0], [Globalization.NumberStyles]::AllowDecimalPoint, [Globalization.CultureInfo]::InvariantCulture, [ref]$seconds)
            if ($valid) {
                foreach ($field in $fields[1..6]) {
                    $value = [uint64]0
                    if ($field -cnotmatch '^\d+$' -or
                        -not [uint64]::TryParse($field, [Globalization.NumberStyles]::None, [Globalization.CultureInfo]::InvariantCulture, [ref]$value)) {
                        $valid = $false
                        break
                    }
                    $values.Add($value)
                }
            }
            if (-not $valid) { throw "Invalid TCP status CSV record on line $lineNumber." }
            $last = [pscustomobject]@{
                TimeSliceSeconds = $seconds
                SendBytesPerSecond = $values[0]
                ReceiveBytesPerSecond = $values[1]
                InFlight = $values[2]
                Completed = $values[3]
                NetworkErrors = $values[4]
                DataErrors = $values[5]
            }
            $count++
        }
    }
    finally {
        $reader.Dispose()
    }
    [pscustomobject]@{ Path = $Path; RecordedSamples = $count; LastSample = $last }
}
