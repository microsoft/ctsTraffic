# Resolves the finite pilot commands; does not validate the environment or start traffic.

function New-CtsPilotProcessSpec {
    param(
        [Parameter(Mandatory)][ValidateSet('Server', 'Client')][string]$Role,
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string]$RunDirectory,
        [Parameter(Mandatory)][string]$ServerAddress
    )

    foreach ($path in @($Executable, $RunDirectory)) {
        if ($path -notmatch '^[A-Za-z]:\\' -or $path -match '[\s"<>|?*\x00-\x1f]' -or
            $path.Substring(2) -match ':' -or
            @($path.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
            throw 'Pilot paths must be absolute local paths without whitespace, quotes, wildcards, or dot segments.'
        }
    }
    $address = $null
    if (-not [System.Net.IPAddress]::TryParse($ServerAddress, [ref]$address) -or
        $address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork -or
        $ServerAddress -notmatch '^\d{1,3}(\.\d{1,3}){3}$' -or
        $ServerAddress -cne $address.ToString()) {
        throw 'ServerAddress must be a canonical IPv4 address.'
    }

    $prefix = 'g1b-pilot-' + $Role.ToLowerInvariant()
    $connection = Join-Path $RunDirectory "$prefix-connection.csv"
    $errorLog = Join-Path $RunDirectory "$prefix-error.log"
    $status = Join-Path $RunDirectory "$prefix-status.csv"
    $arguments = if ($Role -eq 'Server') {
        @("-Listen:$ServerAddress", '-Pattern:duplex', '-ServerExitLimit:1024', '-TimeLimit:420000')
    }
    else {
        @("-Target:$ServerAddress", '-Pattern:duplex', '-Iterations:128', '-TimeLimit:300000')
    }
    $arguments += @('-ConsoleVerbosity:0', "-ConnectionFilename:$connection", "-ErrorFilename:$errorLog", "-StatusFilename:$status")

    [pscustomobject]@{
        Role = $Role
        FilePath = $Executable
        Arguments = $arguments
        StdoutPath = Join-Path $RunDirectory "$prefix-stdout.txt"
        StderrPath = Join-Path $RunDirectory "$prefix-stderr.txt"
        ConnectionPath = $connection
        ErrorPath = $errorLog
        StatusPath = $status
    }
}
