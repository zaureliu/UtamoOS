#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded headless PMM/VMM validation using real emulated PS/2 input.

Examples (each invocation completes and reaps its one VM before the next):
  python3 scripts/test-memory-qemu.py --suite --name memory-256m
  python3 scripts/test-memory-qemu.py --suite --name memory-64m --ram 64M
  python3 scripts/test-memory-qemu.py --fault vmm --name fault-unmapped
  python3 scripts/test-memory-qemu.py --fault ro --name fault-readonly
  python3 scripts/test-memory-qemu.py --fault nx --name fault-execute

Serial output is the primary evidence. No GTK, SDL, display window or screenshot
is available in this tool. QMP send-key feeds the PS/2 controller; serial input
is never substituted for the keyboard. Page-fault probes are opt-in. RO/NX
probes use fixed ELF symbols at a pre-HLT breakpoint through the existing GDB.
Nothing here executes on an ordinary kernel boot.
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
SPEC = importlib.util.spec_from_file_location("utamo_qemu", ROOT / "scripts/test-qemu.py")
HARNESS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HARNESS)
TEST_BASE = 0xffffc00000000000
OLD_FAULT = 0x00007ffffffff000
STACK_GUARD = 0xffffc00040000000
PAGE_SIZE = 4096
VMM_NX = 1 << 63


def number(vm, text, label, description, hexadecimal=False):
    pattern = r"^" + re.escape(label) + (r"\s*0x([0-9a-fA-F]+)" if hexadecimal
                                        else r"\s*(\d+)")
    match = re.search(pattern, text, re.M)
    vm.check(bool(match), description + " includes " + label)
    return int(match.group(1), 16 if hexadecimal else 10)


def pmm_snapshot(vm, description):
    text = vm.command("pmm", ("Physical Memory",))
    values = {
        "total": number(vm, text, "Managed frames:", description),
        "used": number(vm, text, "Used frames:", description),
        "free": number(vm, text, "Free frames:", description),
        "bitmap": number(vm, text, "Bitmap physical:", description, True),
        "bitmap_bytes": number(vm, text, "Bitmap bytes:", description),
        "storage": number(vm, text, "Bitmap storage:", description),
    }
    vm.check(values["total"] > 0 and values["used"] + values["free"] == values["total"],
             description + " accounts for every managed frame", values)
    vm.check(values["bitmap"] % PAGE_SIZE == 0 and values["bitmap_bytes"] > 0
             and values["storage"] >= 2 * values["bitmap_bytes"]
             and values["storage"] % PAGE_SIZE == 0,
             description + " bitmap storage is page-aligned and sufficiently large")
    vm.check("Page size: 4096 bytes" in text, description + " uses 4 KiB frames")
    vm.report.setdefault("pmm_snapshots", []).append({"label": description, **values})
    return values


def vmm_snapshot(vm, description):
    text = vm.command("vmm", ("Virtual Memory",))
    values = {
        "kernel": number(vm, text, "Kernel base:", description, True),
        "root": number(vm, text, "CR3 root:", description, True),
        "hhdm": number(vm, text, "HHDM offset:", description, True),
        "tables": number(vm, text, "PMM-owned page tables:", description),
        "bits": number(vm, text, "Physical address bits:", description),
        "nx": "NX enabled: Yes" in text,
    }
    vm.check(values["kernel"] == 0xffffffff80000000,
             description + " preserves the existing higher-half kernel base")
    vm.check(values["root"] > 0 and values["root"] % PAGE_SIZE == 0,
             description + " reports an aligned physical CR3 root")
    vm.check(0xffff800000000000 <= values["hhdm"] < TEST_BASE
             and values["hhdm"] % PAGE_SIZE == 0,
             description + " reports a higher-half HHDM below the owned test slot")
    vm.check(32 <= values["bits"] <= 52,
             description + " reports a supported physical address width")
    expected_nx = vm.args.expect_nx == "on"
    nx_label = "Yes" if expected_nx else "No"
    vm.check(values["nx"] == expected_nx and "NX supported: " + nx_label in text
             and "NX enabled: " + nx_label in text,
             description + " reports expected NX capability/enabled state")
    vm.check("Section protections: Applied" in text,
             description + " reports applied kernel section protections")
    vm.report.setdefault("vmm_snapshots", []).append({"label": description, **values})
    return values


