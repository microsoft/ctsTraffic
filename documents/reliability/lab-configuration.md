# Lab Configuration (v1)

Status: **Approved for the first G1B runs**. This is the authoritative record
of the lab-wide settings for the first Hyper-V lab. Topic documents own the
rules; this document records the values chosen for this lab. A change to any
value here is a recorded configuration change, and runs on either side of it
are not compared as if nothing had changed.

## Host

| Setting | Value |
| --- | --- |
| Host | Windows 11 Insider build (recorded per run), ARM64, 12 cores, 31.6 GiB RAM |
| Lab root | `C:\ctsReliability` (outside the repository) |
| Run result bundles | `C:\ctsReliability\runs\<runId>` |
| Debugger package cache | `C:\ctsReliability\cache\dbg`; see [debugging tools](debugging-tools.md) |
| Guest images | `C:\ctsReliability\vhdx` |
| Debugger package on host | Installed, for dump analysis and optional KDNET |

The host's own networks must not overlap any lab subnet that the host can
see. Setup checks the management subnet against every host adapter and fails
on an overlap. The Private test switches are not visible to the host.

## Guest image

- Source: the public Windows 11 Arm64 multi-edition ISO from
  [microsoft.com](https://www.microsoft.com/en-us/software-download/windows11arm64).
- Target: Windows 11 24H2 (build 26100). The public download serves the
  current release, which may be newer; record the actual edition, version,
  build, and UBR after installation. A build other than 26100 is acceptable
  only if recorded as the run's guest build.
- Build the golden VHDX under `C:\ctsReliability\vhdx` from the ISO with
  `New-CtsGoldenImage.ps1`, without booting it. The script applies the Windows
  11 Pro edition from `install.wim` to the VHDX and makes it bootable. An
  image that has never booted is already in the generalized out-of-box state,
  so no sysprep is needed. The script records the ISO and VHDX SHA-256 in a
  metadata JSON file beside the VHDX, and marks the VHDX read-only. Every
  test VM boots a differencing disk of that golden image. Each VM's first
  boot completes OOBE from an injected unattend file that sets its computer
  name and the guest account.

## Test VMs

| Setting | Value |
| --- | --- |
| Count | 2 for the first runs (VM-A, VM-B), then 3; up to 5 |
| Generation | 2 |
| vCPU | 4 |
| Memory | 8 GiB, **static** (dynamic memory disabled so memory and pool measurements are not perturbed) |
| Checkpoints | Automatic checkpoints disabled. One clean checkpoint after provisioning and setup verification; revert is manual only, never automatic |
| Crash capture | Per [crash capture and triage](crash-capture-and-triage.md) |
| Guest settings | Applied and verified by provisioning: static adapter addresses by MAC, the `ctsTraffic Reliability` firewall rules, complete dumps with a page file of memory + 1 GiB, WER local dumps, High performance power plan with hibernation off, Windows Update disabled by policy, and Defender real-time protection off. Tamper Protection can block the Defender change; provisioning then records a finding instead of failing. |

Provisioning usage is in the
[`TestScripts/Reliability` README](../../TestScripts/Reliability/README.md);
its tests are in [reliability tooling tests](tooling-tests.md).

Two VMs use 8 of 12 cores and 16 GiB of the host's memory; three use all 12
cores and 24 GiB. Record host CPU and memory load during every run, and treat
host saturation as host-side noise per the [README](README.md#planning-guardrails).

## Management path

- One Internal switch, `ctsRel-Mgmt`, with a host vNIC.
- IPv4 only: `192.168.250.0/24`. Host is `.254`; VM-A is `.1`, VM-B `.2`,
  and so on. No gateway or DNS on guest management adapters.
- Used for WinRM evaluation only; never for test traffic. Test addressing is
  in [topologies](topologies.md#address-and-adapter-inventory).

## Remote control

The channel used for runs is chosen by the Phase 3B evaluation in
[remote control and firewall](remote-control-and-firewall.md). Settings for
both channels:

| Setting | Value |
| --- | --- |
| Guest account | Local administrator `ctsadmin`, same name on every guest |
| Credential source | Windows Credential Manager generic credential `ctsReliability/guest` on the host, stored with `Set-CtsGuestCredential.ps1` and read at run time. Never written to scripts, logs, or artifacts. The unattend file holds only the encoded form, and first-boot setup deletes it. |
| WinRM | HTTP (5985) on the management adapter only, Negotiate authentication with the local account |
| Host client trust | `TrustedHosts` set to exactly the guest management addresses; recorded as a host setup mutation |

HTTP is acceptable only because the management switch is Internal and
isolated; WinRM still encrypts the session with Negotiate. Revisit this if the
lab moves to multiple hosts.
