# Kernel pool tag snapshots (poolsnap equivalent) via NtQuerySystemInformation(SystemPoolTagInformation).

$script:CtsPoolTagScript = {
    # Self-contained: runs locally or inside a guest (Windows PowerShell 5.1 compatible).
    # -LoadOnly only compiles the reader type (used by tests); it does not query the kernel.
    param([switch]$LoadOnly)
    if (-not ('CtsReliability.PoolTagReader' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
namespace CtsReliability {
    public sealed class PoolTagEntry {
        public string Tag;
        public long PagedAllocs; public long PagedFrees; public long PagedUsed;
        public long NonPagedAllocs; public long NonPagedFrees; public long NonPagedUsed;
    }
    public static class PoolTagReader {
        const int SystemPoolTagInformation = 22;
        const uint STATUS_INFO_LENGTH_MISMATCH = 0xC0000004;
        [DllImport("ntdll.dll")]
        static extern uint NtQuerySystemInformation(int infoClass, IntPtr buffer, int length, out int returnLength);

        public static List<PoolTagEntry> Parse(IntPtr buffer, bool is64Bit) {
            // SYSTEM_POOLTAG: Tag(4) PagedAllocs(4) PagedFrees(4) [pad] PagedUsed(SIZE_T) NonPagedAllocs(4) NonPagedFrees(4) NonPagedUsed(SIZE_T)
            int entrySize = is64Bit ? 40 : 28;
            int arrayOffset = is64Bit ? 8 : 4;
            int count = Marshal.ReadInt32(buffer, 0);
            var result = new List<PoolTagEntry>(count);
            for (int i = 0; i < count; i++) {
                IntPtr e = IntPtr.Add(buffer, arrayOffset + i * entrySize);
                byte[] tag = new byte[4];
                Marshal.Copy(e, tag, 0, 4);
                char[] chars = new char[4];
                for (int c = 0; c < 4; c++) { chars[c] = (tag[c] >= 0x20 && tag[c] < 0x7F) ? (char)tag[c] : '.'; }
                var entry = new PoolTagEntry();
                entry.Tag = new string(chars);
                entry.PagedAllocs = (uint)Marshal.ReadInt32(e, 4);
                entry.PagedFrees = (uint)Marshal.ReadInt32(e, 8);
                entry.PagedUsed = is64Bit ? Marshal.ReadInt64(e, 16) : (uint)Marshal.ReadInt32(e, 12);
                entry.NonPagedAllocs = (uint)Marshal.ReadInt32(e, is64Bit ? 24 : 16);
                entry.NonPagedFrees = (uint)Marshal.ReadInt32(e, is64Bit ? 28 : 20);
                entry.NonPagedUsed = is64Bit ? Marshal.ReadInt64(e, 32) : (uint)Marshal.ReadInt32(e, 24);
                result.Add(entry);
            }
            return result;
        }

        public static List<PoolTagEntry> Parse(byte[] data, bool is64Bit) {
            GCHandle handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try { return Parse(handle.AddrOfPinnedObject(), is64Bit); }
            finally { handle.Free(); }
        }

        public static List<PoolTagEntry> Query() {
            int length = 1024 * 1024;
            for (int attempt = 0; attempt < 8; attempt++) {
                IntPtr buffer = Marshal.AllocHGlobal(length);
                try {
                    int needed;
                    uint status = NtQuerySystemInformation(SystemPoolTagInformation, buffer, length, out needed);
                    if (status == STATUS_INFO_LENGTH_MISMATCH) {
                        length = Math.Max(length * 2, needed + 64 * 1024);
                        continue;
                    }
                    if (status != 0) { throw new InvalidOperationException(string.Format("NtQuerySystemInformation(SystemPoolTagInformation) failed: 0x{0:X8}", status)); }
                    return Parse(buffer, IntPtr.Size == 8);
                }
                finally { Marshal.FreeHGlobal(buffer); }
            }
            throw new InvalidOperationException("NtQuerySystemInformation(SystemPoolTagInformation): buffer did not stabilize");
        }
    }
}
'@
    }
    if ($LoadOnly) { return }
    $entries = [CtsReliability.PoolTagReader]::Query()
    if ($entries.Count -le 1 -and ($entries.Count -eq 0 -or ($entries[0].Tag -eq 'None' -and $entries[0].PagedUsed -eq 0 -and $entries[0].NonPagedUsed -eq 0))) {
        throw 'Pool tag information is restricted for this caller (only a placeholder entry was returned). Run from an elevated (administrator) session.'
    }
    foreach ($entry in $entries) {
        [pscustomobject]@{
            Tag            = $entry.Tag
            PagedAllocs    = $entry.PagedAllocs
            PagedFrees     = $entry.PagedFrees
            PagedDiff      = $entry.PagedAllocs - $entry.PagedFrees
            PagedUsed      = $entry.PagedUsed
            NonPagedAllocs = $entry.NonPagedAllocs
            NonPagedFrees  = $entry.NonPagedFrees
            NonPagedDiff   = $entry.NonPagedAllocs - $entry.NonPagedFrees
            NonPagedUsed   = $entry.NonPagedUsed
        }
    }
}

function Get-CtsPoolTagSnapshot {
    <#
    .SYNOPSIS
    Captures kernel pool usage per tag on the local machine or inside a guest. Requires an elevated
    session: Windows returns only a placeholder entry to non-administrators, which this function rejects.
    .PARAMETER Path
    Optional CSV output path. The file must not already exist.
    .PARAMETER GuestSession
    Optional guest session from New-CtsGuestSession; when omitted the local machine is captured.
    #>
    [CmdletBinding()]
    param(
        [string]$Path,
        [object]$GuestSession
    )
    if ($Path) {
        if (Test-Path -LiteralPath $Path) { throw "Output file already exists: $Path" }
        $parent = Split-Path -Parent $Path
        if ($parent -and -not (Test-Path -LiteralPath $parent)) { throw "Output directory does not exist: $parent" }
    }
    $rows = if ($GuestSession) {
        Invoke-CtsGuestCommand -GuestSession $GuestSession -ScriptBlock $script:CtsPoolTagScript -Operation 'PoolTagSnapshot'
    }
    else {
        & $script:CtsPoolTagScript
    }
    $rows = @($rows | ForEach-Object {
            [pscustomobject]@{
                Tag = [string]$_.Tag
                PagedAllocs = [int64]$_.PagedAllocs; PagedFrees = [int64]$_.PagedFrees; PagedDiff = [int64]$_.PagedDiff; PagedUsed = [int64]$_.PagedUsed
                NonPagedAllocs = [int64]$_.NonPagedAllocs; NonPagedFrees = [int64]$_.NonPagedFrees; NonPagedDiff = [int64]$_.NonPagedDiff; NonPagedUsed = [int64]$_.NonPagedUsed
            }
        } | Sort-Object Tag)
    if ($Path) {
        $csvArgs = @{ LiteralPath = $Path; Encoding = 'UTF8' }
        if ($PSVersionTable.PSVersion.Major -lt 6) { $csvArgs.NoTypeInformation = $true }
        $rows | Export-Csv @csvArgs
    }
    return $rows
}

function Compare-CtsPoolTagSnapshot {
    <#
    .SYNOPSIS
    Returns per-tag growth between two pool tag snapshots (objects or CSV paths), largest growth first.
    .PARAMETER MinimumBytes
    Only report tags whose paged or nonpaged bytes grew by at least this much.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object]$Reference,
        [Parameter(Mandatory)][object]$Difference,
        [int64]$MinimumBytes = 0
    )
    function Load($value) {
        if ($value -is [string]) { return @(Import-Csv -LiteralPath $value) }
        return @($value)
    }
    $before = @{}
    foreach ($row in (Load $Reference)) { $before[[string]$row.Tag] = $row }
    $after = @{}
    foreach ($row in (Load $Difference)) { $after[[string]$row.Tag] = $row }

    $result = foreach ($tag in (@($before.Keys) + @($after.Keys) | Sort-Object -Unique)) {
        $b = $before[$tag]; $a = $after[$tag]
        $pagedBefore = if ($b) { [int64]$b.PagedUsed } else { 0 }
        $pagedAfter = if ($a) { [int64]$a.PagedUsed } else { 0 }
        $nonPagedBefore = if ($b) { [int64]$b.NonPagedUsed } else { 0 }
        $nonPagedAfter = if ($a) { [int64]$a.NonPagedUsed } else { 0 }
        $outBefore = if ($b) { [int64]$b.PagedDiff + [int64]$b.NonPagedDiff } else { 0 }
        $outAfter = if ($a) { [int64]$a.PagedDiff + [int64]$a.NonPagedDiff } else { 0 }
        $pagedDelta = $pagedAfter - $pagedBefore
        $nonPagedDelta = $nonPagedAfter - $nonPagedBefore
        if ([math]::Max($pagedDelta, $nonPagedDelta) -lt $MinimumBytes) { continue }
        [pscustomobject]@{
            Tag                    = $tag
            PagedUsedDelta         = $pagedDelta
            NonPagedUsedDelta      = $nonPagedDelta
            TotalUsedDelta         = $pagedDelta + $nonPagedDelta
            OutstandingAllocsDelta = $outAfter - $outBefore
            OnlyIn                 = if (-not $b) { 'Difference' } elseif (-not $a) { 'Reference' } else { $null }
        }
    }
    return @($result | Sort-Object TotalUsedDelta -Descending)
}
