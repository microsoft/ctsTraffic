<#
.SYNOPSIS
Stores the lab guest administrator credential in Windows Credential Manager.

.DESCRIPTION
Prompts with Get-Credential (unless -Credential is supplied) and writes a generic credential named by
guest.credentialTarget in the lab definition. The user name must match guest.adminUserName. The password is
never written to disk, logs, or output.

.PARAMETER LabDefinitionPath
Lab definition JSON. Defaults to lab.v1.json next to this script.

.PARAMETER Credential
Credential to store. When omitted, the script prompts.

.EXAMPLE
.\TestScripts\Reliability\Set-CtsGuestCredential.ps1
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$LabDefinitionPath,
    [pscredential]$Credential
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$modulePath = Join-Path $PSScriptRoot 'CtsReliability.psm1'
if (-not (Get-Module -Name CtsReliability | Where-Object { $_.Path -eq $modulePath })) { Import-Module $modulePath -Force }

$definition = if ($LabDefinitionPath) { Get-CtsLabDefinition -Path $LabDefinitionPath } else { Get-CtsLabDefinition }
$expectedUser = $definition.guest.adminUserName
if (-not $Credential) {
    $Credential = Get-Credential -UserName $expectedUser -Message "Password for the lab guest account '$expectedUser'"
}
if (-not $Credential) { throw 'No credential was entered.' }
if (($Credential.UserName -replace '^\.\\', '') -ne $expectedUser) {
    throw "User name '$($Credential.UserName)' does not match guest.adminUserName '$expectedUser'."
}
if ($Credential.GetNetworkCredential().Password.Length -eq 0) { throw 'The password is empty.' }

if ($PSCmdlet.ShouldProcess($definition.guest.credentialTarget, 'Store guest credential')) {
    Set-CtsStoredCredential -Target $definition.guest.credentialTarget -Credential $Credential
    Write-Output "Stored credential for '$expectedUser' as '$($definition.guest.credentialTarget)'."
}
