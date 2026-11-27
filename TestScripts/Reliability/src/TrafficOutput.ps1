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
