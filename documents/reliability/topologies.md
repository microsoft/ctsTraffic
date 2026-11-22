# Initial VM Topology

Status: **Draft**. The first topology targets three Windows VMs on Hyper-V.
The runbook should support a mesh of up to five VMs. This is not yet an
executable network configuration; define address selection and validate the
required IP families before implementation.

## Goal

Start with multiple VMs that communicate directly on-link, without a router
between test peers. Give each VM-to-VM link its own isolated private virtual
switch and a dedicated VM NIC at each end. This provides independent links for
multi-peer workloads and enables a future scenario to interrupt one path
without intentionally disconnecting every peer.

## Pairwise private-switch model

For the initial three VMs, create one Hyper-V Private switch and one
point-to-point IP subnet for each VM pair. Each VM has one test NIC for every
other VM (two test NICs per VM). The switches connect only the two endpoint VM
NICs and do not provide a host adapter or external-network connection:

```text
VM-A NIC-AB ── Private switch AB ── VM-B NIC-BA
VM-A NIC-AC ── Private switch AC ── VM-C NIC-CA
VM-B NIC-BC ── Private switch BC ── VM-C NIC-CB
```

Use PowerShell Remoting/WinRM on a separate management path to configure and
control the guests and collect artifacts. Do not route that management
connection through the isolated test switches. Control-path churn must neither
modify nor depend on the management interfaces, addresses, routes, neighbors,
or connectivity; the management path remains available independently of the
test links.

## Address and adapter inventory

Maintain a generated or reviewed table for every test NIC:

| VM | NIC identity | Peer | Virtual switch | IPv4 address/prefix | IPv6 address/prefix | Gateway/DNS |
| --- | --- | --- | --- | --- | --- | --- |
| VM-A | NIC-AB | VM-B | Private-AB | Static, range TBD | Static, range TBD | TBD |
| VM-B | NIC-BA | VM-A | Private-AB | Static, range TBD | Static, range TBD | TBD |
| VM-A | NIC-AC | VM-C | Private-AC | Static, range TBD | Static, range TBD | TBD |
| VM-C | NIC-CA | VM-A | Private-AC | Static, range TBD | Static, range TBD | TBD |
| VM-B | NIC-BC | VM-C | Private-BC | Static, range TBD | Static, range TBD | TBD |
| VM-C | NIC-CB | VM-B | Private-BC | Static, range TBD | Static, range TBD | TBD |

Select static test IP addresses and prefixes for each campaign; the ranges do
not need to be permanently reserved. Addresses must be usable and routable
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

Exercise both traffic relationships: traffic that actively uses the address,
route, or neighbor entry being changed, and traffic that uses unaffected
objects on the same test link while other entries are changed. Changes that
leave the active path usable must not interrupt traffic. Deleting or modifying
an object to break connectivity may interrupt traffic, but the scenario must
specify how the affected path and traffic are expected to recover. Test both
in-place updates where supported and delete-and-recreate sequences.

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
policy remain TBD. For each changed object, also record whether the active path
remained usable and therefore traffic was uninterrupted, or whether the change
intentionally broke connectivity and the declared recovery was observed.

The final plan must define:

- Static IPv4 and IPv6 address/prefix selection for each point-to-point link,
  including how the setup phase validates each endpoint pair.
- Whether default gateways and DNS are intentionally absent on isolated test
  NICs; how host/management connectivity remains separate.
- Stable mapping from VM NIC to virtual switch and IP, including how setup
  detects adapter renumbering, duplicate addresses, or stale routes.
- Which address is used by ctsTraffic for each client/server pairing and how
  each path is verified before execution.
- IPv6 link-local addresses are out of scope and must not be used in place of
  the selected static test addresses.

## Setup and verification expectations

For each link, setup should verify the intended peer NICs are attached to the
correct isolated switch and have matching address-family prefix/subnet
mappings; verify reachability in both directions before starting ctsTraffic.
Save switch, adapter, route, and address state as run artifacts. Also verify
the management/control path is not accidentally using the test network.

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

1. What procedure should select a static, routable IPv4 and IPv6 range for
   each campaign?
2. Which management network, WinRM authentication mode, and guest firewall
   policy will be used?
3. What process will supply the per-run Windows version/build and architecture?

IPv4-only and IPv6-only are both required run configurations. Dual-stack
socket testing is also required later, but is blocked until ctsTraffic adds
support for it. Do not mark the networking reliability matrix complete
without this follow-up.
