# Golden image creation (offline apply of install.wim/esd into a GPT VHDX) and per-VM unattend content.
# The golden VHDX holds an applied, never-booted image, so it is already in the generalized (OOBE) state;
# each VM boots a differencing disk with its own injected unattend.xml.

$script:CtsEspGptType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$script:CtsMsrGptType = '{e3c9e316-0b5c-4db8-817d-f92df00215ae}'

function ConvertTo-CtsUnattendPassword {
    # Unattend "PlainText=false" encoding: Base64(UTF-16LE(password + element name)). Obfuscation, not encryption.
    param([Parameter(Mandatory)][securestring]$Password, [string]$Suffix = 'Password')
    $plain = [pscredential]::new('x', $Password).GetNetworkCredential().Password
    try { return [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($plain + $Suffix)) }
    finally { $plain = $null }
}

function New-CtsUnattendXml {
    <#
    .SYNOPSIS
    Returns the unattend.xml text for one VM: computer name, UTC time zone, locale, OOBE suppressed, and one
    local administrator. The password is stored in the unattend's encoded form, never as plain text.
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9-]{1,15}$')][string]$ComputerName,
        [Parameter(Mandatory)][pscredential]$Credential,
        [Parameter(Mandatory)][ValidateSet('amd64', 'arm64')][string]$Architecture,
        [string]$Locale = 'en-US'
    )
    $esc = { param($s) [System.Security.SecurityElement]::Escape([string]$s) }
    $user = & $esc $Credential.UserName
    $encoded = ConvertTo-CtsUnattendPassword -Password $Credential.Password
    $loc = & $esc $Locale
    $component = 'processorArchitecture="{0}" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS"' -f $Architecture
    return @"
<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
  <settings pass="specialize">
    <component name="Microsoft-Windows-Shell-Setup" $component>
      <ComputerName>$(& $esc $ComputerName)</ComputerName>
      <TimeZone>UTC</TimeZone>
    </component>
  </settings>
  <settings pass="oobeSystem">
    <component name="Microsoft-Windows-International-Core" $component>
      <InputLocale>$loc</InputLocale>
      <SystemLocale>$loc</SystemLocale>
      <UILanguage>$loc</UILanguage>
      <UserLocale>$loc</UserLocale>
    </component>
    <component name="Microsoft-Windows-Shell-Setup" $component>
      <OOBE>
        <HideEULAPage>true</HideEULAPage>
        <HideOEMRegistrationScreen>true</HideOEMRegistrationScreen>
        <HideOnlineAccountScreens>true</HideOnlineAccountScreens>
        <HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE>
        <HideLocalAccountScreen>true</HideLocalAccountScreen>
        <ProtectYourPC>3</ProtectYourPC>
      </OOBE>
      <UserAccounts>
        <LocalAccounts>
          <LocalAccount wcm:action="add">
            <Name>$user</Name>
            <Group>Administrators</Group>
            <Password>
              <Value>$encoded</Value>
              <PlainText>false</PlainText>
            </Password>
          </LocalAccount>
        </LocalAccounts>
      </UserAccounts>
    </component>
  </settings>
</unattend>
"@
}

function New-CtsSetupCompleteCmd {
    <#
    .SYNOPSIS
    Returns SetupComplete.cmd text: removes the unattend copies that hold the encoded password, writes a
    completion marker under the guest root, and deletes itself.
    #>
    param([Parameter(Mandatory)][string]$GuestRootPath)
    return @"
@echo off
del /f /q "%WINDIR%\Panther\unattend.xml" 2>nul
del /f /q "%WINDIR%\Panther\Unattend\unattend.xml" 2>nul
if not exist "$GuestRootPath" mkdir "$GuestRootPath"
echo %DATE% %TIME%> "$GuestRootPath\setupcomplete.txt"
del /f /q "%~f0"
"@
}

function Get-CtsGoldenImagePath {
    param([Parameter(Mandatory)]$Definition)
    $vhdx = Join-Path (Join-Path $Definition.labRoot 'vhdx') $Definition.goldenImage.fileName
    return [pscustomobject]@{
        Vhdx     = $vhdx
        Partial  = [IO.Path]::ChangeExtension($vhdx, '.partial.vhdx')
        Metadata = [IO.Path]::ChangeExtension($vhdx, '.json')
    }
}

function Get-CtsHostArchitecture {
    $arch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
    switch ($arch) { 'ARM64' { 'arm64' } 'AMD64' { 'amd64' } default { throw "Unsupported host architecture '$arch'." } }
}

