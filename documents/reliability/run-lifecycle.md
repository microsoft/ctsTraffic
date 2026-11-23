# Reliability Run Lifecycle

Status: **Draft**. This describes the phases and evidence required for a run.
The first vertical slice uses two Hyper-V VMs, then three; the runbook supports
campaigns of up to five VMs. It selects WinRM for remote operations, without yet
specifying a production-ready script or the WinRM security policy.

## Run inputs

Before setup, resolve one immutable run definition containing:

- Run ID, scenario, goal, acceptance criteria, and owner.
- System class selected by the user: `desktop` or `server`.
- VM inventory and roles (up to five VMs); host/hypervisor; switch and static
  IP plan.
- IP-family selection (IPv4-only or IPv6-only) and ctsTraffic IO model.
- Guest OS, build, and architecture for each campaign, plus networking driver,
  ctsTraffic/tool versions, and available resources.
- Verifier state/settings, IO pattern, connection count, transfer size,
  duration, and repetitions, plus the ctsTraffic verification mode:
  `-Verify:data` for Goal 1/3 and post-stress checks; `-Verify:connection`
  only for declared Goal 2 stress workloads.
- Campaign-calibrated load level, system-specific capacity assumptions,
  monitored resource bounds, and stop conditions.
- Fault schedule (if applicable), observation plan, cleanup/recovery policy,
  and required artifacts.
- ctsTraffic CSV policy: include connection-result CSVs when connection
  success rates or individual connection outcomes are acceptance measures
  (using `-ConnectionFilename`); omit noisy per-connection CSVs for
  intentional-failure stress when they do not inform the result, and retain
  aggregate status/error evidence.
- For a control-path churn scenario, the exact declared IP-address, route,
  and/or neighbor add/update/remove operations, their targets and order, plus
  whether traffic uses each changed object or an unaffected object on the same
  link. Define in-place updates where supported versus remove-and-recreate
  sequences, expected interruption for changes that break usability, and the
  recovery plan. Record the finite ctsTraffic test-connection target
  (`connections * iterations`), attempted versus successfully completed
  connections, expected replacement attempts, per-scenario existing-connection
  versus replacement-connection recovery requirement, and allowed rare-stall
  threshold. Commands, tools, object counts, operation rates, timing, and
  acceptance thresholds remain TBD until selected for the campaign.

Capture the resolved inputs with the results. A run must not silently proceed
with missing or changed required configuration.

## Phase 1 — Setup

1. **Reserve and baseline the environment.** Confirm the VMs are dedicated,
   recoverable test systems; record host and guest identity, OS/build, driver,
   clock/time source, resource capacity, and current health. Create or identify
   a known recovery point according to the agreed VM lifecycle.
2. **Build the topology.** Create the required isolated virtual switches and
   attach the planned VM NICs. Apply unique, documented IP settings per link.
   Record adapter-to-switch and address mappings. Check the intended on-link
   paths and ensure there are no duplicate addresses or unintended routes.
   Configure static IP addresses; do not use IPv6 link-local addresses. The
   initial matrix requires separate IPv4-only and IPv6-only runs.
   Dual-stack runs remain blocked until ctsTraffic supports dual-stack and
   that support is validated.
3. **Stage tools and scripts.** Copy the approved ctsTraffic binaries,
   scenario scripts, support tools, and configuration to each guest. Verify
   versions and file integrity. Record the transfer and destination paths.
4. **Establish remote operations.** Use PowerShell Remoting/WinRM for remote
   guest configuration, process launch/status/stop, and artifact collection.
   Confirm it works on every guest and record the access outcome; do not store
   credentials in run artifacts. The identity, authentication, privilege, and
   firewall policy for WinRM is TBD and must be selected before implementing
   this step.
5. **Configure guest OS, crash capture, and verifiers.** Apply only the
   scenario's declared OS settings. Configure complete memory dumps, injected
   NMI dumps, and WER local dumps on every guest as defined in
   [crash capture and triage](crash-capture-and-triage.md), and verify dump
   space. Attach a KDNET debugger only if the campaign opts in. Apply the
   scenario's named [verifier profile](verifier-profiles.md), capture the
   exact resulting settings, and reboot/wait for a healthy guest when
   required. Never enable a verifier implicitly.
6. **Capture pre-run evidence.** Save baseline health, verifier state,
   relevant performance counters, process/pool observations, and event-log
   position. Validate the planned ctsTraffic role/configuration on all nodes.
7. **Gate execution.** Require every guest reachable, correct NIC/IP mapping,
   expected link connectivity, correct tool/config versions, sufficient free
   resources, and no pending reboot or unexplained health issue. Abort setup
   rather than starting a partial multi-VM run. Before each campaign, select a
   compatible workload configuration or optionally characterize the system to
   derive one. Validate its load and safety envelope. Confirm the workload is
   not below the declared stress target, while remaining within approved
   resource and safety limits and stop conditions. The exact calibration
   signals and thresholds are TBD; this gate cannot be automated until they
   are defined. For G1B, require the approved pool-tag snapshot tool before any
   workload, including characterization pilots.
8. **Prepare control-path churn (when in scope).** Before any test changes,
   capture and retain the baseline IP address, route, and neighbor state for
   each affected test interface/address, along with enough system state to
   restore the declared configuration. Identify the separate WinRM management
   path and its interfaces, addresses, and required routes. Explicitly exclude
   that path from every test mutation; no churn operation may alter or remove
   its configuration or connectivity. Verify management reachability before
   proceeding. Do not begin churn unless the baseline and recovery plan are
   recorded and this isolation gate passes.

## Phase 2 — Execution

