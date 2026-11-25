# Lab definition, derived topology, and small shared helpers.

function Test-CtsElevated {
    [CmdletBinding()]
    param()
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Merge-CtsObject {
    # Copies every property of $Overlay onto $Base, recursing into nested objects.
    param([Parameter(Mandatory)]$Base, [Parameter(Mandatory)]$Overlay)
    foreach ($p in $Overlay.PSObject.Properties) {
        $existing = $Base.PSObject.Properties[$p.Name]
        if ($existing -and $existing.Value -is [System.Management.Automation.PSCustomObject] -and $p.Value -is [System.Management.Automation.PSCustomObject]) {
            Merge-CtsObject -Base $existing.Value -Overlay $p.Value
        } else {
            $Base | Add-Member -NotePropertyName $p.Name -NotePropertyValue $p.Value -Force
        }
    }
}

function Get-CtsLabDefinition {
    <#
    .SYNOPSIS
    Loads and validates a lab definition JSON file (default: lab.v1.json next to the module).
    .DESCRIPTION
    Values in the local override file (default: <labRoot>\lab.local.json) replace those in the definition.
    The override holds machine- or organization-specific values, such as internal share paths, that must
    not be committed to the public repository.
    #>
    [CmdletBinding()]
    param(
        [string]$Path = (Join-Path (Split-Path $PSScriptRoot -Parent) 'lab.v1.json'),
        [string]$LocalPath
    )
    $definition = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if (-not $LocalPath -and $definition.PSObject.Properties['labRoot']) { $LocalPath = Join-Path $definition.labRoot 'lab.local.json' }
    if ($LocalPath -and (Test-Path -LiteralPath $LocalPath -PathType Leaf)) {
        $local = Get-Content -LiteralPath $LocalPath -Raw | ConvertFrom-Json
        if ($local.PSObject.Properties['labRoot']) { throw "labRoot cannot be set in the local override file '$LocalPath'." }
        Merge-CtsObject -Base $definition -Overlay $local
    }
    $errors = [System.Collections.Generic.List[string]]::new()

    foreach ($name in 'schemaVersion', 'labId', 'labRoot', 'namePrefix', 'goldenImage', 'vm', 'management', 'test', 'guest', 'debugger') {
        if (-not ($definition.PSObject.Properties.Name -contains $name)) { $errors.Add("missing field '$name'") }
    }
    if ($errors.Count -eq 0) {
        if ($definition.schemaVersion -ne 1) { $errors.Add("unsupported schemaVersion '$($definition.schemaVersion)'") }
        if ($definition.vm.count -lt 2 -or $definition.vm.count -gt 5) { $errors.Add('vm.count must be 2 through 5') }
        if ($definition.vm.processorCount -lt 1) { $errors.Add('vm.processorCount must be at least 1') }
        if ($definition.vm.memoryGB -lt 1) { $errors.Add('vm.memoryGB must be at least 1') }
        if ($definition.vm.diskMode -notin 'Differencing', 'Copy') { $errors.Add("vm.diskMode must be Differencing or Copy") }
        if ($definition.namePrefix -notmatch '^[A-Za-z][A-Za-z0-9-]{0,9}$') { $errors.Add('namePrefix must be 1-10 letters, digits, or hyphens, starting with a letter') }
        if ([string]$definition.goldenImage.fileName -notmatch '^(?:[\w.-]|\{arch\})+\.vhdx$') { $errors.Add('goldenImage.fileName must be a plain file name ending in .vhdx; {arch} is replaced by the host architecture') }
        $isoProperty = $definition.goldenImage.PSObject.Properties['iso']
        if ($isoProperty -and $isoProperty.Value) {
            $isoFile = $isoProperty.Value.PSObject.Properties['fileName']
            if ($isoFile -and $isoFile.Value -and ([string]$isoFile.Value -notmatch '^(?:[\w.-]|\{arch\})+\.iso$')) { $errors.Add('goldenImage.iso.fileName must be a plain file name ending in .iso; {arch} is replaced by the host architecture') }
            $isoHash = $isoProperty.Value.PSObject.Properties['sha256']
            if ($isoHash -and $null -ne $isoHash.Value) {
                $hashValid = $isoHash.Value -is [System.Management.Automation.PSCustomObject]
                if ($hashValid) {
                    foreach ($p in $isoHash.Value.PSObject.Properties) {
                        if ($p.Name -notin 'amd64', 'arm64' -or ($p.Value -and ([string]$p.Value -notmatch '^[0-9A-Fa-f]{64}$'))) { $hashValid = $false }
                    }
                }
                if (-not $hashValid) { $errors.Add('goldenImage.iso.sha256 must be null or an object with amd64 and arm64 keys, each null or a 64-character hex SHA-256') }
            }
        }
        $subnetValid = $true
        try { $null = ConvertTo-CtsIPv4Network $definition.management.subnet } catch { $subnetValid = $false; $errors.Add("management.subnet: $($_.Exception.Message)") }
        try {
            if ($subnetValid -and -not (Test-CtsIPv4InSubnet -Address $definition.management.hostAddress -Cidr $definition.management.subnet)) {
                $errors.Add('management.hostAddress is not inside management.subnet')
            }
        }
        catch { $errors.Add("management.hostAddress: $($_.Exception.Message)") }
    }
    if ($errors.Count -gt 0) {
        throw "Invalid lab definition '$Path': $($errors -join '; ')"
    }
    return $definition
}

function ConvertTo-CtsIPv4Network {
    param([Parameter(Mandatory)][string]$Cidr)
    $parts = $Cidr.Split('/')
    if ($parts.Count -ne 2) { throw "'$Cidr' is not in address/prefix form" }
    $address = [System.Net.IPAddress]::Parse($parts[0])
    if ($address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) { throw "'$Cidr' is not IPv4" }
    $prefix = [int]$parts[1]
    if ($prefix -lt 0 -or $prefix -gt 32) { throw "'$Cidr' has an invalid prefix length" }
    $bytes = $address.GetAddressBytes(); [Array]::Reverse($bytes)
    $value = [BitConverter]::ToUInt32($bytes, 0)
    $mask = [uint32]([uint64]4294967296 - [uint64][math]::Pow(2, 32 - $prefix))
    return [pscustomobject]@{ Network = ($value -band $mask); Mask = $mask; Prefix = $prefix }
}

function Test-CtsIPv4InSubnet {
    param([string]$Address, [string]$Cidr)
    $net = ConvertTo-CtsIPv4Network $Cidr
    $host_ = ConvertTo-CtsIPv4Network "$Address/32"
    return (($host_.Network -band $net.Mask) -eq $net.Network)
}

function Test-CtsSubnetOverlap {
    <#
    .SYNOPSIS
    Returns the existing IPv4 address entries whose subnet overlaps the given CIDR.
    .PARAMETER Existing
    Objects with IPAddress and PrefixLength (for example, from Get-NetIPAddress -AddressFamily IPv4).
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Cidr,
        [AllowEmptyCollection()][object[]]$Existing = @()
    )
    $target = ConvertTo-CtsIPv4Network $Cidr
    foreach ($entry in $Existing) {
        if ([string]$entry.IPAddress -notmatch '^\d+\.\d+\.\d+\.\d+$') { continue }
        $other = ConvertTo-CtsIPv4Network ("{0}/{1}" -f $entry.IPAddress, $entry.PrefixLength)
        $commonMask = if ($target.Prefix -lt $other.Prefix) { $target.Mask } else { $other.Mask }
        if (($target.Network -band $commonMask) -eq ($other.Network -band $commonMask)) { $entry }
    }
}

