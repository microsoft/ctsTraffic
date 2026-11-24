# Windows Credential Manager access (generic credentials) for the guest administrator account.
# Passwords are only ever held in memory as SecureString/PSCredential; nothing is written to disk by this module.

if (-not ('CtsReliability.NativeCred' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace CtsReliability {
    public static class NativeCred {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct CREDENTIAL {
            public int Flags; public int Type; public string TargetName; public string Comment;
            public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
            public int CredentialBlobSize; public IntPtr CredentialBlob; public int Persist;
            public int AttributeCount; public IntPtr Attributes; public string TargetAlias; public string UserName;
        }
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool CredReadW(string target, int type, int flags, out IntPtr credential);
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool CredWriteW(ref CREDENTIAL credential, int flags);
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool CredDeleteW(string target, int type, int flags);
        [DllImport("advapi32.dll")]
        public static extern void CredFree(IntPtr buffer);

        public const int CRED_TYPE_GENERIC = 1;
        public const int CRED_PERSIST_LOCAL_MACHINE = 2;

        public static string[] Read(string target) {
            IntPtr p;
            if (!CredReadW(target, CRED_TYPE_GENERIC, 0, out p)) {
                int err = Marshal.GetLastWin32Error();
                if (err == 1168) { return null; } // ERROR_NOT_FOUND
                throw new System.ComponentModel.Win32Exception(err);
            }
            try {
                CREDENTIAL c = (CREDENTIAL)Marshal.PtrToStructure(p, typeof(CREDENTIAL));
                string secret = c.CredentialBlobSize > 0 ? Marshal.PtrToStringUni(c.CredentialBlob, c.CredentialBlobSize / 2) : string.Empty;
                return new string[] { c.UserName, secret };
            } finally { CredFree(p); }
        }

        public static void Write(string target, string user, string secret) {
            byte[] blob = System.Text.Encoding.Unicode.GetBytes(secret);
            IntPtr pBlob = Marshal.AllocHGlobal(blob.Length);
            try {
                Marshal.Copy(blob, 0, pBlob, blob.Length);
                CREDENTIAL c = new CREDENTIAL();
                c.Type = CRED_TYPE_GENERIC; c.TargetName = target; c.UserName = user;
                c.CredentialBlob = pBlob; c.CredentialBlobSize = blob.Length; c.Persist = CRED_PERSIST_LOCAL_MACHINE;
                if (!CredWriteW(ref c, 0)) { throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()); }
            } finally {
                for (int i = 0; i < blob.Length; i++) { Marshal.WriteByte(pBlob, i, 0); }
                Marshal.FreeHGlobal(pBlob);
            }
        }

        public static bool Delete(string target) {
            if (CredDeleteW(target, CRED_TYPE_GENERIC, 0)) { return true; }
            int err = Marshal.GetLastWin32Error();
            if (err == 1168) { return false; }
            throw new System.ComponentModel.Win32Exception(err);
        }
    }
}
'@
}

function Get-CtsStoredCredential {
    <#
    .SYNOPSIS
    Reads a generic credential from Windows Credential Manager and returns a PSCredential.
    Throws if the credential does not exist.
    #>
    [CmdletBinding()]
    [OutputType([pscredential])]
    param([Parameter(Mandatory)][string]$Target)
    $entry = [CtsReliability.NativeCred]::Read($Target)
    if ($null -eq $entry) {
        throw "No credential '$Target' in Windows Credential Manager. Run Set-CtsGuestCredential.ps1 first."
    }
    $secure = ConvertTo-SecureString -String $entry[1] -AsPlainText -Force
    $entry[1] = $null
    return [pscredential]::new($entry[0], $secure)
}

function Set-CtsStoredCredential {
    <#
    .SYNOPSIS
    Writes a PSCredential to Windows Credential Manager as a generic credential.
    #>
    [CmdletBinding(SupportsShouldProcess)]
    param(
        [Parameter(Mandatory)][string]$Target,
        [Parameter(Mandatory)][pscredential]$Credential
    )
    if ($PSCmdlet.ShouldProcess($Target, 'Write generic credential')) {
        [CtsReliability.NativeCred]::Write($Target, $Credential.UserName, $Credential.GetNetworkCredential().Password)
    }
}

function Remove-CtsStoredCredential {
    [CmdletBinding(SupportsShouldProcess)]
    param([Parameter(Mandatory)][string]$Target)
    if ($PSCmdlet.ShouldProcess($Target, 'Delete generic credential')) {
        return [CtsReliability.NativeCred]::Delete($Target)
    }
}
