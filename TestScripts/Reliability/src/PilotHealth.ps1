# Read-only pilot observations; these helpers do not authorize or run a workload.

$script:CtsPilotGuestSampleScript = {
    $ErrorActionPreference = 'Stop'
    $os = Get-CimInstance Win32_OperatingSystem
    $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='C:'"
    if ($null -eq $os -or $null -eq $os.FreePhysicalMemory -or
        $null -eq $os.LastBootUpTime -or $null -eq $disk -or $null -eq $disk.FreeSpace) {
        throw 'Guest resource data is unavailable.'
    }
    [pscustomobject]@{
        Guest             = $env:COMPUTERNAME
        BootUtc           = $os.LastBootUpTime.ToUniversalTime().ToString('o')
        FreeMemoryBytes   = [long]$os.FreePhysicalMemory * 1024
        FreeDiskBytes     = [long]$disk.FreeSpace
    }
}

function Get-CtsPilotGuestSample {
    <#
    .SYNOPSIS
    Reads a guest's boot time and free memory and disk with a wait timeout.
    .DESCRIPTION
    Uses an existing guest session. A failed, incomplete, or timed-out probe
    throws; callers must treat the missing observation as monitor loss.
    This function does not implement a monitor loop or stop any workload.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][ValidateRange(1, 2147483647)][int]$TimeoutSeconds
    )

    Invoke-CtsLogged -Operation 'PilotGuestSample' -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
        $job = $null
        try {
            $job = Invoke-Command -Session $GuestSession.Session -ScriptBlock $script:CtsPilotGuestSampleScript -AsJob -ErrorAction Stop
            if ($null -eq (Wait-Job -Job $job -Timeout $TimeoutSeconds -ErrorAction Stop)) {
                throw "Guest resource probe timed out after $TimeoutSeconds seconds."
            }
            if ($job.State -ne 'Completed') {
                throw "Guest resource probe ended in state '$($job.State)'."
            }
            $samples = @(Receive-Job -Job $job -ErrorAction Stop)
            if ($samples.Count -ne 1) {
                throw 'Guest resource probe did not return exactly one sample.'
            }
            return $samples[0]
        }
        finally {
            if ($null -ne $job) {
                Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

function Test-CtsPilotGuestSample {
    <#
    .SYNOPSIS
    Reports missing or unsafe resource data in one guest sample.
    .DESCRIPTION
    Compares free memory and disk with caller-supplied minimums and checks
    that the guest has not rebooted. Empty output does not authorize a run.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Sample,
        [Parameter(Mandatory)][datetime]$ExpectedBootUtc,
        [Parameter(Mandatory)][ValidateRange(1, [long]::MaxValue)][long]$MinimumFreeMemoryBytes,
        [Parameter(Mandatory)][ValidateRange(1, [long]::MaxValue)][long]$MinimumFreeDiskBytes
    )

    if ($ExpectedBootUtc.Kind -ne [DateTimeKind]::Utc) {
        throw 'ExpectedBootUtc must be a UTC DateTime.'
    }
    $boot = Get-CtsProp $Sample 'BootUtc'
    $parsedBoot = [DateTimeOffset]::MinValue
    if ($boot -isnot [string] -or $boot -notmatch 'Z$' -or
        -not [DateTimeOffset]::TryParse($boot, [ref]$parsedBoot)) {
        'Guest boot time is missing or invalid'
    }
    elseif ($parsedBoot.UtcDateTime -ne $ExpectedBootUtc) {
        'Guest boot time changed'
    }

    foreach ($check in @(
            @{ name = 'FreeMemoryBytes'; minimum = $MinimumFreeMemoryBytes }
            @{ name = 'FreeDiskBytes'; minimum = $MinimumFreeDiskBytes }
        )) {
        $value = Get-CtsProp $Sample $check.name
        if (($value -isnot [int] -and $value -isnot [long]) -or $value -lt 0) {
            "Guest $($check.name) is missing or invalid"
        }
        elseif ($value -lt $check.minimum) {
            "Guest $($check.name) is below the minimum"
        }
    }
}
