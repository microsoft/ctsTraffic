# Host-to-guest transport layer: PowerShell Direct (VMBus) or WinRM (management network).
# Every operation is recorded as one JSON line in the run/setup log.

function Write-CtsLogEntry {
    [CmdletBinding()]
    param(
        [string]$LogPath,
        [Parameter(Mandatory)][System.Collections.IDictionary]$Entry
    )
    if ([string]::IsNullOrEmpty($LogPath)) { return }
    $directory = Split-Path -Parent $LogPath
    if ($directory -and -not (Test-Path -LiteralPath $directory)) { $null = New-Item -ItemType Directory -Path $directory -Force }
    $line = [pscustomobject]$Entry | ConvertTo-Json -Compress -Depth 5
    [System.IO.File]::AppendAllText($LogPath, $line + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
}

function Invoke-CtsLogged {
    param(
        [Parameter(Mandatory)][string]$Operation,
        [Parameter(Mandatory)][string]$Channel,
        [Parameter(Mandatory)][string]$Target,
        [string]$LogPath,
        [Parameter(Mandatory)][scriptblock]$Action
    )
    $start = [DateTime]::UtcNow
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $success = $false
    $errorText = $null
    try {
        $result = & $Action
        $success = $true
        return $result
    }
    catch {
        $errorText = $_.Exception.Message
        throw
    }
    finally {
        $stopwatch.Stop()
        Write-CtsLogEntry -LogPath $LogPath -Entry ([ordered]@{
                startUtc   = $start.ToString('o')
                endUtc     = [DateTime]::UtcNow.ToString('o')
                durationMs = [int]$stopwatch.ElapsedMilliseconds
                channel    = $Channel
                target     = $Target
                operation  = $Operation
                success    = $success
                error      = $errorText
            })
    }
}

function New-CtsGuestSession {
    <#
    .SYNOPSIS
    Opens a PowerShell session to a guest over PowerShell Direct or WinRM.
    .PARAMETER ComputerName
    The guest management address; required for WinRM.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$VMName,
        [Parameter(Mandatory)][ValidateSet('Direct', 'WinRM')][string]$Channel,
        [Parameter(Mandatory)][pscredential]$Credential,
        [string]$ComputerName,
        [string]$LogPath
    )
    if ($Channel -eq 'WinRM' -and [string]::IsNullOrEmpty($ComputerName)) {
        throw 'ComputerName (guest management address) is required for the WinRM channel.'
    }
    $target = if ($Channel -eq 'Direct') { $VMName } else { $ComputerName }
    $session = Invoke-CtsLogged -Operation 'OpenSession' -Channel $Channel -Target $target -LogPath $LogPath -Action {
        if ($Channel -eq 'Direct') {
            New-PSSession -VMName $VMName -Credential $Credential -ErrorAction Stop
        }
        else {
            New-PSSession -ComputerName $ComputerName -Credential $Credential -Authentication Negotiate -ErrorAction Stop
        }
    }
    return [pscustomobject]@{
        Channel = $Channel
        VMName  = $VMName
        Target  = $target
        Session = $session
        LogPath = $LogPath
    }
}

function Invoke-CtsGuestCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][scriptblock]$ScriptBlock,
        [object[]]$ArgumentList = @(),
        [string]$Operation = 'Invoke'
    )
    Invoke-CtsLogged -Operation $Operation -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
        Invoke-Command -Session $GuestSession.Session -ScriptBlock $ScriptBlock -ArgumentList $ArgumentList -ErrorAction Stop
    }
}

function Copy-CtsToGuest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Destination
    )
    Invoke-CtsLogged -Operation "CopyTo:$Destination" -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
        Invoke-Command -Session $GuestSession.Session -ScriptBlock {
            param($d) if (-not (Test-Path -LiteralPath $d)) { $null = New-Item -ItemType Directory -Path $d -Force }
        } -ArgumentList $Destination -ErrorAction Stop
        Copy-Item -LiteralPath $Path -Destination $Destination -ToSession $GuestSession.Session -Recurse -Force -ErrorAction Stop
    }
}

function Copy-CtsFromGuest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Destination
    )
    Invoke-CtsLogged -Operation "CopyFrom:$Path" -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
        if (-not (Test-Path -LiteralPath $Destination)) { $null = New-Item -ItemType Directory -Path $Destination -Force }
        Copy-Item -Path $Path -Destination $Destination -FromSession $GuestSession.Session -Recurse -Force -ErrorAction Stop
    }
}

function Wait-CtsGuestReady {
    <#
    .SYNOPSIS
    Waits until a guest answers a PowerShell Direct command, or throws after the timeout.
    #>
    param(
        [Parameter(Mandatory)][string]$VMName,
        [Parameter(Mandatory)][pscredential]$Credential,
        [int]$TimeoutSeconds = 1800,
        [int]$PollSeconds = 15,
        [string]$LogPath
    )
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $lastError = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $null = Invoke-Command -VMName $VMName -Credential $Credential -ScriptBlock { $env:COMPUTERNAME } -ErrorAction Stop
            Write-CtsLogEntry -LogPath $LogPath -Entry ([ordered]@{ startUtc = [DateTime]::UtcNow.ToString('o'); channel = 'Direct'; target = $VMName; operation = 'GuestReady'; success = $true })
            return
        }
        catch { $lastError = $_.Exception.Message }
        Start-Sleep -Seconds $PollSeconds
    }
    throw "Guest '$VMName' did not become reachable over PowerShell Direct within $TimeoutSeconds seconds. Last error: $lastError"
}
