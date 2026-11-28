# Reserve an empty run directory without adopting an existing path.

$script:CtsPilotDirectoryScript = {
    param($RootPath, $Token)

    $ErrorActionPreference = 'Stop'
    if ($Token -notmatch '^[0-9a-f]{32}$') { throw 'Invalid pilot directory token.' }
    if ($RootPath -notmatch '^[A-Za-z]:\\' -or $RootPath -match '[\s"<>|?*\x00-\x1f]' -or
        $RootPath.Substring(2) -match ':' -or
        @($RootPath.Substring(3).Split('\') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
        throw 'Pilot root must be an absolute local path without whitespace, quotes, wildcards, or dot segments.'
    }

    if (-not ('CtsReliability.PilotDirectoryNative' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace CtsReliability {
    public static class PilotDirectoryNative {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool CreateDirectoryW(string path, IntPtr securityAttributes);
    }
}
'@
    }
    $path = [IO.Path]::Combine($RootPath, "pilot-$Token")
    if (-not [CtsReliability.PilotDirectoryNative]::CreateDirectoryW($path, [IntPtr]::Zero)) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($code -eq 183) { throw "Pilot directory already exists: $path" }
        throw [System.ComponentModel.Win32Exception]::new($code, "Could not create pilot directory: $path")
    }
    return $path
}

function New-CtsPilotRunDirectory {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)][string]$RootPath,
        [Parameter(Mandatory)][string]$Token
    )
    Invoke-CtsGuestCommand -GuestSession $GuestSession -ScriptBlock $script:CtsPilotDirectoryScript `
        -ArgumentList @($RootPath, $Token) -Operation 'PilotDirectory:Create'
}