function Get-CtsGoldenImagePlan {
    <#
    .SYNOPSIS
    Read-only checks for building the golden image. Returns the plan with Blocked and Reasons.
    #>
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)][string]$IsoPath,
        [long]$MinimumFreeBytes = 30GB
    )
    $paths = Get-CtsGoldenImagePath -Definition $Definition
    $reasons = [System.Collections.Generic.List[string]]::new()
    if (-not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) { $reasons.Add("ISO not found: $IsoPath") }
    if (Test-Path -LiteralPath $paths.Vhdx) { $reasons.Add("Golden image already exists: $($paths.Vhdx). It is never overwritten; record a new fileName in the lab definition to build another.") }
    if (Test-Path -LiteralPath $paths.Partial) { $reasons.Add("A failed build left $($paths.Partial). Inspect, then delete it manually before retrying.") }
    $root = [IO.Path]::GetPathRoot($paths.Vhdx)
    $free = $null
    try { $free = ([IO.DriveInfo]::new($root)).AvailableFreeSpace } catch { $reasons.Add("Cannot read free space on ${root}: $($_.Exception.Message)") }
    if ($null -ne $free -and $free -lt $MinimumFreeBytes) { $reasons.Add(("Only {0:N1} GiB free on {1}; need {2:N0} GiB." -f ($free / 1GB), $root, ($MinimumFreeBytes / 1GB))) }
    $elevated = Test-CtsElevated
    return [pscustomobject]@{
        IsoPath        = $IsoPath
        Edition        = $Definition.goldenImage.edition
        SizeGB         = [int]$Definition.goldenImage.sizeGB
        Architecture   = Get-CtsHostArchitecture
        TargetPath     = $paths.Vhdx
        MetadataPath   = $paths.Metadata
        FreeBytes      = $free
        Elevated       = $elevated
        Blocked        = ($reasons.Count -gt 0)
        Reasons        = @($reasons)
        Steps          = @(
            'Hash the ISO (SHA-256) and mount it read-only'
            "Select the '$($Definition.goldenImage.edition)' index in sources\install.wim or install.esd and check its architecture"
            "Create a dynamic $($Definition.goldenImage.sizeGB) GB VHDX at $($paths.Partial); GPT with ESP (260 MB FAT32), MSR (16 MB), and Windows (NTFS)"
            'Expand-WindowsImage into the Windows partition; bcdboot /f UEFI into the ESP'
            "Dismount; rename to $($paths.Vhdx); write $($paths.Metadata) with SHA-256 values; set the VHDX read-only"
        )
    }
}

