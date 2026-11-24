# Reliability Tooling Tests

Status: **Current** for the uncommitted `TestScripts/Reliability` tooling.
This document covers the tests that prove the lab scripts work. It does not
cover ctsTraffic reliability scenarios; those are defined in
[goals and scenarios](goals-and-scenarios.md).

## Why these tests exist

The reliability lab scripts create and change Hyper-V switches, VMs, disks,
guest firewall rules, registry settings and credentials. A scripting bug can
cost a long run, damage unrelated host resources, or produce evidence that is
quietly wrong. The tests exist so that:

- **Every script is proven before it touches a real lab.** Each module source
  file and each entry script has a test file. A coverage test fails if a new
  script is added without one.
- **Plan and apply stay separate.** The tests check that every changing
  command plans by default, refuses to apply a blocked plan, and refuses to
  apply when not elevated.
- **Unsafe states are refused.** These include name collisions, unowned
  resources, drift, subnet overlap, missing images, an existing output file,
  a downgraded debugger, and a password left in an unattend file.
- **Evidence formats stay stable.** Snapshot schema validation, manifest
  hashes, drift comparison and pool-tag parsing are checked against fixed
  inputs.
- **Both PowerShell editions keep working.** The host may run the scripts
  under PowerShell 7, while some guest-side code runs under Windows PowerShell
  5.1, so every test runs under both.
- **Anyone can rerun them.** The tests, the runner and this document are in
  the repository. Nothing depends on a session folder or a particular
  machine.

