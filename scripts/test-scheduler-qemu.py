#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded headless scheduler gate using the existing heap/memory/QMP harnesses.

Run sequentially after building the kernel and ISO:
  python3 scripts/test-scheduler-qemu.py --suite --name sched-256m
  python3 scripts/test-scheduler-qemu.py --suite --name sched-64m --ram 64M
  python3 scripts/test-scheduler-qemu.py --suite --name sched-no-nx --ram 64M \
      --cpu qemu64,-nx --expect-nx off

Commands use actual emulated PS/2 input. GDB only reads kernel state and IDT
entries; it does not call guest functions or fabricate scheduler progress.
No graphics window, automatic build or parallel VM is provided.
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
    "utamo_heap_qemu", ROOT / "scripts/test-heap-qemu.py")
HEAP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HEAP)
MEMORY = HEAP.MEMORY
HARNESS = HEAP.HARNESS

STACK_REGION = 0xffffc00040000000
STACK_SIZE = 65536
STACK_STRIDE = 69632
PAGE_SIZE = 4096
STRESS_REPETITIONS = 3
STRESS_THREADS = 77
MINIMUM_SWITCHES = 4000
LIFETIME_FIELDS = ("created", "exited", "reaped")


def scheduler_snapshot(vm, description):
    text = vm.command("schedulerstats", ("Scheduler",))
    labels = {
        "threads": "Threads:", "current": "Current TID:",
        "quantum": "Quantum ticks:", "ready": "Ready threads:",
        "sleeping": "Sleeping threads:", "blocked": "Blocked threads:",
        "zombies": "Zombie threads:", "switches": "Context switches:",
        "preemptions": "Timer preemptions:", "created": "Threads created:",
        "exited": "Threads exited:", "reaped": "Threads reaped:",
    }
    values = {key: MEMORY.number(vm, text, label, description)
              for key, label in labels.items()}
    vm.check(bool(re.search(r"^Scheduler integrity: OK$", text, re.M)),
             description + " reports valid scheduler state")
    vm.check(all(0 <= value <= HEAP.UINT64_MAX for value in values.values()),
             description + " has unsigned bounded counters")
    vm.check(values["threads"] == 2 and values["current"] == 1,
             description + " runs in the bootstrap shell with only shell and idle")
    vm.check(values["quantum"] == 2, description + " uses a two-tick quantum")
    vm.check(all(values[field] == 0 for field in
                 ("ready", "sleeping", "blocked", "zombies")),
             description + " has no pending worker or unreaped zombie")
    vm.check(values["created"] >= values["exited"] >= values["reaped"]
             and values["threads"] == 2 + values["created"] - values["reaped"],
             description + " reconciles dynamic creation and deferred cleanup")
    vm.report.setdefault("scheduler_snapshots", []).append(
        {"label": description, **values})
    return values


def thread_listing(vm, command, description):
    text = vm.command(command, ("TID STATE NAME",))
    rows = [(int(tid), state, name) for tid, state, name in
            re.findall(r"^(\d+) (RUNNING|READY|BLOCKED|SLEEPING|ZOMBIE) ([^\n]+)$",
                       text, re.M)]
    vm.check(rows == [(0, "READY", "idle"), (1, "RUNNING", "shell")],
             description + " reports the actual two-thread snapshot", rows)
    vm.report.setdefault("thread_lists", []).append(
        {"label": description, "rows": rows})


