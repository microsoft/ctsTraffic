# Verifier Profiles

Status: **Draft; profile structure proposed, exact settings TBD**. This
document is the authoritative list of named Application Verifier and Driver
Verifier profiles. Scenarios reference a profile by name rather than listing
settings inline.

## Rules

- Goal 1A (correctness) profiles must **not** include fault injection such as
  low-resource simulation. Injected failures would make the 1A "no unexpected
  failures" criterion meaningless.
- Goal 2 (stress) profiles may include fault injection.
- Goal 1B (baseline) runs with **no** verifier profile; setup verifies that
  both verifiers are off.
- Setup applies the profile, reboots when Driver Verifier requires it, and
  records the effective settings (for example `verifier /querysettings` and
  the Application Verifier settings export) in the run bundle. A run whose
  effective settings differ from its declared profile is invalid.
- Application Verifier comes from the debugger package installed on every
  guest; see [debugging tools](debugging-tools.md). Record its version with
  the effective settings.
- Exact flags must be confirmed against the target Windows build and
  architecture before a profile is approved.

## Candidate target drivers (Driver Verifier)

| Driver | Role | Notes |
| --- | --- | --- |
| `tcpip.sys` | TCP/IP stack | Primary target. |
| `afd.sys` | Winsock kernel provider | Primary target. |
| `netio.sys` | Network I/O subsystem | Primary target. |
| `ndis.sys` | NDIS | Confirm overhead is acceptable. |
| Synthetic NIC miniport | Hyper-V guest network driver | Confirm the exact driver name on the target build. |

## Profiles

| Profile | Used by | Application Verifier (on `ctsTraffic.exe`) | Driver Verifier (on target drivers) |
| --- | --- | --- | --- |
| `AV-DV-CORRECTNESS` | Goal 1A, optional Goal 3 | Basic checks (heaps, handles, locks, memory, TLS, exceptions, thread pool) and networking checks if available on the build; no low-resource simulation | Standard checks plus pool tracking and IRQL checking; no low-resource simulation |
| `AV-DV-LOWRES` | Goal 2 resource pressure | Correctness checks plus low-resource simulation | Standard checks plus randomized low-resource simulation |
| `AV-DV-STRESS` | Goal 2 infrastructure and protocol faults | Same as `AV-DV-CORRECTNESS` | Same as `AV-DV-CORRECTNESS` |
| `NONE` | Goal 1B, post-stress verification | Disabled | Disabled |

A scenario may add Windows services as Application Verifier targets in
addition to `ctsTraffic.exe`. It names each service, and setup applies the
profile's Application Verifier settings to those services only, using the
method below.

## Targeting a specific service hosted in svchost.exe

Many networking services run inside `svchost.exe`. Enabling Application
Verifier on `C:\Windows\System32\svchost.exe` alone applies it to **every**
svchost-hosted service on the system. That is almost never the goal.

To limit it, also set the `VerifierActivationFilter` registry value under the
`svchost.exe` Image File Execution Options key. The value is a
space-delimited set of strings. Application Verifier is activated only in
svchost processes whose command line contains one of those strings.

### Steps

1. **Find the target command line.** Run the service and read its host
   process command line, for example with `tlist`, or
   `Get-CimInstance Win32_Process -Filter "Name='svchost.exe'" | Select-Object ProcessId, CommandLine`.
   For example, the firewall services `BFE` and `mpssvc` share this
   command line:

   ```text
   C:\WINDOWS\system32\svchost.exe -k LocalServiceNoNetworkFirewall -p
   ```

2. **Choose filter strings** that appear only in the target processes'
   command lines. Check every other running svchost command line to make sure
   none of them also match. Every service in a matched process is verified,
   not just the one you are interested in; record all of them.
3. **Enable Application Verifier on `svchost.exe`** with the profile's
   layers, and set the filter. For example, to verify only the firewall
   services:

   ```cmd
   reg add "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\svchost.exe" /v VerifierActivationFilter /t REG_SZ /d " LocalServiceNoNetworkFirewall "
   ```

4. **Restart the targeted services, or reboot the guest,** so their host
   process starts with verification. Some services cannot be restarted on
   their own; reboot in that case.
5. **Verify the scope.** Confirm the verifier is loaded in the targeted svchost
   processes and in no others, for example with `tasklist /m verifier.dll`.
   Record the matching process IDs, command lines, and hosted services. A
   verifier loaded in an unintended svchost process makes the setup invalid.

### Recording and cleanup

- The effective settings recorded for the run include the
  `VerifierActivationFilter` value and the scope check from step 5.
- When the profile is removed, delete the `VerifierActivationFilter` value
  along with the `svchost.exe` Application Verifier settings, then restart the
  services or reboot. A leftover filter, or verifier settings left on
  `svchost.exe` without a filter, are configuration drift.

## Open items

1. Confirm the exact Application Verifier layers and Driver Verifier options
   for each profile on the target build, and their reboot requirements.
2. Decide whether `ndis.sys` and the synthetic NIC miniport are always in
   scope or only in selected campaigns.
3. Decide the low-resource simulation probability and warm-up delay for
   `AV-DV-LOWRES`, and how expected allocation failures are recognized.
4. Decide whether verifier stops break into an attached debugger or are
   logged and continue, consistent with
   [crash capture](crash-capture-and-triage.md).
5. Decide which svchost-hosted networking services, if any, each profile
   targets by default, and confirm their filter strings on the target build.
