# Reliability Scenario Template

Use one copy per scenario/campaign. Select exactly one system class:
`desktop` or `server`. The class is an input for campaign planning; it does not
by itself define connection counts, load limits, or success thresholds.

Run `TestScripts/ctsTraffic_reliability_populate_template.ps1` to copy this
template and capture the host profile. The generated copy is written only to
the requested output path; keep machine-specific output outside the repository
unless it is intentionally being checked in.

Run `TestScripts/ctsTraffic_reliability_capture_device_profile.ps1` on each
participating Windows device to capture its inventory and current network
configuration. Attach or reference those generated profiles below. Device
class and inventory help scope a run; scenario load parameters still require
calibration and must not be inferred from class alone.

## Scenario

- **System class:** {{SYSTEM_CLASS}}
- **Goal / scenario ID:**
- **Captured at (UTC):** {{CAPTURED_AT_UTC}}
- **Hypervisor:** {{HYPERVISOR}}
- **Host OS/build/architecture:** {{HOST_OS}}
- **CPU model(s):** {{CPU_MODELS}}
- **Physical cores / logical processors:** {{CPU_COUNTS}}
- **Reported L2/L3 cache:** {{CPU_CACHES}}
- **Total host RAM:** {{TOTAL_RAM}}
- **Fixed storage (total/free):** {{FIXED_STORAGE}}
- **Guest image:** {{GUEST_IMAGE}}
- **Guest VHDX path:** {{VHDX_PATH}}
- **Guest OS edition/version/build/architecture after install:**
- **VM count/roles and per-VM vCPU/RAM:**
- **Verifier profile** ([options](verifier-profiles.md)):
- **ctsTraffic verification mode** (`-Verify:data`, or `-Verify:connection`
  for declared Goal 2 stress only, with reason):
- **Duration (`-TimeLimit`) and iterations:**
- **Fault type** (none, resource pressure, link loss, adapter lifecycle,
  protocol):
- **KDNET debugger attached** (yes/no; complete dumps are always on):
- **Workload or configuration changes:**
- **Expected behavior:**

## Participating device profiles

| Device/profile ID | Role (host, DUT, peer, controller) | Profile file | Scenario-specific resources or settings |
| --- | --- | --- | --- |
|  |  |  |  |

## Measurements and outcome

| Measurement | Source / unit | Result | Success criterion |
| --- | --- | --- | --- |
|  |  |  |  |

Every success criterion must be supported by a recorded measurement. Define
the measurement source and unit before the run; mark unestablished numeric
thresholds as TBD and use pilot results or an explicit customer requirement to
set them before treating the scenario as pass/fail.

## Safety and evidence

- **Stop conditions / recovery:**
- **Artifacts:** include ctsTraffic connection CSVs when connection success or
  recovery is measured; otherwise state why they are omitted.