def stack_mapping_checks(vm, description):
    MEMORY.mapping(vm, STACK_REGION, description + " idle guard",
                   expected_mapped=False)
    for label, address in (("first idle stack byte", STACK_REGION + PAGE_SIZE),
                           ("last idle stack byte",
                            STACK_REGION + PAGE_SIZE + STACK_SIZE - 1)):
        info = MEMORY.mapping(vm, address, description + " " + label,
                              writable=True, nx=vm.args.expect_nx == "on")
        vm.check(info["page_size"] == PAGE_SIZE,
                 description + " " + label + " uses a 4 KiB supervisor leaf")
    # The four concurrently created workers reuse these first dynamic slots.
    for slot in range(1, 5):
        base = STACK_REGION + slot * STACK_STRIDE
        for label, address in (("guard", base), ("first byte", base + PAGE_SIZE),
                               ("last byte", base + PAGE_SIZE + STACK_SIZE - 1)):
            MEMORY.mapping(vm, address,
                           description + " released worker slot " + str(slot) +
                           " " + label, expected_mapped=False)


def read_gdb_number(vm, text, label):
    match = re.search(r"^" + re.escape(label) + r"=(\d+)$", text, re.M)
    vm.check(bool(match), "GDB snapshot includes " + label)
    return int(match.group(1))


def idle_and_gate_checks(vm, symbols, description):
    commands = [
        'printf "SCHED_CURRENT=%llu\n", (unsigned long long)scheduler.current->id',
        'printf "SCHED_SHELL_STATE=%u\n", (unsigned int)bootstrap_thread.task.state',
        'printf "SCHED_IDLE_STATE=%u\n", (unsigned int)idle_thread.task.state',
        'printf "SCHED_TASKS=%llu\n", (unsigned long long)scheduler.task_count',
        'printf "SCHED_DEPTH=%u\n", (unsigned int)scheduler.current->preempt_depth',
        'printf "SCHED_STACK_BASE=%llu\n", (unsigned long long)idle_thread.stack.base',
        'printf "SCHED_STACK_TOP=%llu\n", (unsigned long long)idle_thread.stack.top',
        'printf "SCHED_GUARD=%llu\n", (unsigned long long)idle_thread.stack.guard',
        'printf "SCHED_RSP=%llu\n", (unsigned long long)$rsp',
        'printf "YIELD_ATTRIBUTES=%u\n", (unsigned int)idt[240].type_attributes',
        'printf "YIELD_SELECTOR=%u\n", (unsigned int)idt[240].selector',
        'printf "YIELD_IST=%u\n", (unsigned int)idt[240].ist',
        'printf "YIELD_RESERVED=%u\n", (unsigned int)idt[240].reserved',
        'printf "YIELD_TARGET=%llu\n", (unsigned long long)idt[240].offset_low | '
        '((unsigned long long)idt[240].offset_middle << 16) | '
        '((unsigned long long)idt[240].offset_high << 32)',
        "monitor info registers",
    ]
    # GDB expects a literal backslash-n inside printf, not a newline in its
    # command argument. It stops the VM while all fields below are sampled.
    commands = [command.replace("\n", r"\n") for command in commands]
    observed = None
    for attempt in range(10):
        vm.remaining()
        time.sleep(0.03)
        text = vm.gdb(description + "-idle-" + str(attempt), commands)
        fields = ("SCHED_CURRENT", "SCHED_SHELL_STATE", "SCHED_IDLE_STATE",
                  "SCHED_TASKS", "SCHED_DEPTH", "SCHED_STACK_BASE",
                  "SCHED_STACK_TOP", "SCHED_GUARD", "SCHED_RSP",
                  "YIELD_ATTRIBUTES", "YIELD_SELECTOR", "YIELD_IST",
                  "YIELD_RESERVED", "YIELD_TARGET")
        observed = {field: read_gdb_number(vm, text, field) for field in fields}
        if (observed["SCHED_CURRENT"] == 0 and
                observed["SCHED_SHELL_STATE"] == 2 and
                observed["SCHED_IDLE_STATE"] == 0):
            break
    vm.check(observed["SCHED_CURRENT"] == 0
             and observed["SCHED_SHELL_STATE"] == 2
             and observed["SCHED_IDLE_STATE"] == 0,
             description + " idle executes while the shell blocks for input",
             observed)
    vm.check(observed["SCHED_TASKS"] == 2 and observed["SCHED_DEPTH"] == 0,
             description + " idle has no workers or leaked preemption nesting")
    vm.check(observed["SCHED_GUARD"] == STACK_REGION
             and observed["SCHED_STACK_BASE"] == STACK_REGION + PAGE_SIZE
             and observed["SCHED_STACK_TOP"] == STACK_REGION + PAGE_SIZE + STACK_SIZE,
             description + " idle owns the expected guarded 64 KiB stack")
    vm.check(observed["SCHED_STACK_BASE"] <= observed["SCHED_RSP"]
             < observed["SCHED_STACK_TOP"],
             description + " actual RSP is on the idle thread stack")
    vm.check(observed["YIELD_ATTRIBUTES"] == 0x8e
             and observed["YIELD_SELECTOR"] == 8
             and observed["YIELD_IST"] == 0
             and observed["YIELD_RESERVED"] == 0,
             description + " INT240 uses a present DPL0 interrupt gate without IST")
    vm.check(observed["YIELD_TARGET"] == symbols["interrupt_stub_240"],
             description + " live INT240 gate targets its validated NASM stub")
    vm.report.setdefault("idle_snapshots", []).append(
        {"label": description, **observed})


