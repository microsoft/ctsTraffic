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
