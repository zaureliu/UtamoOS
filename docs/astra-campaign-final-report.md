# UTAMO OS – Astra Systems Campaign Final Engineering Report

**Campaign complete: v0.8.0 core GREEN.** All required milestones through
native networking passed their local gates. The final kernel audit and clean
validation are complete. All work remains local on astra-campaign; no push,
main merge, release, GUI or tag mutation occurred.

| Metric | Final value |
| --- | --- |
| Campaign branch | `astra-campaign` |
| Highest GREEN version | **v0.8.0 core** |
| Final implementation commit / LAST_KNOWN_GOOD | `81a01cef24bd9461b0a6f221e966507a2c0344cb` |
| Final campaign commit | Commit containing this report, at `astra-campaign HEAD`; exact hash and clean status in [post-commit receipt](../validation-artifacts/astra-final-git.json) |
| Campaign baseline | `f73951185755da40c85467de99749611f6ab66dc` |
| Commits created | 29 through the final implementation, plus this final documentation commit |
| Latest resumption from d847eca | Four implementation/gate/promotion commits, plus this final documentation commit |
| Files created / modified | 128 / 58 |
| Files deleted / renamed | 0 / 0 |
| Unresolved blockers | None within the accepted scope |
| ISO | `build/utamo-os-0.8.0.iso` |
| Frozen last known good | `validation-artifacts/astra-last-known-good/v0.8.0/` |

The [change inventory](astra-campaign-changes.json) lists every created/modified
file and every campaign commit through the final implementation. The separate
post-commit receipt records the final documentation commit without embedding a
self-referential Git hash in this report.

## Subsystem acceptance

“Stable” means the documented single-BSP Q35/TCG gate passed. It does not mean
general compatibility or physical-hardware certification. Host evidence for
the native runtime and init consists of ELF/ABI inspection and supporting
loader/lifecycle contracts; execution evidence comes from actual CPL3 in QEMU.

| Subsystem | Implemented | Host Tested | QEMU Validated | Stable |
| --- | --- | --- | --- | --- |
| PMM | Yes | Yes | Yes | Yes, scoped |
| VMM | Yes | Yes | Yes | Yes, scoped |
| Heap | Yes | Yes | Yes | Yes, scoped |
| Threads | Yes | Yes | Yes | Yes, scoped |
| Scheduler | Yes | Yes | Yes | Yes, scoped |
| Processes | Yes | Yes | Yes | Yes, scoped |
| Ring 3 | Yes | Yes | Yes | Yes, scoped |
| Syscalls | Yes | Yes | Yes | Yes, scoped |
| User Isolation | Yes | Yes | Yes | Yes, scoped |
| VFS | Yes | Yes | Yes | Yes, scoped |
| initramfs | Yes | Yes | Yes | Yes, scoped |
| ELF Loader | Yes | Yes | Yes | Yes, scoped |
| Userspace Runtime | Yes | ELF/ABI + loader contracts | Yes, native ELF | Yes, scoped |
| init | Yes, PID 1 | ELF/ABI + lifecycle contracts | Yes | Yes, scoped |
| Userspace Shell | No | No | No | No |
| PCI | Yes | Yes | Yes | Yes, scoped |
| Block Layer | Yes | Yes | Yes | Yes, scoped |
| AHCI | Yes | Yes | Yes | Yes, scoped |
| FAT32 | Yes | Yes | Yes | Yes, scoped |
| NIC | Yes | Yes | Yes | Yes, scoped |
| Ethernet | Yes | Yes | Yes | Yes, scoped |
| ARP | Yes | Yes | Yes | Yes, scoped |
| IPv4 | Yes | Yes | Yes | Yes, scoped |
| ICMP | Yes | Yes | Yes | Yes, scoped |
| UDP | Yes | Yes | Yes | Yes, scoped |
| DHCP | Yes | Yes | Yes | Yes, scoped |
| DNS | Yes, bounded A/CNAME | Yes | Yes, local UDP fixture | Yes, scoped |
| TCP | No | No | No | No |
| HTTP | No | No | No | No |

## Final validation metrics

| Phase | Assertions | Failures | Headless VMs |
| --- | ---: | ---: | ---: |
| Final clean host suite | 36,527 | 0 | — |
| Final kernel ELF/ABI | 1,874 | 0 | — |
| Seven native ELF inspections | 113 | 0 | — |
| Full candidate matrix | 13,551 | 0 | 40 |
| Final version confirmation | 587 | 0 | 3 |
| **Passing gate total** | **52,652** | **0** | **43** |

