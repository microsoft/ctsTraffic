# Initial VM Topology

Status: **Draft**. The first topology is two Windows VMs on Hyper-V joined by
one Private switch, then grows to three. The runbook should support a mesh of
up to five VMs. This is not yet an
executable network configuration; validate the
required IP families before implementation.

## Goal

Start with multiple VMs that communicate directly on-link, without a router
between test peers. Give each VM-to-VM link its own isolated private virtual
switch and a dedicated VM NIC at each end. This provides independent links for
multi-peer workloads and enables a future scenario to interrupt one path
without intentionally disconnecting every peer.

## Pairwise private-switch model

The first vertical slice uses two VMs and a single link, which is the `N = 2`
case of the same model:

```text
VM-A NIC-AB ── Private switch AB ── VM-B NIC-BA
```

For three VMs, create one Hyper-V Private switch and one
point-to-point IP subnet for each VM pair. Each VM has one test NIC for every
other VM (two test NICs per VM). The switches connect only the two endpoint VM
NICs and do not provide a host adapter or external-network connection:

```text
VM-A NIC-AB ── Private switch AB ── VM-B NIC-BA
VM-A NIC-AC ── Private switch AC ── VM-C NIC-CA
VM-B NIC-BC ── Private switch BC ── VM-C NIC-CB
```

Both PowerShell Direct (over VMBus) and WinRM (over a separate management
path) are evaluated as control channels; see
[remote control and firewall](remote-control-and-firewall.md). Each guest has a
management vNIC on a dedicated management switch with static addresses so
WinRM can be tested. Do not route management traffic through the isolated
test switches. Use PowerShell Direct (`Invoke-Command -VMName`) for operations
that may disrupt guest networking, such as adapter-lifecycle faults, because
it does not depend on any guest network path. An optional KDNET debug vNIC,
if used, must also stay off the test switches. Control-path churn must neither
modify nor depend on the management interfaces, addresses, routes, neighbors,
or connectivity; the management path remains available independently of the
test links.

## Address and adapter inventory

Maintain a generated or reviewed table for every test NIC. The two-VM slice
uses only the `Private-AB` rows; the three-VM table is:

| VM | NIC identity | Peer | Virtual switch | IPv4 address/prefix | IPv6 address/prefix | Gateway/DNS |
| --- | --- | --- | --- | --- | --- | --- |
| VM-A | NIC-AB | VM-B | Private-AB | 192.168.1.1/24 | fd00:1::1/64 | None |
| VM-B | NIC-BA | VM-A | Private-AB | 192.168.1.2/24 | fd00:1::2/64 | None |
| VM-A | NIC-AC | VM-C | Private-AC | 192.168.2.1/24 | fd00:2::1/64 | None |
| VM-C | NIC-CA | VM-A | Private-AC | 192.168.2.3/24 | fd00:2::3/64 | None |
| VM-B | NIC-BC | VM-C | Private-BC | 192.168.3.2/24 | fd00:3::2/64 | None |
| VM-C | NIC-CB | VM-B | Private-BC | 192.168.3.3/24 | fd00:3::3/64 | None |

Addressing rule (v1): each pairwise switch has a link number `L` and each VM a
VM number `V` (VM-A = 1 through VM-E = 5). A test NIC's address is
`192.168.L.V/24` and `fd00:L::V/64`. Link numbers are assigned in pair order:
AB = 1, AC = 2, BC = 3, then AD = 4, BD = 5, CD = 6, AE = 7, BE = 8, CE = 9,
DE = 10. Test NICs have no default gateway and no DNS servers. The management
subnet (`192.168.250.0/24`, see [lab configuration](lab-configuration.md#management-path))
is outside this range. A run uses one address family (see
[goals and scenarios](goals-and-scenarios.md)); setup assigns only that
family's test addresses and records it. IPv6 link-local addresses remain but
are never used for test traffic.

Addresses must be usable and routable
between the intended test peers, and IPv6 test addresses must not be
link-local. Each private switch connects only the two peer NICs for its link.
For each enabled address family, those endpoint NICs must have matching
subnet prefixes. Do not attach unrelated NICs to a link switch or mix
unrelated prefixes on a link.

## Control-path churn scope

Plan scenarios that add, update, and remove large numbers of IP addresses,
routes, and neighbor entries while ctsTraffic is actively using the affected
test interfaces and addresses. A test link consists only of its two dedicated
peer NICs on their pairwise private switch. For each enabled family, addresses
on the two NICs must use the same link-specific subnet prefix; churn must not
introduce unrelated prefixes or NICs. The scenario must identify the
interface/address pairs used by active traffic and the intended peer links
before making changes, including when a traffic address itself is changed or
removed.

The traffic relationships (changing objects in use versus unaffected objects),
interruption rules, and recovery contract are defined in
[Goal 3](goals-and-scenarios.md#goal-3--network-configuration-control-path-reliability).
This section defines only which objects churn may touch.

The permitted object scope is limited to the selected test NICs and their
link-specific address/prefix assignments, routes associated with those NICs
and intended peer-link prefixes or peer addresses, and neighbor entries on
those NICs for intended peers. Changes must not affect other peer links or
management objects, including management routes or neighbors, and must not
alter default routes or redirect management traffic. Address and route
selection must continue to distinguish each peer link from management and
other links. The scenario must not rely on the management path to carry test
traffic or to supply a test route or neighbor.

Record before, during, and after state on both endpoints sufficient to prove
the scope: VM/NIC identities and switch attachments; link prefixes and
addresses; interface-associated routes and neighbors; and ctsTraffic
interface/address pairings and activity. Correlate each observed change to the
selected link and compare unaffected peer links and the separate management
path against their baseline. Record any unexpected changes or management-path
dependency as a scope failure. Exact ranges, counts, rates, APIs, and cleanup
policy remain TBD.

The final plan must define:

- How the setup phase validates each endpoint pair's static addresses.
- Stable mapping from VM NIC to virtual switch and IP, including how setup
  detects adapter renumbering, duplicate addresses, or stale routes.
- Which address is used by ctsTraffic for each client/server pairing and how
  each path is verified before execution.

## Setup and verification expectations

For each link, setup verifies the intended peer NICs are attached to the
correct isolated switch and have matching address-family prefix/subnet
mappings, and verifies reachability in both directions before starting
ctsTraffic (measurements SET-03 and SET-04 in the
[readiness plan](end-to-end-readiness-plan.md)). Also verify the
management/control path is not accidentally using the test network.

The topology runbook must define how to handle a failed link check, a failed
VM, a partially configured switch, and cleanup/recreation of the isolated
switches. It must not continue with a partially built mesh.

## Scaling and future topologies

The pairwise mesh requires `N * (N - 1) / 2` virtual switches and `N - 1`
test NICs per VM. The planned maximum of five VMs therefore requires ten
private switches and four test NICs per VM. Verify the host can support this
before setup. A routed or shared-switch topology may be useful later, but
should be a separately named topology with its own address plan and acceptance
criteria rather than a silent variation of this baseline.

## Decisions required

Test addressing is set above; the management switch and its addressing are in
[lab configuration](lab-configuration.md#management-path). The address-family
matrix, including the deferred dual-stack runs, is defined in
[goals and scenarios](goals-and-scenarios.md).