function New-CtsGoldenImage {
    <#
    .SYNOPSIS
    Plans (default) or builds (-Apply) the golden VHDX from a Windows ISO. Apply requires elevation.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Definition,
        [Parameter(Mandatory)][string]$IsoPath,
        [switch]$Apply,
        [string]$LogPath
    )
    $plan = Get-CtsGoldenImagePlan -Definition $Definition -IsoPath $IsoPath
    if (-not $Apply) { return $plan }
    if ($plan.Blocked) { throw "Golden image build is blocked: $($plan.Reasons -join ' ')" }
    if (-not $plan.Elevated) { throw 'Building the golden image requires an elevated PowerShell session.' }

    $paths = Get-CtsGoldenImagePath -Definition $Definition
    $log = { param($op, $ok, $detail) Write-CtsLogEntry -LogPath $LogPath -Entry ([ordered]@{ startUtc = [DateTime]::UtcNow.ToString('o'); channel = 'Host'; target = $paths.Vhdx; operation = $op; success = $ok; detail = $detail }) }
    $directory = Split-Path -Parent $paths.Vhdx
    if (-not (Test-Path -LiteralPath $directory)) { $null = New-Item -ItemType Directory -Path $directory -Force }

    $isoHash = (Get-FileHash -LiteralPath $IsoPath -Algorithm SHA256).Hash
    & $log 'HashIso' $true $isoHash
    $isoMounted = $false; $vhdMounted = $false
    try {
        $iso = Mount-DiskImage -ImagePath $IsoPath -Access ReadOnly -StorageType ISO -PassThru -ErrorAction Stop
        $isoMounted = $true
        $isoLetter = ($iso | Get-Volume -ErrorAction Stop).DriveLetter
        $wim = @("${isoLetter}:\sources\install.wim", "${isoLetter}:\sources\install.esd") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if (-not $wim) { throw "No sources\install.wim or install.esd on the ISO." }
        $images = @(Get-WindowsImage -ImagePath $wim -ErrorAction Stop)
        $match = @($images | Where-Object { $_.ImageName -eq $Definition.goldenImage.edition })
        if ($match.Count -ne 1) { throw "Edition '$($Definition.goldenImage.edition)' not found exactly once. Available: $(($images.ImageName) -join ', ')" }
        $info = Get-WindowsImage -ImagePath $wim -Index $match[0].ImageIndex -ErrorAction Stop
        $imageArch = switch ([int]$info.Architecture) { 12 { 'arm64' } 9 { 'amd64' } default { "unknown($($info.Architecture))" } }
        if ($imageArch -ne $plan.Architecture) { throw "Image architecture '$imageArch' does not match host architecture '$($plan.Architecture)'." }
        & $log 'SelectImage' $true "$wim index $($info.ImageIndex) $($info.ImageName) $($info.Version)"

        $null = New-VHD -Path $paths.Partial -SizeBytes ([uint64]$Definition.goldenImage.sizeGB * 1GB) -Dynamic -ErrorAction Stop
        $disk = Mount-VHD -Path $paths.Partial -Passthru -ErrorAction Stop | Get-Disk
        $vhdMounted = $true
        Initialize-Disk -Number $disk.Number -PartitionStyle GPT -ErrorAction Stop
        $esp = New-Partition -DiskNumber $disk.Number -Size 260MB -GptType $script:CtsEspGptType -AssignDriveLetter -ErrorAction Stop
        $null = Format-Volume -Partition $esp -FileSystem FAT32 -NewFileSystemLabel 'System' -Confirm:$false -ErrorAction Stop
        $null = New-Partition -DiskNumber $disk.Number -Size 16MB -GptType $script:CtsMsrGptType -ErrorAction Stop
        $os = New-Partition -DiskNumber $disk.Number -UseMaximumSize -AssignDriveLetter -ErrorAction Stop
        $null = Format-Volume -Partition $os -FileSystem NTFS -NewFileSystemLabel 'Windows' -Confirm:$false -ErrorAction Stop
        $espLetter = (Get-Partition -DiskNumber $disk.Number -PartitionNumber $esp.PartitionNumber).DriveLetter
        $osLetter = (Get-Partition -DiskNumber $disk.Number -PartitionNumber $os.PartitionNumber).DriveLetter
        & $log 'PartitionVhd' $true "ESP ${espLetter}: Windows ${osLetter}:"

        $null = Expand-WindowsImage -ImagePath $wim -Index $info.ImageIndex -ApplyPath "${osLetter}:\" -ErrorAction Stop
        & $log 'ExpandImage' $true $null
        $bcd = & bcdboot.exe "${osLetter}:\Windows" /s "${espLetter}:" /f UEFI 2>&1
        if ($LASTEXITCODE -ne 0) { throw "bcdboot failed ($LASTEXITCODE): $bcd" }
        & $log 'Bcdboot' $true ([string]$bcd)
    }
    catch {
        & $log 'BuildGoldenImage' $false $_.Exception.Message
        throw
    }
    finally {
        if ($vhdMounted) { Dismount-VHD -Path $paths.Partial -ErrorAction SilentlyContinue }
        if ($isoMounted) { $null = Dismount-DiskImage -ImagePath $IsoPath -ErrorAction SilentlyContinue }
    }

    Move-Item -LiteralPath $paths.Partial -Destination $paths.Vhdx -ErrorAction Stop
    $vhdxHash = (Get-FileHash -LiteralPath $paths.Vhdx -Algorithm SHA256).Hash
    $metadata = [ordered]@{
        schemaVersion = 1
        createdUtc    = [DateTime]::UtcNow.ToString('o')
        vhdxPath      = $paths.Vhdx
        vhdxSha256    = $vhdxHash
        isoPath       = $IsoPath
        isoSha256     = $isoHash
        imageIndex    = [int]$info.ImageIndex
        imageName     = $info.ImageName
        imageVersion  = [string]$info.Version
        architecture  = $imageArch
        languages     = @($info.Languages)
    }
    [IO.File]::WriteAllText($paths.Metadata, ($metadata | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
    Set-ItemProperty -LiteralPath $paths.Vhdx -Name IsReadOnly -Value $true
    & $log 'BuildGoldenImage' $true $vhdxHash
    return [pscustomobject]$metadata
}