function Get-CtsLabTopology {
    <#
    .SYNOPSIS
    Derives VM names, links, NICs, static MACs, and addresses from the lab definition.
    .DESCRIPTION
    Link numbers follow topologies.md: AB=1, AC=2, BC=3, AD=4, BD=5, CD=6, AE=7, BE=8, CE=9, DE=10.
    Test NIC addresses are 192.168.L.V and fd00:L::V; management is <management subnet>.V.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [ValidateRange(2, 5)][int]$VmCount = $Definition.vm.count
    )
    $letters = 'A', 'B', 'C', 'D', 'E'
    $prefix = $Definition.namePrefix
    $mgmtNet = ConvertTo-CtsIPv4Network $Definition.management.subnet
    $mgmtBase = $Definition.management.subnet.Split('/')[0].Split('.')

    $vms = [ordered]@{}
    for ($v = 1; $v -le $VmCount; $v++) {
        $letter = $letters[$v - 1]
        $vms[$letter] = [pscustomobject]@{
            Letter       = $letter
            Number       = $v
            Name         = "$prefix-$letter"
            ComputerName = ("$prefix-$letter").ToUpperInvariant()
            MgmtNic      = [pscustomobject]@{
                Name         = 'NIC-Mgmt'
                SwitchName   = $Definition.management.switchName
                MacAddress   = ('00155DCE{0:X2}FE' -f $v)
                IPv4Address  = ('{0}.{1}.{2}.{3}' -f $mgmtBase[0], $mgmtBase[1], $mgmtBase[2], $v)
                IPv4Prefix   = $mgmtNet.Prefix
            }
            TestNics     = [System.Collections.Generic.List[object]]::new()
        }
    }

    $links = [System.Collections.Generic.List[object]]::new()
    $linkNumber = 0
    for ($j = 1; $j -lt $VmCount; $j++) {
        for ($i = 0; $i -lt $j; $i++) {
            $linkNumber++
            $a = $letters[$i]; $b = $letters[$j]
            $pair = "$a$b"
            $switchName = "$prefix-Private-$pair"
            $links.Add([pscustomobject]@{ Number = $linkNumber; Pair = $pair; SwitchName = $switchName; Vms = @($vms[$a].Name, $vms[$b].Name) })
            foreach ($end in @(@{ Self = $a; Peer = $b }, @{ Self = $b; Peer = $a })) {
                $self = $vms[$end.Self]; $peer = $vms[$end.Peer]
                $self.TestNics.Add([pscustomobject]@{
                        Name        = "NIC-$($end.Self)$($end.Peer)"
                        Link        = $pair
                        LinkNumber  = $linkNumber
                        SwitchName  = $switchName
                        PeerVm      = $peer.Name
                        MacAddress  = ('00155DCE{0:X2}{1:X2}' -f $self.Number, $linkNumber)
                        IPv4Address = "192.168.$linkNumber.$($self.Number)"
                        IPv4Prefix  = [int]$Definition.test.ipv4Prefix
                        IPv6Address = "fd00:$linkNumber::$($self.Number)"
                        IPv6Prefix  = [int]$Definition.test.ipv6Prefix
                        PeerIPv4    = "192.168.$linkNumber.$($peer.Number)"
                        PeerIPv6    = "fd00:$linkNumber::$($peer.Number)"
                    })
            }
        }
    }

    return [pscustomobject]@{
        LabId      = $Definition.labId
        VmCount    = $VmCount
        Vms        = @($vms.Values)
        Links      = @($links)
        Management = [pscustomobject]@{
            SwitchName  = $Definition.management.switchName
            Subnet      = $Definition.management.subnet
            HostAddress = $Definition.management.hostAddress
            HostPrefix  = $mgmtNet.Prefix
        }
    }
}