def mapping(vm, address, description, expected_mapped=True, writable=None,
            nx=None, physical=None):
    text = vm.command("mapinfo " + format(address, "x"))
    observed_address = number(vm, text, "Virtual:", description, True)
    vm.check(observed_address == address, description + " queries the requested address")
    vm.check(("Mapped: Yes" if expected_mapped else "Mapped: No") in text,
             description + " reports expected mapping presence")
    if not expected_mapped:
        vm.check("Physical:" not in text and "Effective flags:" not in text,
                 description + " does not invent an absent translation")
        return None
    flags = number(vm, text, "Effective flags:", description, True)
    page_size = number(vm, text, "Page size:", description)
    observed_physical = number(vm, text, "Physical:", description, True)
    vm.check(page_size in (PAGE_SIZE, 2 * 1024 * 1024, 1024 * 1024 * 1024),
             description + " identifies a supported leaf size")
    vm.check(flags & 1 != 0 and flags & 4 == 0,
             description + " is present and supervisor-only")
    if writable is not None:
        vm.check(bool(flags & 2) == writable,
                 description + " has expected effective write permission")
    if nx is not None:
        vm.check(bool(flags & VMM_NX) == nx,
                 description + " has expected effective execute permission")
    if physical is not None:
        vm.check(observed_physical == physical,
                 description + " translates to the expected physical byte")
    vm.report.setdefault("mappings", []).append({
        "label": description, "virtual": address, "physical": observed_physical,
        "flags": flags, "page_size": page_size,
    })
    return {"physical": observed_physical, "flags": flags, "page_size": page_size}


def elf_symbols(args):
    nm = ROOT / "toolchain/prefix/bin/x86_64-elf-nm"
    result = subprocess.run(
        [str(nm), "--defined-only", "--numeric-sort", str(ROOT / "build/utamo-kernel.elf")],
        cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=min(10.0, args.timeout), check=True)
    symbols = {}
    for line in result.stdout.splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]+)\s+\S\s+(\S+)", line)
        if match:
            symbols[match.group(2)] = int(match.group(1), 16)
    return symbols


def hardware_memory_checks(vm, info):
    registers = vm.registers("memory-boot")
    vm.descriptor_checks(registers)
    for label in ("CR0", "CR3", "CR4", "EFER"):
        match = re.search(r"\b" + label + r"=([0-9a-fA-F]+)", registers)
        vm.check(bool(match), "Hardware register snapshot includes " + label)
        value = int(match.group(1), 16)
        if label == "CR0":
            vm.check(value & (1 << 16) != 0, "CR0.WP enforces supervisor read-only pages")
        elif label == "CR3":
            vm.check(value & 0x000ffffffffff000 == info["root"],
                     "Shell CR3 root matches the hardware register")
        elif label == "CR4":
            vm.check(value & (1 << 12) == 0, "CR4.LA57 is clear: four-level paging")
        else:
            vm.check(bool(value & (1 << 11)) == (vm.args.expect_nx == "on"),
                     "EFER.NXE matches the expected hardware capability")


