# Remote Control Channels and Guest Firewall

Status: **Draft**. This is the authoritative document for how the host controls
test guests and how guest firewalls are configured for test traffic. Other
documents summarize and link here.

## Decision: evaluate both channels on real work

Both PowerShell Direct and PowerShell Remoting over WinRM will be implemented
and compared on the operations the runbook actually performs. Neither is
chosen in advance. The evaluation may select one channel, or keep both with a
documented split of responsibilities.

The lab tooling must call guest operations through one small transport layer,
not through `-VMName` or `-ComputerName` directly. The scenario chooses the
channel with a single input (`PowerShellDirect` or `WinRM`). The minimum
operations are:

- Open a session, and re-open it after a guest reboot.
- Run a command and return its output, errors, and exit status.
- Copy files to the guest and collect files from the guest.
- Start a long-running process, check it, and stop it.
- Close the session.

Each call records the channel, target, start/end time, duration, result, and
error text in the run log.

## Known properties

| Property | PowerShell Direct | WinRM |
| --- | --- | --- |
| Transport | VMBus (`New-PSSession -VMName`); no guest network needed | TCP 5985/5986 over a guest network adapter |
| Network and firewall setup | None | Management vNIC, address, WinRM listener, firewall rule, host client trust (TrustedHosts or HTTPS certificate) |
| Reachability during network faults | Unaffected by test or management NIC state | Depends on the management NIC staying up and isolated from faults |
| Scope | Hyper-V guests on the local host only | Any reachable host: multi-host labs and bare metal |
| Credentials | Guest administrator credential on every connection | Depends on authentication mode; domain or local accounts |
| After guest reboot | Session is lost; reconnect once the guest is up | Session is lost; reconnect once the network and WinRM are up |
| Effect on the system under test | Uses VMBus, not the guest TCP/IP stack | Adds management traffic through the guest TCP/IP stack under test |
| File copy | `Copy-Item -ToSession/-FromSession` over VMBus | `Copy-Item -ToSession/-FromSession` over WinRM |

