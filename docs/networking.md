# Native networking

The v0.8 core implements one Intel 82540EM-compatible E1000 (PCI 8086:100e),
Ethernet, ARP, IPv4, ICMP echo, UDP transactions, a DHCP client and DNS A
resolution. It is a bounded experimental kernel stack. Network commands run
in the existing kernel shell. There are no userspace sockets.

## Ownership and execution

PCI discovery supplies BAR0 and its actual size. The driver maps the aperture
through the existing supervisor UC MMIO window, disables bus mastering while
resetting the device, masks its interrupts and reads its MAC from EEPROM.
Six contiguous PMM pages hold eight receive descriptors, eight transmit
descriptors, eight 2 KiB receive buffers and one shared transmit buffer.
The descriptor arrays have the required 128-byte length and alignment.

Only one transmission is outstanding. The driver pads short Ethernet frames,
requests FCS insertion and waits for descriptor completion before reusing its
buffer. Receive copies derive from the software ring index; the device cannot
supply a CPU pointer. Length, completion, end-of-packet and descriptor errors
are checked before delivery. Split frames are discarded through their final
descriptor. Both rings wrap during the host model and real QEMU stress.

Network DMA stays reserved after publication. A transmit timeout/error
quarantines the device, disables transmit/receive and keeps those pages owned.
The driver does not claim that disabling an engine synchronously ends all DMA.
Before publication, a failed physical-to-virtual translation frees its
unexposed allocation. There is no reset/reclaim/hotplug recovery API.

The bootstrap/shell thread owns the stack. NIC operations briefly suppress
preemption with IRQs enabled. Packet parsing and transactions do not allocate.
Polling drains at most 32 packets per call. Waits yield in 10 ms intervals,
with both a PIT deadline and an iteration bound. There is no network work in
timer/keyboard ISRs and no background network worker. While the shell waits
for keyboard input, queued packets are processed by the next network command;
this is not a continuously available network server.

## Admitted protocol subset

| Layer | Behavior and bounds |
| --- | --- |
| Ethernet | 14–1514 bytes; own/broadcast destination; unicast nonzero source; no VLAN |
| ARP | Ethernet/IPv4 request/reply; eight entries, 60-second lifetime; local host peers only |
| IPv4 | 20-byte header, MTU 1500, validated length/checksum/TTL; DF allowed; fragments/options rejected |
| ICMP | Echo request/reply with full checksum; matching peer, ID, sequence and payload for ping |
| UDP | Nonzero ports, exact datagram length, IPv4 pseudo-header checksum; zero inbound checksum accepted |
| DHCP | DISCOVER/OFFER/REQUEST/ACK; matching XID/MAC/server/assigned address; bounded options |
| DNS | One A/IN question; at most 512 bytes, 16 answers and 32 total records; same-message CNAME chain |

ARP requests use two attempts with 500 ms response windows. Entries are learned
from requests for this host or replies for an outstanding/already known peer.
Routing uses the learned subnet and gateway. Local network/broadcast sources,
the host's own source address and broadcast ping destinations are rejected.

A UDP transaction owns one ephemeral port and matches remote IP, remote port
and local port. Replies cannot overwrite a completed transaction. Response
capacity failure leaves the caller's buffer and received-length output intact.
There is no concurrent endpoint registry, retransmitted UDP delivery guarantee
or general socket API.

DHCP learns the address, mask, gateway, DNS server and lease from packets.
No normal guest address is hardcoded. It waits up to two seconds for link
negotiation, then makes at most three attempts with one-second OFFER/ACK waits.
Unsupported relays, overloaded BOOTP fields, duplicate required options,
invalid masks and unusable local endpoints are rejected. A failed acquisition
leaves the host unconfigured. Lease expiration is enforced on polling; renewal
is explicit with `dhcp`, not automatic T1/T2 renewal. Link restoration permits
a new acquisition without discarding/reallocating the device.