def suite(vm, symbols):
    boot = vm.wait_for(HARNESS.PROMPT)
    for marker in ("UTAMO OS", "Version: " + vm.args.version, "GDT initialized",
                   "IDT initialized", "PIC initialized", "PIT timer initialized",
                   "PS/2 keyboard initialized", "PMM initialized", "VMM initialized",
                   "Interrupts enabled", "UTAMO OS ready."):
        vm.check(marker in boot, "Boot marker: " + marker)
    vm.command("help", ("help", "clear", "version", "sysinfo", "mem", "pmm",
                        "vmm", "mapinfo", "pmmtest", "vmmtest", "uptime",
                        "echo", "halt", "fault"))
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    first_system = vm.command("sysinfo", ("x86_64", "Limine", "100 Hz", "Ticks:"))
    vm.command("mem", ("Usable memory:", "Physical Memory", "Virtual Memory",
                       "Managed frames:", "Free frames:", "HHDM offset:"))
    initial_pmm = pmm_snapshot(vm, "Initial PMM")
    initial_vmm = vmm_snapshot(vm, "Initial VMM")
    hardware_memory_checks(vm, initial_vmm)
    mapping(vm, initial_vmm["kernel"], "Kernel request segment")
    for symbol, writable, nx in (
            ("__text_start", False, False), ("__rodata_start", False, True),
            ("__data_start", True, True), ("__bss_start", True, True)):
        vm.check(symbol in symbols, "ELF exposes section symbol " + symbol)
        mapping(vm, symbols[symbol], "Section " + symbol, writable=writable,
                nx=nx and vm.args.expect_nx == "on")
    mapping(vm, initial_vmm["hhdm"] + initial_pmm["bitmap"],
            "Bitmap HHDM alias", writable=True, physical=initial_pmm["bitmap"])
    mapping(vm, TEST_BASE, "Initially unused test slot", expected_mapped=False)

    for command in ("mapinfo", "mapinfo 0x", "mapinfo -1",
                    "mapinfo 10000000000000000", "mapinfo 1234 extra"):
        vm.command(command, ("Usage: mapinfo",))
    vm.command("mapinfo 800000000000", ("VMM query unavailable or address invalid.",))
    vm.command("pmmtest extra", ("Unexpected arguments.",))
    vm.command("vmmtest extra", ("Unexpected arguments.",))

    for index in range(3):
        vm.command("pmmtest", ("PMM self-test: PASS",))
        snapshot = pmm_snapshot(vm, "After PMM test " + str(index + 1))
        vm.check(snapshot == initial_pmm,
                 "PMM self-test " + str(index + 1) + " restores exact accounting")
    vm.command("vmmtest", ("VMM self-test: PASS",))
    first_pmm = pmm_snapshot(vm, "After first VMM test")
    first_vmm = vmm_snapshot(vm, "After first VMM test")
    retained = first_vmm["tables"] - initial_vmm["tables"]
    vm.check(retained > 0, "First VMM self-test creates PMM-owned intermediate tables",
             {"before": initial_vmm["tables"], "after": first_vmm["tables"]})
    vm.check(first_pmm["total"] == initial_pmm["total"]
             and first_pmm["used"] - initial_pmm["used"] == retained
             and initial_pmm["free"] - first_pmm["free"] == retained,
             "First VMM self-test retains exactly its intermediate table frames",
             {"retained_tables": retained, "before": initial_pmm, "after": first_pmm})
    mapping(vm, TEST_BASE, "Test mapping removed after first self-test", expected_mapped=False)
    for index in range(2):
        vm.command("vmmtest", ("VMM self-test: PASS",))
        current_pmm = pmm_snapshot(vm, "After repeated VMM test " + str(index + 1))
        current_vmm = vmm_snapshot(vm, "After repeated VMM test " + str(index + 1))
        vm.check(current_pmm == first_pmm,
                 "Repeated VMM self-test " + str(index + 1) + " leaks no data frames")
        vm.check(current_vmm == first_vmm,
                 "Repeated VMM self-test " + str(index + 1) + " reuses existing tables")
    mapping(vm, TEST_BASE, "Test mapping absent after repeated tests", expected_mapped=False)
    vm.command("pmmtest", ("PMM self-test: PASS",))
    vm.check(pmm_snapshot(vm, "Final PMM") == first_pmm,
             "PMM allocation after VMM use preserves pinned table accounting")
    mapping(vm, symbols["__text_start"], "Kernel text after memory stress",
            writable=False, nx=False)

    uptime = vm.command("uptime", ("Uptime:", "ticks",))
    vm.check(bool(re.search(r"\d+h \d+m \d+s", uptime)),
             "Uptime has a numeric duration after memory stress")
    vm.command("echo memory paths remain alive", ("memory paths remain alive",))
    vm.command("echo AbC 123 !?", ("AbC 123 !?",))
    vm.command("versioxx\b\bn", ("UTAMO OS " + vm.args.version,))
    second_system = vm.command("sysinfo", ("Ticks:",))
    old_ticks = number(vm, first_system, "Ticks:", "Initial sysinfo")
    new_ticks = number(vm, second_system, "Ticks:", "Final sysinfo")
    vm.check(new_ticks > old_ticks, "PIT continues across PMM/VMM tests",
             {"before": old_ticks, "after": new_ticks})
    (vm.directory / "pic.txt").write_text(vm.hmp("info pic"))
    (vm.directory / "irq.txt").write_text(vm.hmp("info irq"))
    cleared = vm.command("clear")
    vm.check("\x1b[2J\x1b[H" in cleared,
             "Clear emits serial erase/home and returns the prompt")
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    start = len(vm.serial())
    vm.type_text("halt\n")
    vm.wait_for("System halted", start)
    vm.check(HARNESS.PROMPT not in vm.serial()[start:], "Halt returns no prompt")
    vm.halt_checks()
    vm.check("UTAMO OS KERNEL EXCEPTION" not in vm.serial(),
             "Normal memory suite completes without an unexpected exception")


