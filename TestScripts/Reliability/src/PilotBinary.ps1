# Checks one captured binary record; it does not authorize or start a workload.

function Test-CtsPilotBinarySnapshot {
    param(
        [Parameter(Mandatory)]$Device,
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$ExpectedSha256
    )
    if ($Path -notmatch '^[A-Za-z]:\\' -or $Path -match '[\s"<>|?*\x00-\x1f]' -or
        $Path.Substring(2) -match ':' -or
        @($Path.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0 -or
        [IO.Path]::GetExtension($Path) -ine '.exe') {
        throw 'Pilot executable must be an absolute local executable path without whitespace or dot segments.'
    }
    if ($ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Expected SHA-256 must contain 64 hexadecimal characters.' }
    $inventory = Get-CtsProp $Device 'inventory'
    $sections = Get-CtsProp $inventory 'sections'
    $tools = Get-CtsProp $sections 'tools'
    if ((Get-CtsProp $Device 'role') -ine 'guest' -or (Get-CtsProp $Device 'status') -ne 'complete' -or
        (Get-CtsProp $tools 'status') -ne 'ok') {
        'Guest tool inventory is unavailable or incomplete'
        return
    }
    $entries = @(@(Get-CtsProp $tools 'data') | Where-Object { (Get-CtsProp $_ 'path') -ieq $Path })
    if ($entries.Count -ne 1) {
        'Expected executable was not recorded exactly once'
        return
    }
    $entry = $entries[0]
    $size = Get-CtsProp $entry 'bytes'
    $hash = Get-CtsProp $entry 'sha256'
    if ((Get-CtsProp $entry 'present') -isnot [bool] -or -not (Get-CtsProp $entry 'present') -or
        ($size -isnot [int] -and $size -isnot [long]) -or $size -le 0 -or
        $hash -isnot [string] -or $hash -notmatch '^[0-9a-fA-F]{64}$') {
        'Executable inventory is missing or invalid'
    }
    elseif ($hash -ine $ExpectedSha256) {
        'Executable SHA-256 does not match'
    }
}
