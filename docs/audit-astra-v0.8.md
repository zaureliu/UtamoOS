# Astra v0.8 networking audit

Status: **GREEN**. The complete clean candidate matrix passed all 40 headless
VMs (13,551 assertions); final 0.8.0 confirmation passed three VMs (587).
Final clean host/ELF passed 36,527 / 1,987 assertions. Total: 52,652,
zero gate failures. Last known good is 81a01cef24bd9461b0a6f221e966507a2c0344cb.
See [the final record](validation-astra-v0.8.json).

## Reviewed ownership and boundaries

- E1000 admission is limited to PCI 8086:100e. BAR sizing and the existing
  supervisor UC MMIO policy precede register access. PCI preparation disables
  bus mastering before reset; RX/TX engines and interrupts are disabled before
  publishing new ring addresses.
- Six contiguous owned pages contain descriptor rings, eight separate RX
  buffers and one synchronous TX buffer. Ring lengths and alignment satisfy
  the selected controller layout. RX copying derives its address from the
  software index, never a device-supplied address. DD/EOP, error and length
  checks precede copying; split or oversized frames are dropped and credits
  are returned. TX pads short Ethernet frames with zeroes, publishes through
  a fence and waits for descriptor completion before reusing the buffer.
- Early translation failure frees unpublished DMA. Publication and ambiguous
  hardware failure retain the allocation permanently. TX timeout/error
  quarantines the device, disables RX/TX, rejects further operations and
  preserves potentially device-owned memory. Link loss alone does not
  quarantine the controller. Polling has both timer and iteration bounds.
- Ethernet, ARP, IPv4, ICMP and UDP parsing establishes bounds before endian
  reads or copies. IPv4 header checksums, ICMP checksums and present UDP
  checksums are verified; an omitted UDP checksum is accepted for IPv4.
  IPv4 options and fragmentation are rejected explicitly. Outgoing UDP
  packets include the pseudo-header checksum.
- ARP admission checks matching Ethernet/sender MACs, local-subnet host
  addresses and target ownership. Replies populate the cache only for a
  pending or existing peer. IPv4 traffic from this host's own address or its
  subnet's network/broadcast addresses cannot trigger echo responses.
- DHCP options have bounded lengths, mandatory fields and duplicate rejection.
  Transaction ID, client MAC, message type, server/source and ACK address
  matching precede configuration publication. Failed acquisition leaves no
  partial configuration. Lease expiry clears configuration and ARP entries.
  Gateway and same-subnet service endpoints must be usable host addresses;
  client self-addresses are rejected for gateway, DNS and server endpoints.
- DNS verifies transaction ID, question and response flags before accepting
  bounded A/CNAME records. Compression follows backward pointers with a hop
  limit; malformed labels, encoded lengths, self/forward pointers and CNAME
  cycles cannot run unbounded. Rejected responses preserve the result address.
- The network stack has one owning kernel thread, fixed storage and no heap
  allocations or network work in IRQ/NMI handlers. Transactions match the
  expected peer, ports and echo payload and clear their active binding on
  exit. Polling is bounded and its clock yields while waiting.
- Shell commands validate argument count and UDP-port range. The network gate
  uses the existing exclusively headless VM owner, bounded QMP/GDB operations
  and process reaping. Its UDP/DNS fixture binds only WSL host loopback,
  closes its socket and joins its thread. The capture writer is stopped
  before reading the complete pcap.

No additional gate-blocking ownership or bounds defect was found in this
review after the corrections below. This is local engineering evidence for
the selected single-BSP/Q35/E1000 scope.

## Corrections retained in the candidate

The preliminary link-restoration test failed because emulated link negotiation
completes asynchronously. DHCP now gives link restoration a bounded two-second
opportunity before consuming acquisition attempts. The fix does not disable
the link-loss check or weaken the gate.

Review also identified accepted DHCP gateway network/broadcast addresses that
the routing layer could never use. Validation now rejects those values,
same-subnet DNS/server network/broadcast addresses and service addresses equal
to the assigned client address. Remote DNS/server host bits are not inferred
from the client's subnet mask. Negative packet tests cover these rejections.

The earlier preliminary first network VM recorded 197 assertions with one
failure; the recovery VM passed all 266 assertions. Both were reaped. Their
original reports, serial logs and captures are retained under
`validation-artifacts/astra-before-v08-clean-20260914T144940Z/validation/`,
in `astra-v08-net-first/` and `astra-v08-net-recovery/`. Neither preliminary
run is included in passing candidate-gate totals.