DNS validates the question, transaction ID, flags, all record extents and
the final message length. Compression pointers must move backward and have
a hop bound. CNAME traversal is bounded independently. Rejection preserves the
output address. There is no DNS cache, EDNS, DNSSEC, TCP fallback or follow-up
query for a CNAME whose A record is absent. IDs/ports use a deterministic
noncryptographic generator; this stack does not provide adversarial-network
authentication.

## Commands

| Command | Action |
| --- | --- |
| `netinfo` | NIC/DMA state, DHCP configuration and real packet counters |
| `dhcp` | Acquire a fresh lease with bounded waits |
| `ping gateway` or `ping 10.0.2.2` | Four echo requests |
| `resolve example.com` | Query the DHCP-provided DNS server on UDP port 53 |
| `resolve fixture.test gateway 5300` | Explicit server/port override for local testing |
| `nettest 5300` | Sixteen ping, DNS and generic UDP echo rounds against the test fixture |

A `nettest` port must belong to the local fixture described below. It expects
`fixture.test` to resolve to 192.0.2.123 and a byte-for-byte UDP echo for its
64-byte `UTAMO` payload. These values belong to the explicit self-test, not
normal DHCP or DNS resolution.

## Reproducible headless validation

Use the existing project-local cross compiler. Commands run from the Linux/WSL
repository and do not change system configuration:

```sh
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel inspect iso
python3 scripts/test-network-qemu.py --suite --network --name network-local
python3 scripts/test-network-qemu.py --suite --network --name network-alternate \
  --network-subnet 10.23.0.0/24 --network-mac 52:54:00:ab:cd:ef
python3 scripts/test-network-qemu.py --suite --name network-absent
```

The shared harness enforces one bounded QEMU instance, `-display none`,
serial files, no monitor window and explicit process reaping. Its E1000 uses
QEMU user networking. A Python UDP service binds only the WSL host's
127.0.0.1 on a temporary port and terminates when the suite exits. The guest
accesses it through its learned gateway. Tests never require public DNS or
an internet service. Normal resolution through the DHCP DNS address is
implemented; the gate verifies DNS wire behavior with the explicit local
endpoint override, not an upstream recursive resolver.

Each network VM records serial evidence, JSON assertions and a pcap. The
suite checks DHCP option learning, ping, valid/malformed DNS replies, three
16-round stress runs, exact PMM/heap/VMM accounting, link loss/recovery and
DHCP reacquisition. One configuration also runs readonly AHCI/FAT32 alongside
networking. RAM/NX/subnet/MAC variants and absent-NIC behavior are recorded in
the campaign validation manifest.

Host tests separately cover truncated/corrupt packets, checksums, split
descriptors, ring wrap, injected driver failures, rejected DHCP endpoints,
DNS compression/CNAME cycles, wrong UDP peers, timeouts and lease expiration.
Sanitizer evidence is supplemental and counted separately from the main gate.

## Reference material

Register layout, reset and ring programming were checked against the
[Intel PCI/PCI-X Gigabit Ethernet controller manual](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf).
Wire contracts were checked against [ARP RFC 826](https://www.rfc-editor.org/rfc/rfc826),
[IPv4 RFC 791](https://www.rfc-editor.org/rfc/rfc791),
[ICMP RFC 792](https://www.rfc-editor.org/rfc/rfc792),
[UDP RFC 768](https://www.rfc-editor.org/rfc/rfc768),
[DHCP RFC 2131](https://www.rfc-editor.org/rfc/rfc2131),
[DHCP options RFC 2132](https://www.rfc-editor.org/rfc/rfc2132) and
[DNS RFC 1035](https://www.rfc-editor.org/rfc/rfc1035).
The admitted subset above is narrower than general protocol conformance.

Physical NICs, UEFI, packet flooding, non-QEMU DHCP servers and external
networks remain unvalidated. TCP and HTTP are not implemented.
