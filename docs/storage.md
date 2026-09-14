# PCI, AHCI and readonly FAT32

The v0.7 candidate enumerates PCI configuration mechanism 1 on the BSP,
decodes I/O, 32-bit and 64-bit BARs, and attaches one AHCI ATA disk.
No storage write operation exists in the block or filesystem API.

PCI scans all 256 buses, 32 slots and advertised functions, retaining at most
128 devices. BAR sizing is restricted to bootstrap driver attachment: decode
and bus mastering are disabled during the probe, BAR values and the 16-bit
command register are restored and checked. The status register is not written
as a side effect. Shell `lspci` performs no destructive BAR sizing.

## MMIO and DMA

MMIO occupies a separate 4 MiB window at `0xffffc00080000000`, beyond the heap
and guarded thread stack arenas. Individual apertures are at most 1 MiB.
Only aligned validated PCI apertures outside RAM, boot/kernel/module memory,
ACPI, bad memory and framebuffer regions are admitted. Reserved ranges and
unreported PCI holes are allowed; duplicate physical apertures are refused.
PAT support and UC entry 3 are checked before mapping PCD|PWT supervisor RW,
with NX when supported. Normal RAM mapping/protection/unmapping APIs cannot
modify this window. Mappings and their empty table pages are permanent.

AHCI validates BAR5 and the implemented port register extent, performs BIOS
handoff when advertised, disables controller/port interrupts and stops the
command and FIS engines before replacing DMA addresses. ATAPI is skipped.
Four contiguous PMM pages own the command list, received FIS area, command
table and 4 KiB transfer buffer. The controller receives physical addresses;
the CPU accesses the same RAM through the checked HHDM. A controller without
64-bit DMA support is refused if this allocation exceeds 4 GiB.

Only IDENTIFY and READ DMA EXT can be constructed. LBA48 capacity and 512-byte
logical sectors are required. Reads split into at most eight sectors per
command. The transfer byte count, task status and completion are checked.
Polling has both a two-second PIT deadline and an iteration ceiling. Runtime
I/O suppresses preemption while allowing timer interrupts; IRQ handlers perform
no disk work. On ambiguous completion the port is stopped, the disk is disabled,
and all four DMA pages remain quarantined. They are never freed or reused while
the controller might own them. No hotplug, recovery reset, NCQ, writeback,
ATAPI data driver, multiple disks or IOMMU support is claimed.

## FAT32 snapshot

The parser consumes the generic bounds-checked block interface. It accepts a
FAT32 superfloppy volume at LBA 0, 512-byte sectors, power-of-two clusters up to
128 sectors, one or two FATs, and 65,525 through 1,048,576 data clusters.
MBR/GPT partition discovery is not implemented. FAT12/16/exFAT are refused.
Active-FAT selection is supported; mirrored FAT sectors actually visited must
agree. Dirty/error-marked volumes are refused by the reserved-entry policy.

Root and subdirectories use ASCII 8.3 names, normalized to uppercase. Long-name
entries are skipped; lookup uses the short name and is case-sensitive. Deleted
entries and volume labels are skipped. Dot entries are validated but not
published. Illegal names, duplicate paths, cross-linked clusters, cycles,
out-of-range/free/bad/reserved links and chains shorter or longer than file
size permits are rejected.

Import is deliberately bounded: 128 nodes, depth eight below the root,
1 MiB per file, 4 MiB cached payload, 16,384 visited clusters and 65,536 sector
reads. An allocation or read failure discards the entire unpublished tree.
A successful import frees its temporary visited bitmap and retains only file
payloads and a static node index. VFS publishes the complete immutable snapshot
at `/disk` before the first user process. Subsequent reads and seeks use this
cache; external media changes are not reflected, and no runtime unmount exists.
This is a readonly cached filesystem, not demand paging or a writable filesystem.

## Validation and operation

`make test-host` includes PCI/BAR/command restoration, block bounds, MMIO policy,
actual AHCI driver fault models, and the real FAT32 parser with every baseline
read/allocation failure injected. Deterministic mutations and corrupt fixtures
exercise transactional cleanup. Driver model scenarios cover allocation,
translation and enabling failures, stuck port, malformed IDENTIFY, timeout,
task error and short DMA. They do not replace hardware emulation evidence.

`scripts/make-test-disk.py` creates disposable 64 MiB sparse images only in
`build/tests`, with a nested directory, empty file and fragmented 2,049-byte
pattern file. `scripts/test-storage-qemu.py` uses the existing sequential,
bounded, headless QMP harness. QEMU's ATA model refuses a readonly block node,
so the base is opened through QEMU snapshot mode with an unlinked temporary
overlay in `build/tests`; the base hash is checked before and after. No physical
disk or external path is accepted.

`lspci`, `storage`, `ls /disk`, `cat /disk/FILE.TXT` and `disktest` expose
real device and filesystem state. Each `disktest` performs one warm import plus
eight checked imports, compares all paths/types/bytes, frees each temporary tree
and verifies exact PMM/live-heap accounting. `exec /bin/diskread` verifies the
fragmented pattern, EOF, seek and failed user-copy offset rollback from Ring 3.
A missing disk or rejected filesystem leaves initramfs and native init available.

Protocol references: [Intel AHCI 1.3.1](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1-3-1.pdf)
and the [Microsoft FAT specification, mirrored by FSU](https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf).