The comparison also reviewed the separate WESP networking project. That
project has no remote-control code; it copies files into each VM and runs
scripts interactively. Its useful lessons are reflected in the
[unattended run rules](#unattended-run-rules) and the
[firewall policy](#guest-firewall-policy).

## Required lab support for the evaluation

To compare both channels, the first lab includes:

- A management vNIC on each guest connected to a dedicated management switch,
  with static addresses, separate from every test switch. The management
  switch and its addresses are recorded in
  [lab configuration](lab-configuration.md#management-path).
- A WinRM listener in each guest, with its inbound firewall rule scoped to the
  management adapter only.
- A guest administrator credential for PowerShell Direct, and the selected
  WinRM authentication mode.

Credentials are supplied at run time from the source recorded in
[lab configuration](lab-configuration.md#remote-control). They never appear in
command lines, scripts in the repo, or
run artifacts. Any host-side change made for WinRM, such as TrustedHosts, is a
declared setup mutation and is recorded in the setup log.

## Evaluation use cases

Run each case with each channel on the two-VM lab, repeating it enough times
to see failures and timing variation. Record the measurements listed.
RC-07 through RC-09 need network faults and a verifier profile. They are
deferred until Goal 2 tooling exists and must be completed before the first
Goal 2 run; see Phase 3B of the
[readiness plan](end-to-end-readiness-plan.md).

| ID | Use case | Measurements |
| --- | --- | --- |
| RC-01 | Bootstrap a new guest: first connection, configure static IPs, firewall rules, crash capture | Steps needing console or manual action; success; elapsed time |
| RC-02 | Stage tools: copy ctsTraffic, scripts, and support tools to every guest | Throughput, elapsed time, hash verification result |
| RC-03 | Launch the ctsTraffic server and client with redirected output and a coordinated start | Start-time skew between guests; success; elapsed time |
| RC-04 | Monitor during a long run: periodic status and counter polling | Polling success rate, call latency, management traffic on the guest |
| RC-05 | Collect artifacts: CSVs, logs, counter files, pool snapshots, and a large dump-size file | Throughput, elapsed time, completeness |
| RC-06 | Reconnect after a guest reboot, including a verifier-enable reboot | Time until the channel is usable; failures before then |
| RC-07 | Control during host link-loss faults on test links | Whether control continues; errors |
| RC-08 | Control during guest adapter-lifecycle faults on test links | Whether control continues; errors; no effect on the management path |
| RC-09 | Control while the guest runs the low-resource verifier profile | Session failures, latency, timeouts |
| RC-10 | Hang probe used by [crash capture and triage](crash-capture-and-triage.md) | False positive/negative results; probe latency |
| RC-11 | Operate on all guests concurrently | Success; elapsed time compared with sequential operation |

Also record the operator setup effort, credential handling, and any guest or
host change each channel required.

## Selection criteria

Choose per operation, in this order:

1. **Correctness:** completes the operation reliably in every tested condition.
2. **Isolation:** does not depend on, or add load to, the networking paths
   under test during measured or faulted periods.
3. **Recovery:** reconnects predictably after reboots and faults.
4. **Reach:** supports the planned multi-host and bare-metal extensions.
5. **Cost:** setup effort and elapsed time.

Until RC-07 through RC-09 are complete, operations during network faults and
hang probes use PowerShell Direct, because they must not depend on any guest
network path.

G1B work-over-time measurements always record which channel performed polling
during the measured interval, so runs using different channels are not
compared as equivalent.

Record the outcome and supporting data in this document. The outcome closes
the remote-control item in the [open decisions](README.md#open-decisions).

## Unattended run rules

These rules apply to either channel:

- Never pause for input. Scripts used in runs must not call `Read-Host`,
  `ReadKey`, `pause`, or show prompts. A failure records its error and
  returns a non-zero result.
- Start ctsTraffic and helpers with separate standard output and standard
  error files per process, for example with `Start-Process
  -RedirectStandardOutput -RedirectStandardError -PassThru`. Keep these files
  as run evidence.
- Record every process exit code. For ctsTraffic, the exit code is the sum of
  network and protocol errors.
- Stop processes by the documented clean shutdown path first. Use forced
  termination only after a timeout, and record it as a cleanup finding.
- Setup and cleanup never delete previous run output or evidence. Each run
  writes to a new run directory.

## Guest firewall policy

Keep Windows Firewall enabled on every guest. Disabling it changes the packet
path and is not a customer configuration. Add only the rules the scenario
needs.

Network adapters on isolated Private or Internal switches typically have no
identified network and get the Public firewall profile. Rules must apply to
that profile, scoped tightly as described below.

### Rules

All lab rules belong to one rule group, `ctsTraffic Reliability`, so they can
be listed, verified, and removed together.

| Rule | Direction | Scope | Purpose |
| --- | --- | --- | --- |
| ctsTraffic TCP | Inbound, allow | Program: staged `ctsTraffic.exe` path; protocol TCP; local ports from the scenario; interface: test adapters only; remote addresses: test subnets | Server listeners |
| ctsTraffic UDP (deferred) | Inbound, allow | As for TCP, with protocol UDP | Added with UDP scenarios |
| ICMPv4 echo | Inbound, allow | Echo request; test adapters; test subnets | Reachability checks |
| ICMPv6 echo | Inbound, allow | Echo request; test adapters; test subnets | Reachability checks |
| WinRM | Inbound, allow | TCP 5985 or 5986; management adapter only; management subnet | Only when the WinRM channel is used |

Outbound traffic is allowed by the default policy and needs no rule. IPv6
Neighbor Discovery is covered by the built-in Core Networking rules; verify
those rules are enabled rather than adding new ones.

### Setup

1. Read and record each guest's firewall state before any change: profile
   settings per profile, network profile per adapter, and existing rules in
   the `ctsTraffic Reliability` group.
2. Create or update the rules above from the scenario's ports, adapters, and
   subnets. Record each change as a setup mutation.
3. Verify the effective rules: rule enabled, profile, program, port, interface,
   and address filters. Then verify reachability on every test link with ICMP
   and a short ctsTraffic connection before the run.

### Cleanup

The firewall rules are part of the retained lab, like the VMs and switches.
Cleanup keeps them and verifies they still match the post-setup state.
Remove the rule group only when the lab is torn down, or when a later scenario
needs different rules; record that removal as a mutation.

### Diagnostics

If a reachability check fails, check the adapter's network profile and the
effective rule filters first. Firewall drop logging may be enabled temporarily
for diagnosis. Record it as a mutation, and turn it off before measured runs.
