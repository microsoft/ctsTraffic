# Host-local overrides (<labRoot>\lab.local.json) for values that must never be committed, such as internal
# share paths. Get-CtsLabDefinition merges this file over lab.v1.json.

function Set-CtsLocalConfig {
    <#
    .SYNOPSIS
    Plans (default) or writes (-Apply) debugger source paths into the host-local lab.local.json.
    .DESCRIPTION
    Existing values in the file are kept unless replaced. The file must be outside the repository. A source that
    is not reachable now is allowed, with a note, because the share may be offline.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [string]$DebuggerSourceAmd64,
        [string]$DebuggerSourceArm64,
        [string]$LocalPath,
        [switch]$Apply
    )
    if (-not $LocalPath) { $LocalPath = Join-Path $Definition.labRoot 'lab.local.json' }
    $reasons = New-Object System.Collections.Generic.List[string]
    $notes = New-Object System.Collections.Generic.List[string]
    $changes = New-Object System.Collections.Generic.List[string]

    $repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')).TrimEnd('\') + '\'
    $fullLocal = [IO.Path]::GetFullPath($LocalPath)
    if ($fullLocal.StartsWith($repoRoot, [StringComparison]::OrdinalIgnoreCase)) {
        $reasons.Add("$fullLocal is inside the repository. The local config holds internal values and must be outside it.")
    }

    $exists = Test-Path -LiteralPath $fullLocal -PathType Leaf
    $config = if ($exists) { Get-Content -LiteralPath $fullLocal -Raw | ConvertFrom-Json } else { [pscustomobject]@{} }
    if (-not $config) { $config = [pscustomobject]@{} }
    if (-not $config.PSObject.Properties['debugger']) { $config | Add-Member debugger ([pscustomobject]@{}) }
    if (-not $config.debugger.PSObject.Properties['sources']) { $config.debugger | Add-Member sources ([pscustomobject]@{}) }

    $requested = [ordered]@{ amd64 = $DebuggerSourceAmd64; arm64 = $DebuggerSourceArm64 }
    if (-not ($requested.Values | Where-Object { $_ })) { $reasons.Add('Nothing to set. Pass -DebuggerSourceAmd64 and/or -DebuggerSourceArm64.') }
    foreach ($arch in $requested.Keys) {
        $value = $requested[$arch]
        if (-not $value) { continue }
        if ($value -notmatch '^(\\\\[^\\]+\\|[A-Za-z]:\\).+\.msi$') { $reasons.Add("Debugger source for $arch must be a full UNC or drive path to an .msi file: $value"); continue }
        $old = Get-CtsProp $config.debugger.sources $arch
        if ($old -eq $value) { $notes.Add("debugger.sources.$arch is already set to this path."); continue }
        $changes.Add("debugger.sources.${arch}: $(if ($old) { $old } else { '(not set)' }) -> $value")
        $config.debugger.sources | Add-Member -NotePropertyName $arch -NotePropertyValue $value -Force
        $reachable = $false
        try { $reachable = [bool](Test-Path -LiteralPath $value -PathType Leaf -ErrorAction Stop) } catch { $reachable = $false }
        if (-not $reachable) { $notes.Add("$value is not reachable now. It is saved anyway; the debugger cache refresh needs it reachable.") }
    }

    $blocked = $reasons.Count -gt 0
    $plan = [pscustomobject]@{
        Path    = $fullLocal
        Exists  = $exists
        Action  = if ($blocked) { 'Blocked' } elseif ($changes.Count -eq 0) { 'NoChange' } else { 'Write' }
        Blocked = $blocked
        Reasons = @($reasons)
        Changes = @($changes)
        Notes   = @($notes)
        Applied = $false
    }
    if (-not $Apply -or $plan.Action -ne 'Write') {
        if ($Apply -and $blocked) { throw "Local config not written: $($reasons -join ' ')" }
        return $plan
    }

    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $fullLocal) -Force
    [IO.File]::WriteAllText($fullLocal, ($config | ConvertTo-Json -Depth 10))
    $plan.Applied = $true
    return $plan
}
