# Reserves corresponding empty directories; this does not authorize or start a workload.

function New-CtsPilotHostRunDirectory {
    param([Parameter(Mandatory)][string]$RootPath, [Parameter(Mandatory)][string]$Token)
    & $script:CtsPilotDirectoryScript $RootPath $Token
}

function New-CtsPilotRunLayout {
    param(
        [Parameter(Mandatory)]$Server,
        [Parameter(Mandatory)]$Client,
        [Parameter(Mandatory)][string]$RootPath,
        [Parameter(Mandatory)][string]$Token
    )
    Assert-CtsPilotGuardianDirectory -Directory $RootPath
    if ($Token -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid pilot run token.' }
    foreach ($guest in @($Server, $Client)) {
        $target = Get-CtsProp $guest 'Target'
        if ($target -isnot [string] -or [string]::IsNullOrWhiteSpace($target) -or
            (Get-CtsProp $guest 'Channel') -cne 'Direct' -or
            $null -eq (Get-CtsProp $guest 'Session')) {
            throw 'Pilot layout requires two PowerShell Direct guest sessions with targets.'
        }
    }
    if ($Server.Target -ieq $Client.Target -or [object]::ReferenceEquals($Server.Session, $Client.Session)) {
        throw 'Pilot layout guests must be distinct.'
    }

    $path = [IO.Path]::Combine($RootPath, "pilot-$Token")
    $reservations = @(
        [pscustomobject]@{ Endpoint = 'Host'; Target = $env:COMPUTERNAME; Path = $path; Outcome = 'NotAttempted'; Error = $null }
        [pscustomobject]@{ Endpoint = 'Server'; Target = $Server.Target; Path = $path; Outcome = 'NotAttempted'; Error = $null }
        [pscustomobject]@{ Endpoint = 'Client'; Target = $Client.Target; Path = $path; Outcome = 'NotAttempted'; Error = $null }
    )
    foreach ($reservation in $reservations) {
        try {
            $actual = @(
                if ($reservation.Endpoint -eq 'Host') {
                    New-CtsPilotHostRunDirectory -RootPath $RootPath -Token $Token
                }
                else {
                    $guest = if ($reservation.Endpoint -eq 'Server') { $Server } else { $Client }
                    New-CtsPilotRunDirectory -GuestSession $guest -RootPath $RootPath -Token $Token
                }
            )
            if ($actual.Count -ne 1 -or $actual[0] -isnot [string] -or $actual[0] -ine $path) {
                throw 'Reservation did not return the expected directory.'
            }
            $reservation.Outcome = 'Reserved'
        }
        catch {
            $reservation.Outcome = 'Unknown'
            $reservation.Error = $_.Exception.Message
            break
        }
    }
    [pscustomobject]@{
        Outcome = if (@($reservations | Where-Object Outcome -ne 'Reserved').Count -eq 0) { 'Reserved' } else { 'Incomplete' }
        Token = $Token; RootPath = $RootPath; Reservations = $reservations
    }
}
