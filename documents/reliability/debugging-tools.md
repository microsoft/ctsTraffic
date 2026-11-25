# Debugging Tools and Application Verifier Package

Status: **Draft**. This is the authoritative document for how the debugger
package is cached on the Hyper-V host and installed on every test VM. Other
documents summarize and link here.

## Package

Every test VM has the latest debugger package installed. The package
provides:

- User-mode debuggers, including `cdb.exe` and `ntsd.exe`.
- Application Verifier (`appverif.exe` and its verifier layers), used by the
  [verifier profiles](verifier-profiles.md).

The package is architecture-specific. Install the MSI that matches each
guest's OS architecture:

| Guest architecture | Source MSI |
| --- | --- |
| x64 | `dbg_amd64.msi` from the internal debugger share |
| ARM64 | `dbg_arm64.msi` from the internal debugger share |

The share is internal, so its path is not in this repository. Set the full
MSI paths on each host in the local override file
`C:\ctsReliability\lab.local.json`, which is outside the repository:

```json
{
  "debugger": {
    "sources": {
      "amd64": "\\\\<server>\\<path>\\dbg_amd64.msi",
      "arm64": "\\\\<server>\\<path>\\dbg_arm64.msi"
    }
  }
}
```

Values in `lab.local.json` replace those in `lab.v1.json`. Never commit
internal server names or paths. Without a configured source, setup uses the
cached MSI and warns. If nothing is cached, setup fails and says how to
configure the source.

Only the host reads the source share. Guests stay on isolated networks and
never access the share or hold credentials for it. The host stages the MSI
into each guest through the run's control channel; see
[remote control and firewall](remote-control-and-firewall.md).

## Host cache

The host keeps a local cache with the latest MSI for each architecture the lab
uses. Setup always installs from this cache, never directly from the share.

For each cached MSI, the cache stores a metadata record with:

- Architecture, MSI `ProductVersion`, `ProductCode`, and `ProductName`.
- SHA-256 hash and Authenticode signature status and signer.
- Source path and the UTC time it was copied.

Keep the previous cached version alongside the current one, so an earlier run
can be reproduced and a bad package can be rolled back with approval.

The cache location is a lab setting; it must be outside the repository and
outside every run's result directory. The v1 lab uses
`C:\ctsReliability\cache\dbg` ([lab configuration](lab-configuration.md#host)).

### Refresh rule

Run this check for each required architecture at the start of setup, before
any guest is changed:

1. If the source path is not accessible, use the cached MSI. Record that the
   share was unreachable.
2. If it is accessible, read the source MSI's `ProductVersion` without
   installing it. Compare versions numerically, not by file time or size.
3. Refresh the cache only if the source version is newer than the cached
   version, or no cached MSI exists. Copy to a temporary file first. Verify
   that the copy is complete, its Authenticode signature is valid, and its
   version matches what was read. Then replace the cached MSI and update its
   metadata.
4. Never downgrade the cache. If the source has the same version but a
   different hash, keep the cached MSI and record a warning.
5. If no cached MSI exists and the share is not accessible, setup fails for
   that architecture.

Record the check result in the setup log: share reachable or not, source and
cached versions, and whether the cache was refreshed.

## Guest install

Install the package during setup, after tools are staged and before any
verifier profile is applied:

1. Query the installed package version in the guest.
2. If the package is missing or older than the cached version, copy the
   cached MSI to the guest and install it silently with a verbose log, for
   example `msiexec /i <msi> /qn /norestart /l*v <log>`. Do not downgrade a
   newer installed version; record it as drift instead.
3. Treat exit code `0` as success and `3010` as success that requires a
   reboot; reboot and reconnect before continuing. Any other exit code fails
   setup for that guest. Keep the install log as setup evidence.
4. Verify that `cdb.exe`, `ntsd.exe`, and `appverif.exe` exist at the
   installed location, and record their file versions and install path.

All guests of the same architecture in a run must have the same package
version. The package version is part of the run's tool versions. An upgrade
between runs is a recorded configuration change; do not compare work-rate or
leak results across it as if nothing had changed.

Cleanup does not uninstall the package. It stays with the retained lab, like
the VMs, switches, and firewall rules.

## Host install

The host also installs the package from the same cache, using the MSI for the
host's architecture, for dump analysis and optional KDNET debugging. Setup
records the host's installed version. It does not need to match the guests'
version, but a host older than the guests' version produces a warning.
