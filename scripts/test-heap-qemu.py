#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded headless heap gate using the existing QEMU and memory harnesses.

Examples (run sequentially after building the kernel and ISO):
  python3 scripts/test-heap-qemu.py --suite --name heap-256m
  python3 scripts/test-heap-qemu.py --suite --name heap-64m --ram 64M
  python3 scripts/test-heap-qemu.py --suite --name heap-no-nx --ram 64M \
      --cpu qemu64,-nx --expect-nx off

Every input byte travels through QMP and the emulated PS/2 keyboard.
No window, screenshot, automatic build or parallel VM is provided.
Allocator failure injection and invalid ownership paths belong to host tests;
this gate observes the real bounded kernel self-test and hardware accounting.
"""
import argparse
import importlib.util
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "utamo_memory_qemu", ROOT / "scripts/test-memory-qemu.py")
MEMORY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MEMORY)
HARNESS = MEMORY.HARNESS

PAGE_SIZE = 4096
HEAP_BASE = 0xffffc00001000000
HEAP_INITIAL_BYTES = 65536
HEAP_MAX_BYTES = 67108864
HEAP_STRESS_SEED = 0x41535452
HEAP_STRESS_OPERATIONS = 8192
UINT64_MAX = (1 << 64) - 1
STATE_FIELDS = ("base", "mapped", "used", "free", "overhead", "live", "largest")
COUNTER_FIELDS = ("allocations", "frees", "peak", "failed", "invalid")
VMM_IDENTITY_FIELDS = ("kernel", "root", "hhdm", "bits", "nx")


def heap_snapshot(vm, description):
    text = vm.command("heap", ("Kernel Heap",))
    labels = {
        "mapped": "Mapped bytes:", "used": "Used bytes:",
        "free": "Free bytes:", "overhead": "Overhead bytes:",
        "live": "Live allocations:", "allocations": "Allocations:",
        "frees": "Frees:", "peak": "Peak usage:",
        "failed": "Failed allocations:", "invalid": "Invalid frees:",
        "largest": "Largest free block:",
    }
    values = {"base": MEMORY.number(vm, text, "Heap base:", description, True)}
    for field, label in labels.items():
        values[field] = MEMORY.number(vm, text, label, description)
    vm.check(bool(re.search(r"^Heap integrity: OK$", text, re.M)),
             description + " reports valid heap metadata")
    vm.check(all(0 <= value <= UINT64_MAX for value in values.values()),
             description + " contains unsigned 64-bit statistics")
    vm.check(values["base"] == HEAP_BASE,
             description + " uses the dedicated heap base")
    vm.check(HEAP_INITIAL_BYTES <= values["mapped"] <= HEAP_MAX_BYTES
             and values["mapped"] % PAGE_SIZE == 0,
             description + " has a bounded page-aligned committed extent")
    vm.check(values["used"] + values["free"] + values["overhead"] == values["mapped"],
             description + " accounts for payload, free space and overhead", values)
    vm.check(values["allocations"] >= values["frees"]
             and values["allocations"] - values["frees"] == values["live"],
             description + " reconciles successful allocations, frees and live blocks")
    vm.check(values["peak"] >= values["used"]
             and values["peak"] <= HEAP_MAX_BYTES,
             description + " has a valid requested-byte high-water mark")
    vm.check(values["largest"] <= values["free"],
             description + " largest free block fits within all free payload")
    vm.report.setdefault("heap_snapshots", []).append({"label": description, **values})
    return values


def check_memory_identity(vm, before_pmm, after_pmm, before_vmm, after_vmm,
                          description):
    fields = ("total", "bitmap", "bitmap_bytes", "storage")
    vm.check(all(before_pmm[field] == after_pmm[field] for field in fields),
             description + " preserves managed memory and bitmap identity")
    vm.check(all(before_vmm[field] == after_vmm[field]
                 for field in VMM_IDENTITY_FIELDS),
             description + " preserves CR3, HHDM, kernel base and CPU capabilities")


def check_growth_accounting(vm, before_heap, after_heap, before_pmm, after_pmm,
                            before_vmm, after_vmm, description):
    check_memory_identity(vm, before_pmm, after_pmm, before_vmm, after_vmm,
                          description)
    mapped_delta = after_heap["mapped"] - before_heap["mapped"]
    table_delta = after_vmm["tables"] - before_vmm["tables"]
    vm.check(mapped_delta >= 0 and mapped_delta % PAGE_SIZE == 0
             and table_delta >= 0,
             description + " retains committed heap pages and intermediate tables")
    expected_frames = mapped_delta // PAGE_SIZE + table_delta
    vm.check(after_pmm["used"] - before_pmm["used"] == expected_frames
             and before_pmm["free"] - after_pmm["free"] == expected_frames,
             description + " charges PMM exactly for heap growth and new tables",
             {"heap_page_delta": mapped_delta // PAGE_SIZE,
              "table_page_delta": table_delta, "expected_frames": expected_frames,
              "used_frame_delta": after_pmm["used"] - before_pmm["used"],
              "free_frame_delta": before_pmm["free"] - after_pmm["free"]})


def heap_mapping_checks(vm, heap, description):
    vm.check(HEAP_BASE >= MEMORY.TEST_BASE + 4 * 1024 * 1024,
             description + " leaves the VMM self-test arena separate")
    for label, address in (("first byte", heap["base"]),
                           ("last committed byte",
                            heap["base"] + heap["mapped"] - 1)):
        result = MEMORY.mapping(vm, address, description + " " + label,
                                writable=True, nx=vm.args.expect_nx == "on")
        vm.check(result["page_size"] == PAGE_SIZE,
                 description + " " + label + " uses a 4 KiB leaf")
    # Growth is demand-driven; retained pages must not imply mapping the reserve.
    if heap["mapped"] < HEAP_MAX_BYTES:
        MEMORY.mapping(vm, heap["base"] + heap["mapped"],
                       description + " first uncommitted page", expected_mapped=False)
        MEMORY.mapping(vm, heap["base"] + HEAP_MAX_BYTES - 1,
                       description + " end of reserved heap range", expected_mapped=False)


def kernel_mapping_checks(vm, symbols, description):
    for symbol, writable, nx in (
            ("__text_start", False, False), ("__rodata_start", False, True),
            ("__data_start", True, True), ("__bss_start", True, True)):
        vm.check(symbol in symbols, description + " finds ELF symbol " + symbol)
        MEMORY.mapping(vm, symbols[symbol], description + " " + symbol,
                       writable=writable, nx=nx and vm.args.expect_nx == "on")


def check_running_if(vm):
    samples = []
    enabled = False
    # A single snapshot may interrupt the guest during a short IF=0 transaction.
    for attempt in range(10):
        vm.remaining()
        registers = vm.registers("heap-running-" + str(attempt))
        match = re.search(r"\bRFL=([0-9a-fA-F]+)", registers)
        samples.append(match.group(1) if match else None)
        if match and int(match.group(1), 16) & 0x200:
            enabled = True
            break
        time.sleep(0.03)
    vm.check(enabled, "Guest resumes execution with IF enabled after heap stress",
             {"rflags_samples": samples})


def suite(vm, symbols):
    boot = vm.wait_for(HARNESS.PROMPT)
    for marker in ("Version: " + vm.args.version, "PMM initialized",
                   "VMM initialized", "Kernel heap initialized", "GDT initialized",
                   "IDT initialized", "PIT timer initialized",
                   "PS/2 keyboard initialized", "Interrupts enabled",
                   "UTAMO OS ready."):
        vm.check(marker in boot, "Boot marker: " + marker)
    vm.command("help", ("heap", "heaptest", "pmm", "vmm", "pmmtest", "vmmtest"))
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    initial_system = vm.command("sysinfo", ("Ticks:", "x86_64", "Limine"))
    initial_heap = heap_snapshot(vm, "Initial heap")
    initial_pmm = MEMORY.pmm_snapshot(vm, "Initial PMM after heap initialization")
    initial_vmm = MEMORY.vmm_snapshot(vm, "Initial VMM after heap initialization")
    vm.check(initial_heap["mapped"] == HEAP_INITIAL_BYTES,
             "Normal boot commits only the initial 64 KiB heap")
    MEMORY.hardware_memory_checks(vm, initial_vmm)
    heap_mapping_checks(vm, initial_heap, "Initial heap mapping")
    kernel_mapping_checks(vm, symbols, "Kernel before heap stress")

    for command in ("heap extra", "heaptest extra"):
        vm.command(command, ("Unexpected arguments.",))
    rejected_heap = heap_snapshot(vm, "Heap after rejected command arguments")
    rejected_pmm = MEMORY.pmm_snapshot(vm, "PMM after rejected command arguments")
    rejected_vmm = MEMORY.vmm_snapshot(vm, "VMM after rejected command arguments")
    vm.check(rejected_heap == initial_heap and rejected_pmm == initial_pmm
             and rejected_vmm == initial_vmm,
             "Rejected shell arguments cannot allocate or mutate allocator state")

    previous_heap, previous_pmm, previous_vmm = initial_heap, initial_pmm, initial_vmm
    stable_heap = stable_pmm = stable_vmm = None
    for repetition in range(3):
        label = "Heap stress " + str(repetition + 1)
        response = vm.command("heaptest", ("Heap self-test: PASS",))
        seed = MEMORY.number(vm, response, "Heap stress seed:", label, True)
        operations = MEMORY.number(vm, response, "Heap stress operations:", label)
        vm.check(seed == HEAP_STRESS_SEED, label + " uses the reproducible seed")
        vm.check(operations >= HEAP_STRESS_OPERATIONS,
                 label + " executes at least 8192 randomized operations",
                 {"operations": operations})
        current_heap = heap_snapshot(vm, label + " heap")
        current_pmm = MEMORY.pmm_snapshot(vm, label + " PMM")
        current_vmm = MEMORY.vmm_snapshot(vm, label + " VMM")
        vm.check(current_heap["live"] == initial_heap["live"]
                 and current_heap["used"] == initial_heap["used"],
                 label + " restores live allocation count and requested bytes")
        vm.check(current_heap["allocations"] > previous_heap["allocations"]
                 and current_heap["frees"] > previous_heap["frees"],
                 label + " performs successful allocation and release work")
        vm.check(all(current_heap[field] >= previous_heap[field]
                     for field in COUNTER_FIELDS),
                 label + " preserves monotonic lifetime counters")
        check_growth_accounting(vm, previous_heap, current_heap,
                                previous_pmm, current_pmm,
                                previous_vmm, current_vmm, label)
        if repetition == 0:
            vm.check(current_heap["mapped"] > initial_heap["mapped"]
                     and current_heap["mapped"] > 512 * 1024,
                     "First heap self-test forces growth beyond 512 KiB")
            stable_heap, stable_pmm, stable_vmm = current_heap, current_pmm, current_vmm
            heap_mapping_checks(vm, current_heap, "Expanded heap mapping")
        else:
            vm.check(all(current_heap[field] == stable_heap[field]
                         for field in STATE_FIELDS),
                     label + " stabilizes committed space, free blocks and overhead")
            vm.check(current_pmm == stable_pmm and current_vmm == stable_vmm,
                     label + " reuses committed data frames and page tables")
        previous_heap, previous_pmm, previous_vmm = current_heap, current_pmm, current_vmm

    # Memory self-tests use their existing separate range. Do not run the whole
    # imported memory suite here: it owns its own halt, and would end this gate.
    vm.command("pmmtest", ("PMM self-test: PASS",))
    after_pmm_test = MEMORY.pmm_snapshot(vm, "PMM after post-heap PMM self-test")
    vm.check(after_pmm_test == previous_pmm,
             "Physical allocator self-test still restores exact PMM accounting")
    for repetition in range(2):
        label = "Post-heap VMM self-test " + str(repetition + 1)
        vm.command("vmmtest", ("VMM self-test: PASS",))
        after_heap = heap_snapshot(vm, label + " heap")
        after_pmm = MEMORY.pmm_snapshot(vm, label + " PMM")
        after_vmm = MEMORY.vmm_snapshot(vm, label + " VMM")
        vm.check(after_heap == previous_heap,
                 label + " leaves every heap statistic unchanged")
        check_growth_accounting(vm, previous_heap, after_heap,
                                previous_pmm, after_pmm, previous_vmm, after_vmm,
                                label)
        if repetition != 0:
            vm.check(after_pmm == previous_pmm and after_vmm == previous_vmm,
                     label + " reuses the existing memory-test tables")
        previous_heap, previous_pmm, previous_vmm = after_heap, after_pmm, after_vmm
    MEMORY.mapping(vm, MEMORY.TEST_BASE, "VMM self-test mapping cleaned up",
                   expected_mapped=False)
    vm.command("pmmtest", ("PMM self-test: PASS",))
    vm.check(MEMORY.pmm_snapshot(vm, "Final PMM") == previous_pmm,
             "PMM remains stable after heap and VMM stress")
    vm.check(heap_snapshot(vm, "Final heap") == previous_heap,
             "Post-heap memory regressions leave no heap side effects")
    kernel_mapping_checks(vm, symbols, "Kernel after heap and memory stress")
    heap_mapping_checks(vm, previous_heap, "Final heap mapping")
    check_running_if(vm)

    vm.command("echo heap paths remain alive", ("heap paths remain alive",))
    vm.command("echo AbC 123 !?", ("AbC 123 !?",))
    vm.command("versioxx\b\bn", ("UTAMO OS " + vm.args.version,))
    vm.command("uptime", ("Uptime:", "ticks"))
    final_system = vm.command("sysinfo", ("Ticks:",))
    initial_ticks = MEMORY.number(vm, initial_system, "Ticks:", "Initial sysinfo")
    final_ticks = MEMORY.number(vm, final_system, "Ticks:", "Final sysinfo")
    vm.check(final_ticks > initial_ticks, "PIT advances across heap and memory stress",
             {"before": initial_ticks, "after": final_ticks})
    (vm.directory / "pic.txt").write_text(vm.hmp("info pic"))
    (vm.directory / "irq.txt").write_text(vm.hmp("info irq"))
    cleared = vm.command("clear")
    vm.check("\x1b[2J\x1b[H" in cleared,
             "Clear emits serial erase/home and returns the prompt")
    vm.command("version", ("UTAMO OS " + vm.args.version,))

    start = len(vm.serial())
    vm.type_text("halt\n")
    vm.wait_for("System halted.", start)
    vm.check(HARNESS.PROMPT not in vm.serial()[start:], "Halt returns no prompt")
    vm.halt_checks()
    vm.check("UTAMO OS KERNEL EXCEPTION" not in vm.serial()
             and "UTAMO KERNEL PANIC" not in vm.serial(),
             "Heap gate completes without an unexpected CPU exception or panic")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", action="store_true", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--iso", type=HARNESS.project_path)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ram", default="256M")
    parser.add_argument("--cpu", default="qemu64")
    parser.add_argument("--expect-nx", choices=("on", "off"), default="on")
    parser.add_argument("--version", help="Expected semver; defaults to version.h")
    parser.add_argument("--timeout", type=float, default=240.0)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_-]{1,40}", args.name):
        parser.error("--name must contain 1-40 letters, digits, underscores or hyphens")
    if not re.fullmatch(r"[1-9][0-9]{0,4}[MG]", args.ram):
        parser.error("--ram must be an integer followed by M or G")
    if not re.fullmatch(r"[A-Za-z0-9_.,+=-]{1,120}", args.cpu):
        parser.error("--cpu must be a QEMU model/features string without whitespace")
    if not 0 < args.timeout <= 600:
        parser.error("--timeout must be in (0, 600]")
    if args.version is None:
        match = re.search(r'^#define UTAMO_VERSION "([^"]+)"$',
                          (ROOT / "kernel/include/utamo/version.h").read_text(), re.M)
        if not match:
            parser.error("Cannot read current UTAMO_VERSION")
        args.version = match.group(1)
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("--version must be a semantic version")
    if args.iso is None:
        candidates = sorted((ROOT / "build").glob("utamo-os-*.iso"))
        if len(candidates) != 1:
            parser.error("Specify --iso when build does not contain exactly one UTAMO ISO")
        args.iso = HARNESS.project_path(candidates[0])
    if not args.iso.is_file():
        parser.error("ISO does not exist")

    # VM is the existing shared implementation, with no graphics/probe path.
    args.probe = None
    args.probe_at = "cpu_wait_interrupt"
    args.check_timer = False
    args.gdb_port = None
    args.capture_framebuffer = False
    report = {
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "mode": "heap suite", "checks": [],
        "git_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "git_status": subprocess.check_output(
            ["git", "status", "--short"], cwd=ROOT, text=True),
        "iso": str(args.iso.relative_to(ROOT)), "iso_sha256": HARNESS.digest(args.iso),
        "elf_sha256": HARNESS.digest(ROOT / "build/utamo-kernel.elf"),
        "expected_version": args.version, "expected_nx": args.expect_nx,
        "stress_seed": HEAP_STRESS_SEED,
        "minimum_stress_operations": HEAP_STRESS_OPERATIONS,
        "pending_manual": ["Physical keyboard and visual framebuffer review",
                           "UEFI and physical hardware"],
        "separate_host_coverage": [
            "Allocation/mapping failure injection and transactional rollback",
            "Invalid frees, double frees, zero-size and overflow contracts"],
    }
    vm = None
    try:
        symbols = MEMORY.elf_symbols(args)
        vm = HARNESS.VM(args, report)
        vm.start()
        suite(vm, symbols)
        report["passed"] = True
    except (HARNESS.CheckFailed, OSError, ValueError, subprocess.SubprocessError,
            KeyboardInterrupt) as error:
        report["passed"] = False
        report["error"] = str(error)
        print("FAIL " + str(error), file=sys.stderr, flush=True)
    finally:
        if vm is not None:
            try:
                vm.close()
                vm.check(report.get("qemu_process_reaped") is True,
                         "QEMU subprocess was reaped")
            except (HARNESS.CheckFailed, OSError, ValueError,
                    subprocess.SubprocessError) as error:
                report["passed"] = False
                report["cleanup_error"] = str(error)
                print("FAIL cleanup: " + str(error), file=sys.stderr, flush=True)
            report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            report["check_count"] = len(report["checks"])
            report["failure_count"] = sum(not check["passed"] for check in report["checks"])
            report["run_failure"] = not report.get("passed", False)
            (vm.directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            print("Evidence: " + str(vm.directory.relative_to(ROOT)), flush=True)
    return 0 if report.get("passed", False) else 1


if __name__ == "__main__":
    sys.exit(main())