Every gate VM was reaped. Two preliminary attempts remain separate: one
link-negotiation failure (197 assertions, one failed assertion) and its
successful 266-assertion recovery run. Thus there were **45 actual v0.8 VM
attempts**, 43 of them passing gate VMs. No earlier failure was relabeled PASS.

The full candidate matrix used version 0.7.0 with the completed networking
implementation. Only after acceptance did local commits promote it to 0.8.0.
The final stamp was built cleanly and passed network plus storage, alternate
subnet/MAC without NX, and kernel readonly protection. The full RAM matrix was
not repeated after stamping. Kernel text/data/requests/BSS, all seven complete
user ELFs and initramfs match; exactly one kernel rodata version byte changed.

The candidate includes network and storage at 64/256/512 MiB, NX-off refusal
for user processes, alternate DHCP subnet/MAC, absent NIC/disk, six corrupt
disk images, real native file reads, process isolation and fatal probes.
The 40-job plan/results and 3-job final plan/results identify every VM.

Supplemental ASan+UBSan assertions—packets 3,830, network transactions 1,102
and eight actual E1000 driver-model scenarios 1,548—sum to **6,480**, all
passing. They are excluded from the 52,652 total. Host fuzz-like packet tests
use deterministic seed 0x41535452. Assertions include repeated observations;
these counts are not unique tests, coverage percentages or proofs of absence
of defects.

## Recorded stress

| Subsystem | Runs | Recorded work |
| --- | ---: | --- |
| Heap | 8 | 65,536 operations; seed 0x41535452; growth and accounting |
| Scheduler | 7 | 21,735 completed operations; 539 worker threads reaped |
| Embedded user probes | 9 | 360 processes created/reaped; 117 contained faults; 2,951,100 syscalls |
| VFS / ELF | 9 | 315 processes created/reaped with exact cleanup checks |
| Storage | 14 | 126 fresh imports: 112 checked plus 14 warmups |
| Network | 21 | 336 rounds; 1,008 successful ping/DNS/generic UDP transactions |
| **Named stress runs** | **68** | PMM/VMM selfchecks also run within the matrix |

Startup demonstrations and explicit exec checks are additional and excluded
from these lifecycle totals. Scheduler operations include regressions invoked
inside other suites, as recorded in serial logs. Network transactions allocate
no memory; repeated snapshots match PMM/heap/VMM state. All seven disposable
disk bases retained their hashes.

## Preserved milestone history

| Milestone | Last known good | Passing assertions in that milestone's record |
| --- | --- | ---: |
| v0.3.0 | 2fb66be | 23,679 |
| v0.4.0 | 7e07719 | 31,681 |
| v0.5.0 | 4f303c8 | 40,620 |
| v0.6.0 | c5b2a91 | 39,528 |
| v0.7.0 | 0cfca41 | 44,543 |
| v0.8.0 | 81a01ce | 52,652 |

These are separate image-specific records, not additive validation of one
binary. Earlier GREEN implementations, commits, checkpoints and frozen
artifacts were preserved; later matrices exercise regressions on new kernels.

## Final kernel audit

Two independent read-only reviews complemented the implementation review.
The kernel review traced boot ordering, allocator and page-table ownership,
interrupt frames, TSS/RSP0, context switches, process construction/reaping,
user copies, ELF loading, VFS lifetime and AHCI/FAT32. The network review
traced descriptor/DMA publication, quarantine, frame bounds, checksums,
transaction matching, DHCP configuration, DNS compression and harness cleanup.

| Area | Reviewed invariant and evidence |
| --- | --- |
| PMM | Only eligible usable frames are allocated; bitmap/storage reservations, contiguous bounds and checked frees are covered by host tests and kernel stress |
| VMM / heap | Mappings remain in owned windows; failed growth rolls back leaves/frames; kernel empty tables and committed heap growth are retained explicitly |
| CPU / scheduler | Protected 64 KiB stacks, frame alignment/selectors/flags, TSS privilege stack, CR3 switches, queues, sleep/wakeup and deferred reaping have host and real CPL3 evidence |
| Userspace | Private zeroed pages/tables, W^X, complete copy preflight, syscall bounds and contained hostile faults; inactive roots are reclaimed without freeing shared kernel mappings |
| ELF / VFS | Strict static ELF64 segment bounds and rollback; immutable mounted nodes outlive descriptors; READ commits offsets only after a successful checked copy |
| Storage | Real readonly bounded DMA, widened sector arithmetic, descriptor completion checks and permanent quarantine ownership; FAT publication follows bounded complete validation |
| Network | Owned ring buffers, validated lengths/checksums, rejected fragmentation/options, bounded ARP/UDP/DHCP waits and DNS pointer/alias traversal |
| General | Warning-as-error builds, ELF/ABI inspection, malformed-input tests, sanitizer models and documentation consistency review; no placeholder reports were accepted as validation |

