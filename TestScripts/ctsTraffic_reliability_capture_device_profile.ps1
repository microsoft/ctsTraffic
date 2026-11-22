<#
.SYNOPSIS
Creates a reliability device profile populated with local Windows inventory.

.DESCRIPTION
Captures OS, processor/cache, RAM, fixed-disk, and network adapter facts from
the machine running the script. It does not change device configuration.

.PARAMETER SystemClass
Required system class: desktop or server.

.PARAMETER DeviceRole
Required campaign role for this device, such as host, DUT, peer, or controller.

.PARAMETER OutputPath
Required path for the generated Markdown file. The file must not already exist.

.PARAMETER DeviceId
Optional campaign identifier. Defaults to the computer name.

.EXAMPLE
.\TestScripts\ctsTraffic_reliability_capture_device_profile.ps1 `
    -SystemClass desktop `
    -DeviceRole host `
    -OutputPath C:\TestRuns\desktop-host-profile.md
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('desktop', 'server')]
    [string]$SystemClass,

    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string]$DeviceRole,

    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputPath,

    [ValidateNotNullOrEmpty()]
    [string]$DeviceId
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$templatePath = Join-Path $PSScriptRoot '..\documents\reliability\device-profile-template.md'
$templatePath = (Resolve-Path -LiteralPath $templatePath).Path
$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($outputFullPath)

if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    throw "Output directory does not exist: $outputDirectory"
}
if (Test-Path -LiteralPath $outputFullPath) {
    throw "Output file already exists; choose a new path: $outputFullPath"
}

$os = Get-CimInstance -ClassName Win32_OperatingSystem
$computer = Get-CimInstance -ClassName Win32_ComputerSystem
$processors = @(Get-CimInstance -ClassName Win32_Processor)
$fixedDisks = @(Get-CimInstance -ClassName Win32_LogicalDisk -Filter 'DriveType = 3')
$adapters = @(Get-CimInstance -ClassName Win32_NetworkAdapter)
$adapterConfigurations = @(Get-CimInstance -ClassName Win32_NetworkAdapterConfiguration)

if ($processors.Count -eq 0) {
    throw 'No processor inventory was returned by Win32_Processor.'
}
if ([string]::IsNullOrWhiteSpace($DeviceId)) {
    $DeviceId = $computer.Name
}

function Format-CacheSize {
    param([Nullable[uint32]]$SizeKilobytes)

    if ($null -eq $SizeKilobytes -or $SizeKilobytes -le 0) {
        return 'not reported'
    }

    return ('{0:N0} KB ({1:N1} MiB)' -f $SizeKilobytes, ($SizeKilobytes / 1024))
}

function Format-Values {
    param([object[]]$Values)

    if ($null -eq $Values -or $Values.Count -eq 0) {
        return 'none'
    }

    return (@($Values | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }) -join ', ')
}

$cpuModels = @($processors | ForEach-Object { $_.Name } | Select-Object -Unique) -join '; '
$coreCount = ($processors | Measure-Object -Property NumberOfCores -Sum).Sum
$logicalCount = ($processors | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
$cacheDetails = @(
    $processors | ForEach-Object {
        '{0}: L2 {1}; L3 {2}' -f $_.Name, (Format-CacheSize $_.L2CacheSize), (Format-CacheSize $_.L3CacheSize)
    } | Select-Object -Unique
) -join '; '
$diskDetails = @(
    $fixedDisks | ForEach-Object {
        '{0}: {1:N1} GiB total, {2:N1} GiB free' -f $_.DeviceID, ($_.Size / 1GB), ($_.FreeSpace / 1GB)
    }
) -join '; '
if ([string]::IsNullOrWhiteSpace($diskDetails)) {
    $diskDetails = 'not reported'
}

$adapterLines = foreach ($adapter in $adapters | Sort-Object -Property Index) {
    $configuration = $adapterConfigurations | Where-Object { $_.Index -eq $adapter.Index } | Select-Object -First 1
    $ipAddresses = if ($null -ne $configuration) { Format-Values $configuration.IPAddress } else { 'none' }
    $subnets = if ($null -ne $configuration) { Format-Values $configuration.IPSubnet } else { 'none' }
    $gateways = if ($null -ne $configuration) { Format-Values $configuration.DefaultIPGateway } else { 'none' }
    $dhcp = if ($null -ne $configuration) { [string]$configuration.DHCPEnabled } else { 'not reported' }
    $speed = if ($null -ne $adapter.Speed -and $adapter.Speed -gt 0) {
        '{0:N0} Mbps' -f ($adapter.Speed / 1000000)
    }
    else {
        'not reported'
    }
    $linkState = if ($adapter.NetEnabled) { 'enabled' } else { 'disabled or disconnected' }
    "- Index {0}: {1}; {2}; {3}; MAC {4}; IPs {5}; subnets {6}; gateways {7}; DHCP {8}" -f `
        $adapter.Index, $adapter.Name, $linkState, $speed, $adapter.MACAddress, $ipAddresses, $subnets, $gateways, $dhcp
}
if ($null -eq $adapterLines) {
    $adapterLines = '- No network adapters were reported.'
}

$replacementValues = @{
    '{{DEVICE_ID}}' = $DeviceId
    '{{DEVICE_ROLE}}' = $DeviceRole
    '{{SYSTEM_CLASS}}' = $SystemClass
    '{{CAPTURED_AT_UTC}}' = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    '{{COMPUTER_IDENTITY}}' = '{0} / {1} / {2}' -f $computer.Name, $computer.Manufacturer, $computer.Model
    '{{OPERATING_SYSTEM}}' = '{0}, version {1}, build {2}, {3}' -f $os.Caption, $os.Version, $os.BuildNumber, $os.OSArchitecture
    '{{CPU_MODELS}}' = $cpuModels
    '{{CPU_COUNTS}}' = '{0} physical cores / {1} logical processors' -f $coreCount, $logicalCount
    '{{CPU_CACHES}}' = $cacheDetails
    '{{TOTAL_RAM}}' = '{0:N1} GiB' -f ($computer.TotalPhysicalMemory / 1GB)
    '{{AVAILABLE_RAM}}' = '{0:N1} GiB' -f ($os.FreePhysicalMemory / 1MB)
    '{{FIXED_STORAGE}}' = $diskDetails
    '{{NETWORK_ADAPTERS}}' = $adapterLines -join [Environment]::NewLine
}

$document = Get-Content -LiteralPath $templatePath -Raw
foreach ($token in $replacementValues.Keys) {
    $document = $document.Replace($token, [string]$replacementValues[$token])
}

if ($document -match '\{\{[A-Z0-9_]+\}\}') {
    throw 'The device profile template contains an unrecognized or unfilled token.'
}

[System.IO.File]::WriteAllText(
    $outputFullPath,
    $document,
    [System.Text.UTF8Encoding]::new($false)
)
Write-Output "Created device profile: $outputFullPath"