def memory_snapshots(vm, description):
    return (HEAP.heap_snapshot(vm, description + " heap"),
            MEMORY.pmm_snapshot(vm, description + " PMM"),
            MEMORY.vmm_snapshot(vm, description + " VMM"))


def check_memory_after(vm, previous, current, description):
    old_heap, old_pmm, old_vmm = previous
    heap, pmm, vmm = current
    vm.check(heap["used"] == old_heap["used"] and heap["live"] == old_heap["live"],
             description + " restores heap requested bytes and live allocations")
    HEAP.check_growth_accounting(vm, old_heap, heap, old_pmm, pmm,
                                 old_vmm, vmm, description)


def suite(vm, symbols):
    boot = vm.wait_for(HARNESS.PROMPT)
    for marker in ("Version: " + vm.args.version, "GDT initialized", "IDT initialized",
                   "PMM initialized", "VMM initialized", "Kernel heap initialized",
                   "Kernel scheduler initialized", "PIT timer initialized",
                   "PS/2 keyboard initialized", "Interrupts enabled", "UTAMO OS ready."):
        vm.check(marker in boot, "Boot marker: " + marker)
    vm.command("help", ("ps", "threads", "schedulerstats", "schedtest", "sleep",
                        "heaptest", "pmmtest", "vmmtest"))
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    first_system = vm.command("sysinfo", ("Ticks:", "x86_64", "Limine"))
    initial = memory_snapshots(vm, "Initial scheduler baseline")
    vm.check(initial[0]["mapped"] == HEAP.HEAP_INITIAL_BYTES
             and initial[0]["used"] == 0 and initial[0]["live"] == 0,
             "Static bootstrap/idle TCBs leave the initial 64 KiB heap empty")
    MEMORY.hardware_memory_checks(vm, initial[2])
    HEAP.kernel_mapping_checks(vm, symbols, "Initial kernel")
    HEAP.heap_mapping_checks(vm, initial[0], "Initial heap")
    stack_mapping_checks(vm, "Initial stacks")
    previous_sched = scheduler_snapshot(vm, "Initial scheduler")
    thread_listing(vm, "ps", "Initial ps")
    thread_listing(vm, "threads", "Initial threads alias")
    idle_and_gate_checks(vm, symbols, "initial")

    for command in ("ps extra", "threads extra", "schedulerstats extra",
                    "schedtest extra"):
        vm.command(command, ("Unexpected arguments.",))
    for command in ("sleep", "sleep -1", "sleep +1", "sleep 0x10",
                    "sleep 1ms", "sleep 1 2", "sleep 18446744073709551616"):
        response = vm.command(command, ("Usage: sleep <decimal-ms>",))
        vm.check("Sleep completed." not in response,
                 repr(command) + " cannot report a completed sleep")
    rejected_sched = scheduler_snapshot(vm, "After rejected arguments")
    vm.check(all(rejected_sched[key] == previous_sched[key] for key in LIFETIME_FIELDS),
             "Invalid arguments cannot create, exit or reap a thread")
    rejected = memory_snapshots(vm, "After rejected arguments")
    vm.check(rejected == initial, "Invalid arguments leave memory accounting unchanged")

    for milliseconds in (0, 1, 25):
        vm.command("sleep " + str(milliseconds), ("Sleep completed.",))
    # Cross-command PIT deltas include keyboard input and are not a precise
    # sleep-duration assertion. The kernel self-test checks its own tick boundary.
    idle_and_gate_checks(vm, symbols, "after-sleep")

    previous = initial
    stable = None
    for repetition in range(STRESS_REPETITIONS):
        label = "Scheduler stress " + str(repetition + 1)
        before_sched = scheduler_snapshot(vm, label + " before")
        response = vm.command("schedtest", ("Scheduler self-test: PASS",
                              "Scheduler stress: deterministic workers, 16 lifecycle batches"))
        operations = MEMORY.number(vm, response, "Scheduler completed operations:", label)
        switches = MEMORY.number(vm, response, "Context switch delta:", label)
        preemptions = MEMORY.number(vm, response, "Timer preemption delta:", label)
        reaped = MEMORY.number(vm, response, "Reaped thread delta:", label)
        cpu_iterations = MEMORY.number(vm, response, "CPU-bound iterations:", label)
        vm.check(operations == 3105, label + " completes exactly 3105 deterministic operations")
        vm.check(cpu_iterations > 0, label + " executes noncooperative CPU work")
        vm.check(switches >= MINIMUM_SWITCHES, label + " observes at least 4000 context switches")
        vm.check(preemptions > 0, label + " observes timer-driven preemption")
        vm.check(reaped == STRESS_THREADS, label + " reaps all 77 created workers")
        after_sched = scheduler_snapshot(vm, label + " after")
        vm.check(all(after_sched[key] - before_sched[key] == STRESS_THREADS
                     for key in LIFETIME_FIELDS),
                 label + " creates, exits and reaps exactly 77 threads")
        vm.check(after_sched["switches"] - before_sched["switches"] >= switches
                 and after_sched["preemptions"] - before_sched["preemptions"] >= preemptions,
                 label + " reconciles self-test deltas with public monotonic counters")
        current = memory_snapshots(vm, label)
        check_memory_after(vm, previous, current, label)
        if stable is None:
            stable = current
        else:
            vm.check(all(current[0][key] == stable[0][key] for key in HEAP.STATE_FIELDS)
                     and current[1:] == stable[1:],
                     label + " reuses heap, stack data frames and intermediate tables")
        thread_listing(vm, "ps", label + " cleanup")
        vm.report.setdefault("scheduler_stress", []).append({
            "repetition": repetition + 1, "operations": operations,
            "cpu_bound_iterations": cpu_iterations, "switches": switches,
            "timer_preemptions": preemptions, "reaped": reaped})
        previous, previous_sched = current, after_sched

    stack_mapping_checks(vm, "After scheduler stress")
    idle_and_gate_checks(vm, symbols, "after-stress")
    response = vm.command("heaptest", ("Heap self-test: PASS",))
    vm.check(MEMORY.number(vm, response, "Heap stress seed:", "Heap regression", True)
             == HEAP.HEAP_STRESS_SEED, "Heap regression preserves the deterministic seed")
    vm.check(MEMORY.number(vm, response, "Heap stress operations:", "Heap regression")
             >= HEAP.HEAP_STRESS_OPERATIONS, "Heap regression completes 8192 operations")
    current = memory_snapshots(vm, "After heap regression")
    check_memory_after(vm, previous, current, "Heap after scheduler stress")
    previous = current

    vm.command("pmmtest", ("PMM self-test: PASS",))
    current = memory_snapshots(vm, "After PMM regression")
    vm.check(current == previous, "PMM regression restores exact post-scheduler memory state")
    for repetition in range(2):
        label = "VMM regression " + str(repetition + 1)
        vm.command("vmmtest", ("VMM self-test: PASS",))
        current = memory_snapshots(vm, label)
        check_memory_after(vm, previous, current, label)
        vm.check(current[0] == previous[0], label + " leaves heap lifetime counters unchanged")
        if repetition:
            vm.check(current == previous, label + " reuses retained tables without leaks")
        previous = current
    MEMORY.mapping(vm, MEMORY.TEST_BASE, "VMM regression cleanup", expected_mapped=False)
    final_sched = scheduler_snapshot(vm, "After memory regressions")
    vm.check(all(final_sched[key] == previous_sched[key] for key in LIFETIME_FIELDS),
             "Heap/PMM/VMM regressions do not create or destroy threads")
    HEAP.kernel_mapping_checks(vm, symbols, "Final kernel")
    HEAP.heap_mapping_checks(vm, previous[0], "Final heap")
    stack_mapping_checks(vm, "Final stacks")
    HEAP.check_running_if(vm)
    idle_and_gate_checks(vm, symbols, "final")

    vm.command("echo scheduler paths remain alive", ("scheduler paths remain alive",))
    vm.command("echo AbC 123 !?", ("AbC 123 !?",))
    vm.command("versioxx\b\bn", ("UTAMO OS " + vm.args.version,))
    vm.command("uptime", ("Uptime:", "ticks"))
    final_system = vm.command("sysinfo", ("Ticks:",))
    before_ticks = MEMORY.number(vm, first_system, "Ticks:", "Initial PIT")
    after_ticks = MEMORY.number(vm, final_system, "Ticks:", "Final PIT")
    vm.check(after_ticks > before_ticks, "PIT advances across scheduler and allocator stress",
             {"before": before_ticks, "after": after_ticks})
    (vm.directory / "pic.txt").write_text(vm.hmp("info pic"))
    (vm.directory / "irq.txt").write_text(vm.hmp("info irq"))
    cleared = vm.command("clear")
    vm.check("\x1b[2J\x1b[H" in cleared, "Clear emits serial erase/home and returns the prompt")
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    start = len(vm.serial())
    vm.type_text("halt\n")
    vm.wait_for("System halted.", start)
    vm.check(HARNESS.PROMPT not in vm.serial()[start:], "Halt returns no prompt")
    vm.halt_checks()
    vm.check("UTAMO OS KERNEL EXCEPTION" not in vm.serial()
             and "UTAMO KERNEL PANIC" not in vm.serial(),
             "Scheduler gate completes without an unexpected exception or panic")


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
    parser.add_argument("--timeout", type=float, default=300.0)
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
    args.check_timer = True  # Reuse its private GDB socket; no timer probe is armed.
    args.gdb_port = None
    args.capture_framebuffer = False
    report = {
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "mode": "scheduler suite", "checks": [],
        "git_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "git_status": subprocess.check_output(
            ["git", "status", "--short"], cwd=ROOT, text=True),
        "iso": str(args.iso.relative_to(ROOT)), "iso_sha256": HARNESS.digest(args.iso),
        "elf_sha256": HARNESS.digest(ROOT / "build/utamo-kernel.elf"),
        "expected_version": args.version, "expected_nx": args.expect_nx,
        "stress_repetitions": STRESS_REPETITIONS,
        "threads_per_stress": STRESS_THREADS,
        "minimum_switches_per_stress": MINIMUM_SWITCHES,
        "pending_manual": ["Physical keyboard and visual framebuffer review",
                           "UEFI and physical hardware"],
        "separate_host_coverage": [
            "Queue ownership, deadline overflow, nesting and registry exhaustion",
            "Stack allocation rollback, stale descriptors and synthetic frame layout",
            "Invalid frees, double frees and allocator rollback"],
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
