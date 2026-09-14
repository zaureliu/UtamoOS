# Astra v0.7 storage audit

Status: implementation review and clean candidate gate passed (34 headless VMs,
12,202 QEMU assertions). Final version confirmation follows. This is
a local single-BSP/Q35 acceptance scope, not a physical hardware certification.

## Reviewed ownership and boundaries

- PCI configuration transactions preserve IF, isolate 16-bit command writes
  from status RW1C bits, restore sized BARs and skip 64-bit upper halves.
  Enumeration is generic across bus/slot/function; the AHCI match uses class,
  subclass and programming interface.
- MMIO policy rejects overlap with RAM and all mapped boot/kernel/framebuffer
  types. Its dedicated virtual window cannot be changed through RAM APIs.
  PAT UC is checked before use, and page-table insertion failure rolls back
  leaves while retaining the documented pinned empty kernel tables.
- AHCI port engines stop before DMA rebasing. Command headers, tables and
  buffers use separate aligned physical pages. Commands permit no writes,
  each PRDT covers at most the owned 4 KiB transfer buffer, and completion
  requires the expected byte count. Review broadened the rejected status
  bits to include host-bus/interface/overflow errors in addition to TFES.
- The real AHCI C driver is exercised with a deterministic MMIO/DMA model.
  Allocation/translation failures before publication free only unexposed
  memory; enabling/IDENTIFY failures and ambiguous completion retain the DMA
  allocation. Timeout, task error, bus error and short transfer disable future
  reads and preserve the caller buffer for a failed first batch. A multi-batch
  read may have copied earlier successful batches before a later failure;
  the filesystem importer treats the entire operation as failed.
- FAT32 arithmetic uses widened products before narrowing. Geometry bounds
  precede allocation and reads. A visited bitmap covers all reached directory
  and file chains, rejecting cross-links as well as cycles. Parent paths live
  in the stable node array during bounded recursion. File data is owned by its
  node before later operations can fail, allowing one cleanup path.
- FAT import has node/depth/cluster/read/payload limits. Unpublished failure
  leaves no namespace, payload or temporary bitmap behind. The mounted
  immutable snapshot remains alive for every descriptor. No unload API exists.
- Root initramfs and additional top-level VFS mounts cannot shadow existing
  nodes. FAT mounts happen before PID 1. Existing descriptor copying, EOF,
  seek and user-copy-before-offset-commit rules apply unchanged to cached FAT
  files; the native diskread executable exercises these paths from CPL3.
- Timer/keyboard ISRs perform no storage work. Runtime disk commands suppress
  preemption, retain timer IRQs, and have two independent polling bounds.
  DMA frames and MMIO mappings are excluded from temporary stress accounting.
  Reimport stress verifies no unexplained PMM or live-heap growth.

## Evidence and corrections

The clean gate candidate is
`validation-artifacts/astra-v07-candidate-20260914T134705Z/`.
Host assertions: 30,022 with zero failures. ELF inspection: 1,803 kernel plus
113 userspace assertions. The seven native ELFs include diskread.
The full QEMU matrix is recorded independently in matrix-plan/results.

The earlier candidate at 20260914T134515Z contains one failed diskread
assertion: it expected SEEK to return zero at offset 2040. The established
ABI returns the resulting offset. The assertion was corrected and source-line
diagnostics were added; neither the kernel ABI nor disk data was altered.
The failed report and serial log remain in that candidate's validation folder.

Initial FAT host fixture expectations were also corrected: some mutations
replaced bytes with their existing value; a larger file still fitting one
allocated cluster was valid, not corrupt. These cases now use actual invalid
geometry/chain lengths. Failure cleanup in the test itself prevents one
unexpected acceptance from contaminating subsequent scenarios. No parser
acceptance was weakened to obtain passing results.

QEMU initially refused an ide-hd attached to a readonly block node before boot.
The harness now uses snapshot=on, with the temporary overlay restricted to
build/tests. Source images remain disposable project fixtures. The launch
failure and the successful initial storage run are preserved under
`validation-artifacts/astra-before-v07-clean/`.
Supplemental ASan+UBSan parser, core and driver-model logs are in
`validation-artifacts/astra-v07-incremental/` and excluded from gate totals.

## Deliberate limits

Only one ATA disk, 512-byte logical sectors, polling and LBA48 reads are
implemented. There is no controller recovery, hotplug, NCQ, storage write API,
partition discovery or full FAT compatibility. FAT32 is a bounded immutable
cache populated through real block reads; it does not observe external writes.
Long names, non-ASCII short names, dirty volumes and files with surplus
allocated chain tails are outside the admitted format.

Permanent empty kernel page tables, inherited HHDM permission debt, lack of
IOMMU/SMP and the earlier native userspace ABI limits remain documented.
Quarantined DMA is an explicit reservation after hardware failure, not a
successful cleanup claim. UEFI, physical SATA devices and visual acceptance
remain separate manual work.