The network review corrected acceptance of unusable DHCP gateway/local DNS/
server endpoints. QEMU exposed asynchronous link negotiation after reconnection;
DHCP now waits for link readiness with a deadline and iteration bound.
Host negative tests and real link recovery verify these corrections.

Compiler stack-usage records show the largest individual network frame is
8,896 bytes in DNS decoding. This is a compiler measurement of that function,
not a runtime high-water mark. Bounded nested network call paths were reviewed
against the existing 64 KiB thread stack. There are no network allocations,
background workers or packet parsing inside hardware ISRs.

Historical subsystem guides were reconciled with implemented behavior.
The older v0.2 material now states its version scope and links to current
process, VFS, storage and networking contracts. No working subsystem was
replaced for stylistic reasons.

## Known limits and manual acceptance

- BSP only; PIC/PIT, general registers and a native INT128 ABI. No SMP, POSIX,
  TLS or floating-point/vector context switching. User processes require NX.
- Kernel HHDM aliases retain inherited permissions; primary section protection
  does not establish global kernel W^X. Heap commitment and empty kernel tables
  stay reserved; private process pages/tables are reclaimed.
- Processes and completion history are bounded; SPAWN keeps IF disabled during
  construction. Immutable nodes simplify descriptor ownership. There is no
  fork, dynamic linker, input syscall or userspace shell.
- AHCI supports one 512-byte-sector ATA disk with polling LBA48 reads. No writes,
  partitions, hotplug, NCQ or controller recovery. FAT32 is a bounded immutable
  ASCII 8.3 snapshot; malformed/unsupported media is rejected.
- E1000 DMA remains reserved after publication. Ambiguous hardware failure
  quarantines pages. There is no IOMMU or physical-device recovery guarantee.
- Networking is owned by the bootstrap/shell thread and polls during network
  operations. There is no continuously running network service, userspace
  socket API, IPv4 fragmentation/options, automatic DHCP renewal, DNS cache,
  EDNS, DNSSEC, TCP or HTTP. DNS supports A/CNAME within one bounded reply.
- QEMU DNS validation uses a loopback-only fixture reached through the learned
  gateway. The DHCP-provided DNS address is checked, but external recursive
  resolvers and internet access are not gate dependencies.
- UEFI, physical keyboard/NIC/SATA devices, visual framebuffer acceptance,
  sustained packet flooding and non-QEMU network interoperability remain
  manual or future validation. Fatal NMI/double-fault/machine-check delivery
  has not been deliberately exercised.

No GUI work, publishing, main merge, release or tag mutation belongs to this
campaign. TCP/HTTP and a userspace shell are optional and remain absent.

## Artifacts and reproduction

| Artifact | Value |
| --- | --- |
| Final ELF SHA-256 | `cdf220446fe7fb0ad6e69922942d565508341bec7670ee6731c37fc00ed64267` |
| Final ISO SHA-256 | `fbbaa1cc479bf1a48b3ca09682e9962bd574b4db86e88506bbf8442abde725d1` |
| Initramfs SHA-256 | `3eb6bb58373e4e6ed559537b39fe423046c8d233ab0323ccc1f14a3a37b2eda4` |
| Candidate evidence | `validation-artifacts/astra-v08-candidate-20260914T144940Z/` |
| Final evidence | `validation-artifacts/astra-v08-final-20260914T150758Z/` |
| Canonical validation manifest | [validation-astra-v0.8.json](validation-astra-v0.8.json) |
| Networking audit | [audit-astra-v0.8.md](audit-astra-v0.8.md) |
| Git/file inventory | [astra-campaign-changes.json](astra-campaign-changes.json) |

Run from the existing Linux/WSL project, using the existing tools:

```sh
make test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel inspect iso
python3 scripts/test-network-qemu.py --suite --network --name network-review
```

Use fresh VM names. The frozen matrix plans contain complete RAM, NX,
storage and fault arguments. Archive build/validation before make clean.
The shared harness enforces headless display, bounded runtime, one VM at a
time and explicit reaping. No installation or system configuration change
was needed. Manual acceptance remains listed above.