Most tests mock Hyper-V, CIM, the firewall, storage and the guest session.
Mocked tests prove the script's decisions and arguments, not that Windows
behaves as assumed. Behavior that only a real elevated lab can prove is listed
under [manual verification](#manual-verification-not-automated).

## How to run

Requirements:

- Pester 5 or later in each edition you run. The results below used Pester
  6.2.0 in PowerShell 7 and Pester 5.7.1 in Windows PowerShell 5.1.
- No elevation is needed. Elevated-only tests skip themselves.
- The Hyper-V PowerShell module must be installed for the provisioning tests.
  They mock every Hyper-V cmdlet, but Pester can only mock a command that
  exists. Without the module, those tests are skipped.

Run everything under both editions from the repository root:

```powershell
.\TestScripts\Reliability\Tests\Invoke-CtsReliabilityTests.ps1
```

The runner starts a clean `-NoProfile` child process per edition and writes
these files to `TestScripts\Reliability\Tests\results\<utc>\`, which git
ignores:

| File | Contents |
| --- | --- |
| `summary.md` | Totals per edition, per-file counts, and failed test names |
| `<edition>.nunit.xml` | NUnit XML for tools or pipelines |
| `<edition>.summary.json` | Machine-readable counts |

The runner exits with code 1 if any test fails or an edition cannot run.

| Option | Effect |
| --- | --- |
| `-Edition pwsh` or `-Edition powershell` | Run one edition only |
| `-IncludeShare` | Also run tests that read the real debugger MSI from the internal share |
| `-ExcludeLive` | Skip tests that read the real local machine |
| `-Path <file>` | Run selected test files |

To run one file interactively:

```powershell
Invoke-Pester .\TestScripts\Reliability\Tests\Provision.Tests.ps1 -Output Detailed
```

### Tags

| Tag | Meaning | Default |
| --- | --- | --- |
| *(none)* | Fully mocked or uses only `TestDrive` and an HKCU test key | Run |
| `Live` | Reads the real local machine (CIM inventory, Credential Manager, the installed debugger, a guest-config dry run on the host) without changing configuration | Run |
| `Elevated` | Needs an administrator session (real Hyper-V inventory, real pool tags); skips itself otherwise | Run, but skipped when not elevated |
| `Share` | Reads the real MSI on the internal share | Excluded unless `-IncludeShare` |

The `Live` guest-config dry run only reads settings. Apply mode of the guest
script is never run on the host.

## What each test file proves

All files are in `TestScripts\Reliability\Tests\`.

| File | Covers | What it proves |
| --- | --- | --- |
| `Lab.Tests.ps1` | `src\Lab.ps1` | The lab definition loads and rejects missing or invalid fields. IPv4 helpers reject bad CIDRs. Subnet-overlap detection works. For each VM count, the topology has the documented link numbering, VM names, management addresses, and unique static MACs, with peers paired symmetrically. |
| `Credential.Tests.ps1` | `src\Credential.ps1` | Write, read, and remove round trip through Windows Credential Manager, using a dummy target and password (`Live`). |
| `Transport.Tests.ps1` | `src\Transport.ps1` | The JSONL log writes one line per entry. Logged operations record success, and record and rethrow failure. PowerShell Direct uses `-VMName`. WinRM uses Negotiate against the management address. Guest command and copy helpers work. Waiting for a guest returns when it answers, or throws after the timeout. |
| `Inventory.Tests.ps1` | `src\Inventory.ps1` | Local device inventory sections (`Live`). Hyper-V inventory with mocked and real Hyper-V (`Elevated`), including missing VMs. |
| `Snapshot.Tests.ps1` | `src\Snapshot.ps1` | Schema validation, the manifest hash check, and detection of modified or deleted files. Report rendering. Drift comparison, including a single inserted route and the default ignore list. A real host-only snapshot, with no drift between two back-to-back captures (`Live`). |
| `PoolTag.Tests.ps1` | `src\PoolTag.ps1` | Parsing of the 64-bit and 32-bit `SystemPoolTagInformation` layouts. A clear elevation error when only the non-admin placeholder entry is returned. CSV output that refuses to overwrite. Growth comparison and filtering. A real capture (`Elevated`). |
| `DebuggerCache.Tests.ps1` | `src\DebuggerCache.ps1` | Version comparison and MSI property reads. The cache refresh rule: refresh only when the share is reachable and has a newer version, and never downgrade. The previous version is kept. Bad signatures and version mismatches are rejected without leaving partial files. Guest install decisions and exit codes, including 3010 reboot. Query of the installed tools on this machine (`Live`). The real share MSI (`Share`). |
| `GoldenImage.Tests.ps1` | `src\GoldenImage.ps1` | The unattend XML has the right passes and computer name, and the password only in encoded form. XML special characters are escaped. SetupComplete removes unattend copies. The plan blocks on a missing ISO, an existing target, a partial file, or low free space. Apply refuses without elevation or with a blocked plan. A failed build dismounts the ISO and leaves no final VHDX. |
| `GuestConfig.Tests.ps1` | `src\GuestConfig.ps1` | The desired configuration derived from the lab definition. The guest script's dry-run versus apply behavior, Compliant, WouldChange, Changed, Failed and Blocked statuses, and the reboot flag. Registry sections, run against an HKCU test key. Adapter addressing, firewall rules, and Defender with Tamper Protection reported as Blocked, all mocked. A real read-only dry run on the host (`Live`). |
| `Provision.Tests.ps1` | `src\Provision.ps1` | The pure plan's Create, Keep, Drift and Collision decisions and their blocking rules. The ownership journal: lab-ID check, replacement and atomic write. Hyper-V state capture. Apply orchestration with mocked Hyper-V: refuses when blocked or not elevated, creates only planned items, journals each one, and stops at the first failure while keeping evidence. |
| `EntryScripts.Tests.ps1` | Every `TestScripts\Reliability\*.ps1` | Coverage checks: every source file and entry script has a test, and every entry script parses and has help. Each entry script loads the lab definition, plans by default, and passes exactly the intended arguments. Credential checks: user-name match, empty password, `-WhatIf`, and the password is never echoed. The snapshot comparison validates both snapshots before comparing. |
| `Prototypes.Tests.ps1` | `TestScripts\ctsTraffic_reliability_*.ps1` | The device profile and scenario template scripts fill every token, refuse to overwrite a file, refuse a missing directory or a bad VHDX path, and fail when no processor inventory is returned. |
| `Invoke-CtsReliabilityTests.ps1` | — | The runner. It is not itself under test; its output is checked by reading `summary.md` after each run. |

## Recorded results

Host: the lab host, ARM64, not elevated, not in Hyper-V Administrators.
Date: 2026-11-24, runs `20261124T070849Z` and `20261124T070526Z` (Share).

| Edition | PowerShell | Pester | Passed | Failed | Skipped | NotRun |
| --- | --- | --- | --- | --- | --- | --- |
| pwsh | 7.7.0 | 6.2.0 | 220 | 0 | 2 | 2 |
| powershell | 5.1.29715.1000 | 5.7.1 | 220 | 0 | 2 | 2 |
| pwsh with `-IncludeShare` | 7.7.0 | 6.2.0 | 222 | 0 | 2 | 0 |

The per-file counts are the same in both editions:

| File | Total | Passed | Skipped | NotRun |
| --- | --- | --- | --- | --- |
| Credential | 5 | 5 | 0 | 0 |
| DebuggerCache | 29 | 27 | 0 | 2 (Share) |
| EntryScripts | 24 | 24 | 0 | 0 |
| GoldenImage | 19 | 19 | 0 | 0 |
| GuestConfig | 23 | 23 | 0 | 0 |
| Inventory | 10 | 9 | 1 (Elevated) | 0 |
| Lab | 23 | 23 | 0 | 0 |
| PoolTag | 11 | 10 | 1 (Elevated) | 0 |
| Prototypes | 10 | 10 | 0 | 0 |
| Provision | 24 | 24 | 0 | 0 |
| Snapshot | 33 | 33 | 0 | 0 |
| Transport | 13 | 13 | 0 | 0 |

Skipped because the session was not elevated:

- `Get-CtsHyperVInventory (real Hyper-V)`
- `Get-CtsPoolTagSnapshot captures this machine's pool tags when elevated`

Run the suite from an elevated session to cover them, and record the result
here.

### Defects the tests found

These were fixed before the results above were recorded.

| Area | Defect | Fix |
| --- | --- | --- |
| Guest config script | A `-DryRun` switch cannot be bound positionally through `Invoke-Command -ArgumentList`, so the script always ran in its default mode. | `DryRun` is now `[bool]`, defaulting to `$true`. |
| Guest config script | When invoked through the module, the module's strict mode applied and failed on absent optional properties. | The script sets `Set-StrictMode -Off` for itself. |
| Guest config script | An empty list returned from a script block arrives as `$null`, and `@($null).Count` is 1, so empty lists looked non-empty. | Empty checks use `-not`. |
| Guest config script | Windows returns IPv4 firewall remote addresses in mask form (`192.168.1.0/255.255.255.0`), so compliant rules looked changed. | Both sides are normalized before comparison. |
| Test runner | Per-file counts were taken from Pester containers, which hold blocks rather than tests, so every file showed 1. | Counts are grouped by each test's source file. |

## Manual verification not automated

These operations need an elevated session, real Hyper-V and a Windows ISO.
No unelevated test can prove them. Record each result here when performed.

| Item | Command | Proves | Status |
| --- | --- | --- | --- |
| Store guest credential | `Set-CtsGuestCredential.ps1` | The credential is stored under `ctsReliability/guest` | Not yet run |
| Golden image build | `New-CtsGoldenImage.ps1 -IsoPath <iso> -Apply` | The edition applies, bcdboot works for ARM64 from the host, metadata is written, and the VHDX is read-only | Not yet run |
| Unattend injection | Part of provisioning Apply | `Set-CtsVhdUnattend` finds the Windows partition. OOBE completes without prompts, including a product key prompt. SetupComplete removes every password-bearing copy. | Not yet run |
| Lab provisioning | `Invoke-CtsLabProvision.ps1 -Apply` | Switches, host address, disks, VMs and journal entries are created. A second run plans only Keep. | Not yet run |
| Guest phase | Part of provisioning Apply | Guest configuration applies and verifies. `Restart-VM -Wait -For Heartbeat` works. The High performance power plan exists on ARM64. Defender is Blocked only by Tamper Protection. The debugger MSI installs cdb, ntsd and appverif. | Not yet run |
| Elevated tests | Runner from an elevated session | The two Elevated tests pass | Not yet run |
| WinRM channel | Phase 3B | Host TrustedHosts and the WinRM listener work over NIC-Mgmt | Deferred to Phase 3B |

## Maintaining the tests

- Add or change a test with every script change. The coverage tests fail if a
  `src\*.ps1` file has no matching `Tests\*.Tests.ps1`, or if an entry script
  is not named in `EntryScripts.Tests.ps1`.
- Use dummy passwords only. Never put a real lab password in a test, fixture,
  log or result file.
- Mock module-internal calls with `Mock -ModuleName CtsReliability`. Mocks of
  commands called by entry scripts and prototype scripts need no module name.
- In Pester 6, a filtered mock that does not match does not fall through to
  the real command. Prefer unfiltered mocks that branch on their parameters
  for common cmdlets.
- Strongly typed parameters on mocked Hyper-V, storage and firewall cmdlets
  need `-RemoveParameterType`, for example `VM`, `VMNetworkAdapter`,
  `DiskImage` and `AssociatedNetFirewallRule`.
- After a full run, update [recorded results](#recorded-results) with the
  date, host and counts for both editions.
