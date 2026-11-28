# Read-only network evidence; it does not authorize or send traffic.

$script:CtsPilotNetworkScript = {
    param($Role, $ServerAddress)
    $ErrorActionPreference = 'Stop'
    $route = $null
    if ($Role -eq 'Server') {
        $addresses = @(Get-NetIPAddress -AddressFamily IPv4 -IPAddress $ServerAddress -ErrorAction Stop)
        if ($addresses.Count -ne 1) { throw 'Server address did not resolve to exactly one interface.' }
        $address = $addresses[0]
    }
    elseif ($Role -eq 'Client') {
        $results = @(Find-NetRoute -RemoteIPAddress $ServerAddress -ErrorAction Stop)
        $addresses = @($results | Where-Object { $_.CimClass.CimClassName -eq 'MSFT_NetIPAddress' })
        $routes = @($results | Where-Object { $_.CimClass.CimClassName -eq 'MSFT_NetRoute' })
        if ($results.Count -ne 2 -or $addresses.Count -ne 1 -or $routes.Count -ne 1) {
            throw 'Client route selection did not return one address and one route.'
        }
        $address = $addresses[0]
        $route = $routes[0]
        if ($address.InterfaceIndex -ne $route.InterfaceIndex) { throw 'Selected source and route interfaces do not match.' }
    }
    else { throw 'Invalid pilot network role.' }

    $adapter = Get-NetAdapter -InterfaceIndex $address.InterfaceIndex -ErrorAction Stop
    if (@($adapter).Count -ne 1 -or $adapter.ifIndex -ne $address.InterfaceIndex) {
        throw 'Selected interface did not resolve to exactly one adapter.'
    }
    [pscustomobject]@{
        Role = $Role
        Guest = $env:COMPUTERNAME
        ObservedUtc = [DateTime]::UtcNow.ToString('o')
        ServerAddress = $ServerAddress
        SelectedAddress = [string]$address.IPAddress
        AddressState = [string]$address.AddressState
        InterfaceIndex = [int]$address.InterfaceIndex
        InterfaceAlias = [string]$address.InterfaceAlias
        AdapterStatus = [string]$adapter.Status
        RoutePrefix = if ($null -ne $route) { [string]$route.DestinationPrefix } else { $null }
        NextHop = if ($null -ne $route) { [string]$route.NextHop } else { $null }
        RouteMetric = if ($null -ne $route) { [int]$route.RouteMetric } else { $null }
    }
}

function Get-CtsPilotNetworkSample {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)][string]$ServerAddress,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    Assert-CtsPilotListenerAddress -Address $ServerAddress
    try {
        $samples = @(Invoke-CtsLogged -Operation 'PilotNetwork:Sample' -Channel $GuestSession.Channel -Target $GuestSession.Target -LogPath $GuestSession.LogPath -Action {
            $job = $null
            try {
                $job = Invoke-Command -Session $GuestSession.Session -ScriptBlock $script:CtsPilotNetworkScript `
                    -ArgumentList @($Role, $ServerAddress) -AsJob -ErrorAction Stop
                if ($null -eq (Wait-Job -Job $job -Timeout $TimeoutSeconds -ErrorAction Stop)) { throw 'Network sample wait timed out.' }
                if ($job.State -ne 'Completed') { throw "Network sample job ended in state '$($job.State)'." }
                Receive-Job -Job $job -ErrorAction Stop
            }
            finally { if ($null -ne $job) { Remove-Job -Job $job -Force -ErrorAction SilentlyContinue } }
        })
        if ($samples.Count -ne 1 -or (Get-CtsProp $samples[0] 'Role') -ine $Role -or
            (Get-CtsProp $samples[0] 'ServerAddress') -cne $ServerAddress) {
            throw 'Network sample did not return the expected endpoint.'
        }
        return $samples[0]
    }
    catch { throw "Pilot network sample is unknown: $($_.Exception.Message)" }
}

function Test-CtsPilotNetworkSample {
    param(
        [Parameter(Mandatory)]$Sample,
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)][string]$ServerAddress,
        [Parameter(Mandatory)][string]$ExpectedAddress
    )
    Assert-CtsPilotListenerAddress -Address $ServerAddress
    Assert-CtsPilotListenerAddress -Address $ExpectedAddress
    if ($Role -eq 'Server' -and $ExpectedAddress -cne $ServerAddress) {
        throw 'The expected server address must match the server destination.'
    }
    $time = Get-CtsProp $Sample 'ObservedUtc'
    $parsedTime = [DateTimeOffset]::MinValue
    if ($time -isnot [string] -or $time -notmatch 'Z$' -or -not [DateTimeOffset]::TryParse($time, [ref]$parsedTime)) {
        'Network sample time is missing or invalid'
    }
    if ((Get-CtsProp $Sample 'Role') -ine $Role -or (Get-CtsProp $Sample 'ServerAddress') -cne $ServerAddress) {
        'Network sample endpoint does not match'
    }
    if ([string]::IsNullOrWhiteSpace((Get-CtsProp $Sample 'Guest'))) { 'Guest identity is missing' }
    if ((Get-CtsProp $Sample 'SelectedAddress') -cne $ExpectedAddress) { 'Selected network address does not match' }
    if ((Get-CtsProp $Sample 'AddressState') -ine 'Preferred') { 'Selected network address is not preferred' }
    $index = Get-CtsProp $Sample 'InterfaceIndex'
    if ($index -isnot [int] -or $index -le 0 -or [string]::IsNullOrWhiteSpace((Get-CtsProp $Sample 'InterfaceAlias'))) {
        'Network interface is missing or invalid'
    }
    if ((Get-CtsProp $Sample 'AdapterStatus') -ine 'Up') { 'Network adapter is not up' }
    if ($Role -eq 'Client') {
        $route = Get-CtsProp $Sample 'RoutePrefix'
        $hop = Get-CtsProp $Sample 'NextHop'
        $metric = Get-CtsProp $Sample 'RouteMetric'
        $validRoute = $false
        $network = $null
        $nextHop = $null
        $match = if ($route -is [string]) { [regex]::Match($route, '^(\d{1,3}(?:\.\d{1,3}){3})/([0-9]|[12][0-9]|3[0-2])$') } else { $null }
        if ($null -ne $match -and $match.Success -and
            [Net.IPAddress]::TryParse($match.Groups[1].Value, [ref]$network) -and
            $network.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork -and
            $network.ToString() -ceq $match.Groups[1].Value -and
            $hop -is [string] -and [Net.IPAddress]::TryParse($hop, [ref]$nextHop) -and
            $nextHop.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork -and $nextHop.ToString() -ceq $hop -and
            $metric -is [int] -and $metric -ge 0) {
            $validRoute = $true
            $prefix = [int]$match.Groups[2].Value
            $networkBytes = $network.GetAddressBytes()
            $targetBytes = ([Net.IPAddress]::Parse($ServerAddress)).GetAddressBytes()
            for ($i = 0; $i -lt 4; $i++) {
                $bits = [Math]::Min(8, [Math]::Max(0, $prefix - ($i * 8)))
                $mask = (255 -shl (8 - $bits)) -band 255
                if (($networkBytes[$i] -band $mask) -ne $networkBytes[$i] -or
                    ($targetBytes[$i] -band $mask) -ne $networkBytes[$i]) { $validRoute = $false }
            }
        }
        if (-not $validRoute) {
            'Client route is missing or invalid'
        }
    }
}
