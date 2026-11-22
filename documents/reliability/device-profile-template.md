# Reliability Device Profile

Create one profile for each Windows device participating in a reliability
campaign. Run `TestScripts/ctsTraffic_reliability_capture_device_profile.ps1`
on that device. Keep generated profiles with the campaign evidence; they
provide measured system inputs for scenario planning and do not define
scenario-specific load limits by themselves.

## Device inventory

- **Device ID:** {{DEVICE_ID}}
- **Role:** {{DEVICE_ROLE}}
- **System class:** {{SYSTEM_CLASS}}
- **Captured at (UTC):** {{CAPTURED_AT_UTC}}
- **Computer / manufacturer / model:** {{COMPUTER_IDENTITY}}
- **OS/build/architecture:** {{OPERATING_SYSTEM}}
- **CPU model(s):** {{CPU_MODELS}}
- **Physical cores / logical processors:** {{CPU_COUNTS}}
- **Reported L2/L3 cache:** {{CPU_CACHES}}
- **Total RAM:** {{TOTAL_RAM}}
- **Available RAM at capture:** {{AVAILABLE_RAM}}
- **Fixed storage (total/free):** {{FIXED_STORAGE}}

## Network adapters at capture

{{NETWORK_ADAPTERS}}

Use the adapter inventory to record which interfaces, addresses, and routes a
scenario will exercise. Capture a fresh profile when the device configuration
changes materially. For every scenario, calibrate connection counts, IO load,
duration, resource bounds, and stop conditions against the participating
devices; do not treat this inventory snapshot as a load test.

## Scenario parameterization

- **Scenario(s) using this profile:**
- **Selected test interfaces/addresses:**
- **Measured baseline and source:**
- **Candidate workload/resource bounds and measurement source:**
- **Calibration run / evidence:**