def fault(vm, kind):
    vm.wait_for(HARNESS.PROMPT)
    start = 0
    if kind in ("vmm", "pf", "stack"):
        start = len(vm.serial())
        vm.type_text("fault " + kind + "\n")
    vm.wait_for("UTAMO OS KERNEL EXCEPTION", start)
    vm.wait_for("Virtual memory context\n", start)
    vm.wait_for("Mapped: Yes" if kind in ("ro", "nx") else "Mapped: No", start)
    vm.halt_checks()
    text = vm.serial()[start:]
    vector = number(vm, text, "Vector:", "Fault report")
    error = number(vm, text, "Error:", "Fault report", True)
    address = number(vm, text, "Fault address:", "Fault report", True)
    expected_error = {"vmm": 2, "ro": 3, "nx": 17, "pf": 2, "stack": 2}[kind]
    expected_address = (OLD_FAULT if kind == "pf" else
                        STACK_GUARD if kind == "stack" else TEST_BASE)
    vm.check("Exception: Page Fault" in text and vector == 14,
             "Controlled memory access reaches the page-fault handler")
    vm.check(error == expected_error, "Page-fault error code matches " + kind,
             {"expected": expected_error, "observed": error})
    vm.check(address == expected_address, "CR2 matches the controlled memory address",
             {"expected": hex(expected_address), "observed": hex(address)})
    for register in ("RIP", "CS", "SS", "RFLAGS", "RSP", "RAX", "RBX", "RCX", "RDX",
                     "RSI", "RDI", "RBP", "R8", "R9", "R10", "R11", "R12",
                     "R13", "R14", "R15"):
        vm.check(bool(re.search(r"\b" + register + r":\s*0x[0-9a-fA-F]+", text)),
                 "Fault report preserves register " + register)
    for register, expected in (("CS", 8), ("SS", 16)):
        value = number(vm, text, register + ":", "Fault selectors", True)
        vm.check(value == expected, "Fault frame retains kernel " + register)
    expected = (
        "Present: Yes (protection)" if kind in ("ro", "nx") else "Present: No",
        "Access: Read" if kind == "nx" else "Access: Write",
        "Mode: Supervisor", "Reserved bit violation: No",
        "Instruction fetch: Yes" if kind == "nx" else "Instruction fetch: No",
    )
    for marker in expected:
        vm.check(marker in text, "Page-fault decode: " + marker)
    memory_text = text.split("Virtual memory context\n", 1)[1]
    vm.check("VMM query: unavailable" not in memory_text,
             "Exception path completes an allocation-free VMM query")
    vm.check(text.index("System halted.") < text.index("Virtual memory context"),
             "Full base serial exception report precedes the optional VMM query")
    if kind in ("ro", "nx"):
        vm.check("Mapped: Yes" in memory_text, "Protection fault retains a present mapping")
        flags = number(vm, memory_text, "Effective flags:", "Fault mapping", True)
        number(vm, memory_text, "Physical:", "Fault mapping", True)
        vm.check(flags & 1 != 0 and flags & 4 == 0,
                 "Fault mapping is present and supervisor-only")
        vm.check(flags & 2 == 0 if kind == "ro" else flags & VMM_NX != 0,
                 "Fault mapping explains the denied access")
    else:
        vm.check("Mapped: No" in memory_text and "Physical:" not in memory_text,
                 "Unmapped fault has no invented physical translation")
    vm.check("recursive CPU exception" not in text,
             "VMM fault diagnostics do not cause a recursive exception")
    vm.check(HARNESS.PROMPT not in text.split("UTAMO OS KERNEL EXCEPTION", 1)[1],
             "Fatal memory fault returns no shell prompt")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--suite", action="store_true")
    mode.add_argument("--fault", choices=("vmm", "ro", "nx", "pf", "stack"))
    parser.add_argument("--name", required=True)
    parser.add_argument("--iso", type=HARNESS.project_path)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ram", default="256M", help="QEMU RAM, for example 64M or 512M")
    parser.add_argument("--cpu", default="qemu64", help="Existing QEMU CPU model/features")
    parser.add_argument("--expect-nx", choices=("on", "off"), default="on")
    parser.add_argument("--version", help="Expected kernel version; defaults to version.h")
    parser.add_argument("--timeout", type=float, default=180.0)
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
        parser.error("--version must be a semantic version such as 0.2.0")
    if args.iso is None:
        candidates = sorted((ROOT / "build").glob("utamo-os-*.iso"))
        if len(candidates) != 1:
            parser.error("Specify --iso when build does not contain exactly one UTAMO ISO")
        args.iso = HARNESS.project_path(candidates[0])
    if not args.iso.is_file():
        parser.error("ISO does not exist")
    # The imported harness accepts these stable attributes. No screenshot path.
    args.probe = {"ro": "memory_fault_readonly", "nx": "memory_fault_nx"}.get(args.fault)
    args.probe_at = "cpu_wait_interrupt"
    args.check_timer = False
    args.gdb_port = None
    args.capture_framebuffer = False
    report = {
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "mode": "memory suite" if args.suite else "memory fault " + args.fault,
        "git_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "git_status": subprocess.check_output(
            ["git", "status", "--short"], cwd=ROOT, text=True),
        "iso": str(args.iso.relative_to(ROOT)), "iso_sha256": HARNESS.digest(args.iso),
        "elf_sha256": HARNESS.digest(ROOT / "build/utamo-kernel.elf"),
        "expected_version": args.version, "checks": [],
        "pending_manual": ["Physical keyboard typing and visual framebuffer review",
                           "UEFI and physical hardware"],
    }
    vm = None
    result = 1
    try:
        symbols = elf_symbols(args) if args.suite else {}
        vm = HARNESS.VM(args, report)
        vm.start()
        if args.suite:
            suite(vm, symbols)
        else:
            fault(vm, args.fault)
        report["passed"] = True
        result = 0
    except (HARNESS.CheckFailed, OSError, ValueError, subprocess.SubprocessError,
            KeyboardInterrupt) as error:
        report["passed"] = False
        report["error"] = str(error)
        print("FAIL " + str(error), file=sys.stderr, flush=True)
    finally:
        if vm is not None:
            vm.close()
            report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            report["check_count"] = len(report["checks"])
            report["failure_count"] = sum(not check["passed"] for check in report["checks"])
            # A transport/timeout can fail before a semantic check is recorded.
            report["run_failure"] = not report.get("passed", False)
            (vm.directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            print("Evidence: " + str(vm.directory.relative_to(ROOT)), flush=True)
    return result


if __name__ == "__main__":
    sys.exit(main())
