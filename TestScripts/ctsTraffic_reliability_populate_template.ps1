<#
.SYNOPSIS
Creates a reliability scenario copy populated with host inventory.

.DESCRIPTION
Captures the local Windows OS, processor/cache, RAM, and fixed-disk facts,
then fills the corresponding tokens in documents/reliability/scenario-template.md.
Scenario measurements and acceptance criteria remain for the user to define.

.PARAMETER SystemClass
Required system class: desktop or server.

.PARAMETER OutputPath
Required path for the generated Markdown file. The file must not already exist.

.PARAMETER Hypervisor
Hypervisor recorded in the template. Defaults to Hyper-V.

.PARAMETER GuestImage
Guest image label recorded in the template. Defaults to Windows 11 RTM.

.PARAMETER VhdxPath
Optional path to an existing VHDX image. When supplied, it is resolved and
recorded; the file itself is not modified.

.EXAMPLE
.\TestScripts\ctsTraffic_reliability_populate_template.ps1 `
    -SystemClass desktop `
    -OutputPath C:\TestRuns\desktop-scenario.md
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('desktop', 'server')]
    [string]$SystemClass,

    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputPath,

    [ValidateNotNullOrEmpty()]
    [string]$Hypervisor = 'Hyper-V',

    [ValidateNotNullOrEmpty()]
    [string]$GuestImage = 'Windows 11 RTM',

    [string]$VhdxPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$templatePath = Join-Path $PSScriptRoot '..\documents\reliability\scenario-template.md'
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

if ($processors.Count -eq 0) {
    throw 'No processor inventory was returned by Win32_Processor.'
}

function Format-CacheSize {
    param([Nullable[uint32]]$SizeKilobytes)

    if ($null -eq $SizeKilobytes -or $SizeKilobytes -le 0) {
        return 'not reported'
    }

    return ('{0:N0} KB ({1:N1} MiB)' -f $SizeKilobytes, ($SizeKilobytes / 1024))
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

if ([string]::IsNullOrWhiteSpace($VhdxPath)) {
    $vhdxValue = 'TBD; image path will be supplied for the campaign'
}
else {
    $vhdxItem = Get-Item -LiteralPath $VhdxPath
    if ($vhdxItem.PSIsContainer -or $vhdxItem.Extension -ine '.vhdx') {
        throw "VhdxPath must identify an existing .vhdx file: $VhdxPath"
    }
    $vhdxValue = $vhdxItem.FullName
}

$replacementValues = @{
    '{{SYSTEM_CLASS}}' = $SystemClass
    '{{CAPTURED_AT_UTC}}' = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    '{{HYPERVISOR}}' = $Hypervisor
    '{{HOST_OS}}' = '{0}, version {1}, build {2}, {3}' -f $os.Caption, $os.Version, $os.BuildNumber, $os.OSArchitecture
    '{{CPU_MODELS}}' = $cpuModels
    '{{CPU_COUNTS}}' = '{0} physical cores / {1} logical processors' -f $coreCount, $logicalCount
    '{{CPU_CACHES}}' = $cacheDetails
    '{{TOTAL_RAM}}' = '{0:N1} GiB' -f ($computer.TotalPhysicalMemory / 1GB)
    '{{FIXED_STORAGE}}' = $diskDetails
    '{{GUEST_IMAGE}}' = $GuestImage
    '{{VHDX_PATH}}' = $vhdxValue
}

$document = Get-Content -LiteralPath $templatePath -Raw
foreach ($token in $replacementValues.Keys) {
    $document = $document.Replace($token, [string]$replacementValues[$token])
}

if ($document -match '\{\{[A-Z0-9_]+\}\}') {
    throw 'The scenario template contains an unrecognized or unfilled token.'
}

[System.IO.File]::WriteAllText(
    $outputFullPath,
    $document,
    [System.Text.UTF8Encoding]::new($false)
)
Write-Output "Created scenario template: $outputFullPath"
