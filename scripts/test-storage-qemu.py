#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real PCI/AHCI readonly media and FAT32, using the shared headless VM owner."""
import importlib.util
import re
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("utamo_storage_gate", ROOT/"scripts/test-process-qemu.py")
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)
H = P.HARNESS

def suite(vm, symbols):
    vm.report["mode"] = "PCI/AHCI/FAT32 suite"
    vm.report["separate_host_coverage"] = [
        "PCI multifunction enumeration, BAR width/size and command/status restoration",
        "AHCI read-only descriptors and IDENTIFY capacity contracts",
        "FAT32 corruption, failure-injected reads/allocations and unpublished rollback"]
    boot = vm.wait_for(H.PROMPT)
    vm.check("PCI enumeration:" in boot, "Boot enumerates PCI devices")
    vm.check("Version: "+vm.args.version in boot, "Version matches candidate")
    text = vm.command("lspci", ("PCI devices:", "class=0x1 subclass=0x6 interface=0x1", "BAR5 MMIO"))
    vm.check("vendor=0x8086" in text, "Q35 hardware identity comes from PCI configuration")
    if vm.args.disk:
        vm.report["disk"] = str(vm.args.disk.relative_to(ROOT))
        vm.report["disk_sha256_before"] = H.digest(vm.args.disk)
        vm.check("AHCI readonly disk:" in boot and "sectors=131072" in boot,
                 "AHCI IDENTIFY reports the real disposable SATA disk")
        vm.command("storage", ("AHCI ready=Yes readonly=Yes", "failures=0", "DMA_pages=4", "quarantine=No"))
        mapping = vm.command("mapinfo ffffc00080000000", ("Mapped: Yes", "Writable: Yes", "User: No"))
        flags = re.search(r"Effective flags: 0x([0-9a-f]+)", mapping)
        vm.check(bool(flags) and int(flags[1],16)&0x18 == 0x18, "AHCI MMIO uses verified UC PAT index")
        vm.check(("NX: Yes" if vm.args.expect_nx == "on" else "NX: No") in mapping,
                 "MMIO respects runtime NX capability")
        if vm.args.expect_disk == "mounted":
            vm.check("FAT32 mounted readonly at /disk: nodes=6" in boot, "Validated FAT32 tree publishes at /disk")
            vm.command("ls", ("/disk", "/bin", "/etc"))
            vm.command("ls /disk", ("/disk/FILE.TXT", "/disk/DOCS", "/disk/CHAIN.BIN", "/disk/EMPTY.TXT"))
            vm.command("ls /disk/DOCS", ("/disk/DOCS/README.TXT",))
            vm.command("cat /disk/FILE.TXT", ("UTAMO readonly AHCI and FAT32",))
            vm.command("cat /disk/DOCS/README.TXT", ("Nested FAT32 directory works",))
            vm.command("cat /disk/EMPTY.TXT")
            vm.command("cat /disk/missing", ("File unavailable.",))
            vm.command("ls /disk/../etc", ("Directory unavailable.",))
            steady = None
            for _ in range(3):
                vm.command("disktest", ("Storage self-test: PASS", "snapshot_equal=Yes accounting=PASS"))
                current = P.SCHED.memory_snapshots(vm, "Storage repeat")
                if steady is not None:
                    vm.check(all(current[0][key] == steady[0][key] for key in P.HEAP.STATE_FIELDS)
                             and current[1:] == steady[1:], "Disk reimports preserve exact PMM/heap/VMM accounting")
                steady = current
            vm.report["storage_stress"] = {"runs":3,"imports_per_run":9,"checked_rounds_per_run":8}
            if vm.args.expect_nx == "on":
                vm.command("exec /bin/filetest", ("filetest: PASS", "Exec: PASS"))
                vm.command("exec /bin/diskread", ("diskread: PASS", "Exec: PASS"))
        else:
            vm.check("FAT32 rejected; /disk not mounted" in boot, "Corrupt media is rejected before publication")
            vm.command("ls /disk", ("Directory unavailable.",))
            vm.command("cat /disk/FILE.TXT", ("File unavailable.",))
            vm.command("cat /etc/motd", ("UTAMO native VFS and initramfs",))
            vm.command("storage", ("FAT32 mounted=No nodes=0 cached_bytes=0",))
        vm.report["disk_sha256_after"] = H.digest(vm.args.disk)
        vm.check(vm.report["disk_sha256_before"] == vm.report["disk_sha256_after"],
                 "Readonly disk image is unchanged")
    else:
        vm.check("AHCI: no SATA ATA disk" in boot, "ATAPI boot media is not misidentified as an ATA disk")
        vm.command("storage", ("AHCI ready=No", "FAT32 mounted=No"))
    vm.command("heap", ("Heap integrity: OK",))
    vm.command("schedulerstats", ("Scheduler integrity: OK",))
    vm.command("echo storage kernel alive", ("storage kernel alive",))
    vm.check("UTAMO OS KERNEL EXCEPTION" not in vm.serial() and
             "UTAMO KERNEL PANIC" not in vm.serial(), "No kernel panic or exception")
    start = len(vm.serial())
    vm.type_text("halt\n")
    vm.wait_for("System halted.", start)
    vm.halt_checks()

if __name__ == "__main__":
    P.suite = suite
    raise SystemExit(P.main())