Two preliminary build invocations used an unresolved or mistyped cross-compiler
prefix. They failed before compilation and were rerun with the existing
project-local x86_64-elf toolchain. Their logs remain in validation-artifacts;
they are not evidence of a completed build or part of passing gate counts.

## Completed evidence

The candidate was frozen after the clean build at
`validation-artifacts/astra-v08-candidate-20260914T144940Z/`.
Its source manifest, kernel ELF, ISO, initramfs, user binaries and build logs
are retained together. The candidate carries version 0.7.0; the final clean stamp is 0.8.0.

- Clean host suite: **36,527 assertions**, zero failures.
- ELF inspection: **1,987 assertions**, comprising 1,874 kernel and 113
  userspace assertions, zero failures.
- All six new network configurations passed: 64/256/512 MiB, NX-off,
  alternate subnet/MAC and absent NIC. The full 40-VM matrix also includes
  storage, VFS/ELF, processes, scheduler, allocators and fatal probes.
- Final confirmation passed networking with AHCI/FAT32, an alternate
  subnet/MAC without NX, and kernel RO protection. All 43 gate VMs were reaped.
  One preliminary pass and one historical failure remain separate (45 attempts).
- Final clean runtime comparison found identical kernel text/data/requests/BSS,
  all seven complete user ELFs and initramfs. Only one kernel rodata version
  byte changed. The full RAM matrix was not repeated after stamping.
  Final host/ELF count once plus both QEMU phases; assertions are not coverage.

The network suite exercises real E1000 DMA traffic, DHCP acquisition and
reacquisition, gateway ping, local DNS A resolution, a malformed compression
reply, three 16-round ping/DNS/UDP stress runs and link-loss recovery. It
compares exact PMM/heap/VMM accounting and checks protocol traffic in pcap.
DNS fixture queries use an explicit gateway/port endpoint; this does not
claim validation of public DNS or internet connectivity.

Supplemental ASan+UBSan evidence is frozen in the candidate's `sanitizers/`:
packet tests 3,830 assertions, transaction tests 1,102 and eight actual-driver
MMIO/DMA scenarios totaling 1,548, all with zero failures. The driver scenarios
are normal operation, reset timeout, invalid MAC, translation failure,
DMA-enable failure, TX timeout, TX error and link down. These repeated
sanitized assertions are **excluded from the main gate totals**.

## Stack review

The candidate's `stack-usage/` records compiler `-fstack-usage` output.
The largest network frame is `dns_decode_reply` at 8,896 bytes. Other relevant
frames are `net_poll` 3,200, `send_ip` 1,600, `send_ethernet` 1,584,
`send_udp` 1,584, UDP checksum 1,536 and `net_resolve` 1,120 bytes.

Review of the bounded, nonrecursive call paths places these network operations
within the existing 64 KiB kernel stacks. These are compiler frame estimates
and a call-path review, not a runtime high-water measurement or a measured
maximum including every interrupt entry.

## Deliberate limits

Only the selected E1000 device, fixed 1,500-byte MTU, IPv4 unicast and bounded
polling are implemented. There is no background receiver, NIC interrupt path,
SMP ownership model, VLAN, IPv6, IP fragmentation/reassembly, IPv4 options,
multicast, socket syscall interface or controller recovery after quarantine.
Unsolicited traffic is processed when network polling runs.

DHCP supports direct discovery/request acquisition with required subnet,
gateway, DNS, lease and server options. Automatic renewal/rebinding, relay,
BOOTP option overload, address-conflict detection and persistent leases are
outside this client. Manual reacquisition replaces the previous configuration.

DNS supports bounded UDP A queries and CNAME chains present in the same
response, at most 512-byte messages, 16 answers and 32 total records. There
is no DNS cache, additional-query CNAME traversal, EDNS, DNSSEC validation
or TCP fallback. TCP/HTTP and a userspace shell remain unimplemented optional
campaign work.

The earlier HHDM permission debt, permanent MMIO/DMA reservations, absent IOMMU
and native userspace ABI limits remain unchanged. Physical NICs, UEFI and
visual acceptance require separate manual validation. Quarantined DMA is an
explicit retained reservation, not a successful resource-reclamation claim.