1. Start listeners and supporting monitors, then start client workloads using
   an agreed coordinated start barrier. Record actual start times and clock
   offsets.
2. Execute for the scenario's declared duration or completion condition.
   Preserve the exact command line/configuration and per-process exit status.
   Enable and collect ctsTraffic connection-result CSVs only when required by
   the scenario's connection-success/recovery metrics using
   `-ConnectionFilename`; record the reason when intentionally noisy failure
   scenarios omit them.
3. Monitor each guest for the defined health signals: process liveness,
   ctsTraffic status/error outputs, relevant performance counters, verifier
   activity, system events, and bugcheck/reboot evidence. Detect hangs with
   the Hyper-V heartbeat and a PowerShell Direct probe, and force a dump with
   an injected NMI when the hang timeout expires and no debugger is attached.
   A counter spot-check is diagnostic evidence, not proof by itself that the
   workload succeeded.
4. Apply scheduled faults only at the declared target and time. Record
   trigger, duration, path affected, confirmation of restoration, and observed
   behavior. Do not inject a fault when the run's setup gate is not satisfied.
5. On a crash, verifier finding, monitor loss, or other stop condition, record
   the incident and preserve volatile evidence where possible before cleanup.
   Avoid automatic retries that could overwrite first-failure evidence.
6. Use the scenario's declared ctsTraffic verification mode (`-Verify:data`
   except for declared Goal 2 stress workloads) and retain its verification
   results as part of the run evidence.
7. **Exercise control-path churn (when in scope).** First establish and verify
   active ctsTraffic through the specifically affected test interfaces and
   addresses; record the mapping and traffic-health evidence. Apply only the
   declared IP-address, route, and/or neighbor add/update/remove operations
   while that traffic is active, in the declared order and scope. Monitor and
   timestamp each operation's outcome, traffic health and errors, system and
   verifier state, and convergence toward the declared network state. If an
   operation leaves the active path usable, any connection failure counts as
   an interruption; record it even if ctsTraffic retries. Brief no-progress
   stalls should be rare and may be allowed only within the campaign's
   declared limit. If an operation deletes or modifies an object to break
   connectivity, record expected failures and replacement attempts. ctsTraffic
   immediately attempts another connection until its configured `connections *
   iterations` test-connection target is met. Record attempted and successfully
   completed connections separately; retries do not erase earlier failures.
   After restoration, verify recovery according to the scenario's declared
   contract and successful-completion criteria. Stop issuing mutations safely
   on a stop condition, management-path risk, loss of required monitoring, or
   other declared abort trigger; preserve the observed state and do not
   continue blindly. Exact commands/tools, counts, rates, timing, interruption
   expectations, and thresholds are TBD.

## Phase 3 — Cooldown and recovery

1. Stop ctsTraffic and scenario helpers using the configured clean shutdown
   path. Record processes that do not stop and any forced termination.
2. Wait for connections and helper processes to drain and for guests to reach
   the scenario's defined quiescent condition. Record the wait duration and
   any residual activity; do not equate elapsed time alone with quiescence.
3. Capture post-run performance counters, user-mode process memory, kernel
   pool observations (using the approved pool-tag tool), verifier
   output, event/system logs, crash dumps, and ctsTraffic outputs before
   resetting or restoring the VM. For leak measurements, follow the
   quiescence gate and snapshot series in
   [leak detection](leak-detection.md).
4. For Goal 2, restore verifier state as specified, reboot if needed, confirm
   guest and network health, and run the designated simple verification
   workload. Capture its complete result as the recovery gate.
5. **Close out control-path churn (when in scope).** Capture post-churn IP
   address, route, and neighbor state for the affected test interfaces and
   addresses, operation and convergence records, traffic/health observations,
   and relevant logs before restoring configuration. For any declared path
   break, restore the expected configuration using the recovery plan and
   verify that affected traffic recovers and meets its declared completion
   criteria; the configured `connections * iterations` target counts test
   connections, not necessarily successes. Also verify the final configuration
   state and management-path reachability. Run and record the designated
   post-run traffic and health verification; retain its commands, outputs,
   outcomes, and residual deviations as artifacts. Do not mark recovery
   successful if expected configuration, traffic, or health is not verified.
6. Collect artifacts from all VMs into the run's uniquely named result
   directory. Verify expected files arrived and are readable; generate a
   manifest and a summary linking each metric to its source artifact.
7. Assign each failure a [triage bucket](crash-capture-and-triage.md) and
   mark the run **passed**, **failed**, **inconclusive**, or **aborted** using
   the scenario's criteria. Preserve failures and deviations; do not convert
   missing evidence into a pass.

For the first G1B workflow, retain the dedicated VMs and switches for reuse
after cooldown. Capture evidence and verify the approved post-setup state;
do not automatically revert checkpoints, reimage guests, or delete resources.
See the [readiness plan](end-to-end-readiness-plan.md) for provisioning,
partial-failure handling, and reuse gates.

## Minimum result bundle

- Resolved run definition, topology/address map, and tool/config versions.
- Setup gate results and pre-run baselines.
- Per-VM ctsTraffic commands, outputs, and exit codes; connection-result CSVs
  when connection success/recovery is measured, or the documented reason they
  were omitted for an intentional-failure stress scenario.
- Time-stamped monitor/counter data and fault-injection record.
- Post-run counters, process memory, pool evidence, verifier/event logs, and
  crash artifacts, including explicit "not collected" reasons.
- Post-stress verification result when required.
- Machine-readable artifact manifest and human-readable run summary with
  acceptance criteria, outcome, deviations, and incident links.

The exact counter set, capture interval, pool tool/version, result format,
retention, and quiescence/recovery thresholds remain to be selected.
