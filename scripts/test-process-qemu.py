#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded headless CPL3 process gate using the existing VM/memory harnesses.

Run sequentially after building, for example:
  python3 scripts/test-process-qemu.py --suite --name process-256m
  python3 scripts/test-process-qemu.py --suite --name process-no-nx --ram 64M \
      --cpu qemu64,-nx --expect-nx off

All shell input uses QMP PS/2 keys. The first available usertest is observed
with a temporary hardware breakpoint at the embedded user entry, without
editing guest registers or memory. No build, graphics or concurrent VM.
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
    "utamo_scheduler_qemu", ROOT / "scripts/test-scheduler-qemu.py")
SCHED = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCHED)
HEAP, MEMORY, HARNESS = SCHED.HEAP, SCHED.MEMORY, SCHED.HARNESS

PAGE_SIZE = 4096
USER_CODE = 0x400000
USER_DATA = 0x600000
USER_STACK_TOP = 0x70000000
USER_STACK_BYTES = 16 * PAGE_SIZE
USER_CODE_SELECTOR = 0x1b
USER_DATA_SELECTOR = 0x23
USERTEST_REPETITIONS = 3
PROCESSES_PER_USERTEST = 40
SYSCALLS_PER_USERTEST = 327900
LIFETIME_FIELDS = ("created", "exited", "reaped")
COUNTER_FIELDS = LIFETIME_FIELDS + ("faults", "syscalls", "preemptions", "switches")


def process_snapshot(vm, description):
    text = vm.command("processes", ("Processes",))
    labels = {
        "active": "Active processes:", "created": "Processes created:",
        "exited": "Processes exited:", "reaped": "Processes reaped:",
        "faults": "User faults:", "syscalls": "Syscalls:",
        "preemptions": "User timer preemptions:",
        "switches": "Address-space switches:",
    }
    values = {key: MEMORY.number(vm, text, label, description)
              for key, label in labels.items()}
    match = re.search(r"^Available: (yes|no)$", text, re.M)
    vm.check(bool(match), description + " reports user process availability")
    values["available"] = match.group(1) == "yes"
    vm.check(values["available"] == (vm.args.expect_nx == "on"),
             description + " availability follows the required NX capability")
    vm.check(all(0 <= values[key] <= HEAP.UINT64_MAX for key in labels),
             description + " reports bounded unsigned lifetime counters")
    vm.check(values["active"] == 0
             and values["created"] == values["exited"] == values["reaped"],
             description + " has no live or unreaped user process", values)
    vm.report.setdefault("process_snapshots", []).append(
        {"label": description, **values})
    return values


def gdb_values(vm, text, labels, description):
    result = {}
    for label in labels:
        match = re.search(r"^" + re.escape(label) + r"=(\d+)$", text, re.M)
        vm.check(bool(match), description + " contains " + label)
        result[label] = int(match.group(1))
    return result


def architecture_checks(vm, symbols, description):
    commands = [
        'printf "USER_GDT_CODE=%llu\\n", (unsigned long long)gdt[3]',
        'printf "USER_GDT_DATA=%llu\\n", (unsigned long long)gdt[4]',
        'printf "USER_TSS_TYPE=%llu\\n", ((unsigned long long)gdt[5] >> 40) & 15',
        'printf "USER_TSS_IOMAP=%u\\n", (unsigned int)tss.iomap_base',
        'printf "USER_GATE_ATTRIBUTES=%u\\n", (unsigned int)idt[128].type_attributes',
        'printf "USER_GATE_SELECTOR=%u\\n", (unsigned int)idt[128].selector',
        'printf "USER_GATE_IST=%u\\n", (unsigned int)idt[128].ist',
        'printf "USER_GATE_RESERVED=%u\\n", (unsigned int)idt[128].reserved',
        'printf "USER_GATE_TARGET=%llu\\n", (unsigned long long)idt[128].offset_low | '
        '((unsigned long long)idt[128].offset_middle << 16) | '
        '((unsigned long long)idt[128].offset_high << 32)',
        'printf "USER_YIELD_ATTRIBUTES=%u\\n", (unsigned int)idt[240].type_attributes',
        "monitor info registers",
    ]
    text = vm.gdb(description + "-architecture", commands)
    labels = ("USER_GDT_CODE", "USER_GDT_DATA", "USER_TSS_TYPE",
              "USER_TSS_IOMAP", "USER_GATE_ATTRIBUTES", "USER_GATE_SELECTOR",
              "USER_GATE_IST", "USER_GATE_RESERVED", "USER_GATE_TARGET",
              "USER_YIELD_ATTRIBUTES")
    values = gdb_values(vm, text, labels, description)
    accessed = 1 << 40  # Hardware may set descriptor Accessed after CPL3 loads.
    vm.check(values["USER_GDT_CODE"] & ~accessed == 0x00affa000000ffff,
             description + " has the DPL3 64-bit code descriptor")
    vm.check(values["USER_GDT_DATA"] & ~accessed == 0x00cff2000000ffff,
             description + " has the DPL3 data descriptor")
    vm.check(values["USER_TSS_TYPE"] == 11 and values["USER_TSS_IOMAP"] == 104,
             description + " retains the loaded busy TSS and denies user port IO")
    vm.check(values["USER_GATE_ATTRIBUTES"] == (0xee if vm.args.expect_nx == "on" else 0x8e)
             and values["USER_GATE_SELECTOR"] == 8
             and values["USER_GATE_IST"] == 0
             and values["USER_GATE_RESERVED"] == 0,
             description + " opens INT128 to CPL3 only after supported initialization")
    vm.check(values["USER_GATE_TARGET"] == symbols["interrupt_stub_128"],
             description + " INT128 points to its normalized assembly entry")
    vm.check(values["USER_YIELD_ATTRIBUTES"] == 0x8e,
             description + " keeps the scheduler INT240 gate restricted to CPL0")
    vm.report.setdefault("process_architecture", []).append(
        {"label": description, **values})


def cpl3_commands(vmm):
    """GDB observes only: its scratch variables live in GDB, not the guest."""
    commands = [
        "hbreak *" + hex(USER_CODE),
        'echo UTAMO_USER_BREAKPOINT_ARMED\\n',
        "continue",
        'printf "CPL3_CS=%llu\\n", (unsigned long long)$cs',
        'printf "CPL3_SS=%llu\\n", (unsigned long long)$ss',
        'printf "CPL3_RIP=%llu\\n", (unsigned long long)$rip',
        'printf "CPL3_RSP=%llu\\n", (unsigned long long)$rsp',
        'printf "CPL3_CR3=%llu\\n", (unsigned long long)$cr3',
        'printf "CPL3_RSP0=%llu\\n", (unsigned long long)tss.rsp[0]',
        'printf "CPL3_FS=%llu\\n", (unsigned long long)$fs',
        'printf "CPL3_GS=%llu\\n", (unsigned long long)$gs',
        "set $task_hhdm = " + hex(vmm["hhdm"]),
        "set $task_mask = " + hex(((1 << vmm["bits"]) - 1) & ~4095),
        "set $task_root = ((unsigned long long)$cr3) & $task_mask",
        "set $task_pml4 = (unsigned long long*)($task_hhdm + $task_root)",
        'printf "CPL3_KERNEL_PML4=%llu\\n", $task_pml4[511]',
        'printf "CPL3_HEAP_PML4=%llu\\n", $task_pml4[384]',
        'printf "CPL3_HHDM_PML4=%llu\\n", $task_pml4['
        + str((vmm["hhdm"] >> 39) & 511) + "]",
    ]
    for label, address in (("CODE", USER_CODE), ("DATA", USER_DATA),
                           ("STACK", USER_STACK_TOP - 1),
                           ("GUARD", USER_STACK_TOP - USER_STACK_BYTES - PAGE_SIZE)):
        pml4, pdpt, pd, pt = ((address >> bit) & 511 for bit in (39, 30, 21, 12))
        commands += [
            'printf "CPL3_' + label + '_PML4=%llu\\n", $task_pml4[' + str(pml4) + "]",
            "set $task_pdpt = (unsigned long long*)($task_hhdm + "
            "($task_pml4[" + str(pml4) + "] & $task_mask))",
            'printf "CPL3_' + label + '_PDPT=%llu\\n", $task_pdpt[' + str(pdpt) + "]",
            "set $task_pd = (unsigned long long*)($task_hhdm + "
            "($task_pdpt[" + str(pdpt) + "] & $task_mask))",
            'printf "CPL3_' + label + '_PD=%llu\\n", $task_pd[' + str(pd) + "]",
            "set $task_pt = (unsigned long long*)($task_hhdm + "
            "($task_pd[" + str(pd) + "] & $task_mask))",
            'printf "CPL3_' + label + '_PT=%llu\\n", $task_pt[' + str(pt) + "]",
        ]
    commands += ["monitor info registers", "delete breakpoints", "detach"]
    return commands


def usertest_with_cpl3_snapshot(vm, vmm):
    """Arm a hardware breakpoint before PS/2 input; reap this GDB on every path."""
    vm.qmp("stop")
    log_path = vm.directory / "first-usertest-cpl3-gdb.log"
    command = ["gdb", "--batch", "--nx", str(ROOT / "build/utamo-kernel.elf"),
               "-ex", "set pagination off", "-ex", "set confirm off",
               "-ex", "target remote " + str(vm.gdb_path)]
    for item in cpl3_commands(vmm):
        command += ["-ex", item]
    debugger = None
    start = len(vm.serial())
    try:
        with log_path.open("w") as stream:
            debugger = subprocess.Popen(command, cwd=ROOT, text=True,
                                        stdout=stream, stderr=subprocess.STDOUT,
                                        stdin=subprocess.DEVNULL)
            end = time.monotonic() + min(12.0, vm.remaining())
            while "UTAMO_USER_BREAKPOINT_ARMED" not in log_path.read_text():
                if debugger.poll() is not None or time.monotonic() >= end:
                    raise HARNESS.CheckFailed("GDB failed to arm the CPL3 hardware breakpoint")
                time.sleep(0.02)
            vm.type_text("usertest\n")
            debugger.wait(timeout=min(15.0, vm.remaining()))
        text = log_path.read_text()
        vm.check(debugger.returncode == 0, "CPL3 observer GDB completed successfully",
                 {"returncode": debugger.returncode, "output": text})
        vm.qmp("cont")
        received = vm.wait_for(HARNESS.PROMPT, start)
    finally:
        if debugger is not None:
            if debugger.poll() is None:
                debugger.terminate()
                try:
                    debugger.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    debugger.kill()
                    debugger.wait(timeout=2)
            vm.report["cpl3_gdb_process_reaped"] = debugger.poll() is not None
        # A failure never leaves our VM stopped for cleanup or a later command.
        if vm.process.poll() is None:
            vm.qmp("cont")
    check_cpl3_snapshot(vm, text, vmm)
    response = received.split("\n", 1)[1] if "\n" in received else ""
    response = response.rsplit(HARNESS.PROMPT, 1)[0]
    vm.report.setdefault("commands", []).append(
        {"input": "usertest", "serial": received, "response": response})
    return response


def check_cpl3_snapshot(vm, text, vmm):
    labels = ("CPL3_CS", "CPL3_SS", "CPL3_RIP", "CPL3_RSP",
              "CPL3_CR3", "CPL3_RSP0", "CPL3_FS", "CPL3_GS",
              "CPL3_KERNEL_PML4", "CPL3_HEAP_PML4", "CPL3_HHDM_PML4")
    labels += tuple("CPL3_" + leaf + "_" + level
                    for leaf in ("CODE", "DATA", "STACK", "GUARD")
                    for level in ("PML4", "PDPT", "PD", "PT"))
    values = gdb_values(vm, text, labels, "First actual user entry")
    # GDB's target-specific $eflags type rejects an integer cast on some
    # installed builds. QEMU's monitor prints the architectural RFL value from
    # this same stopped CPU, with no extra resume/sample race.
    match = re.search(r"\bRFL=([0-9a-fA-F]+)", text)
    vm.check(bool(match), "First actual user entry contains hardware RFLAGS")
    values["CPL3_RFLAGS"] = int(match.group(1), 16)
    vm.check(values["CPL3_CS"] == USER_CODE_SELECTOR
             and values["CPL3_SS"] == USER_DATA_SELECTOR,
             "Actual execution enters CPL3 with the intended GDT selectors")
    vm.check(values["CPL3_RIP"] == USER_CODE
             and USER_STACK_TOP - USER_STACK_BYTES <= values["CPL3_RSP"] <= USER_STACK_TOP,
             "Actual user RIP and RSP refer to the embedded code and private stack")
    vm.check(values["CPL3_CR3"] != vmm["root"]
             and values["CPL3_CR3"] > 0 and values["CPL3_CR3"] % PAGE_SIZE == 0,
             "CPL3 executes using a private physical CR3 without PCID bits")
    forbidden_flags = (3 << 12) | (1 << 14) | (1 << 17) | (1 << 18) | (1 << 10)
    vm.check(values["CPL3_RFLAGS"] & 0x200 != 0
             and values["CPL3_RFLAGS"] & forbidden_flags == 0,
             "User entry enables IRQs without IOPL, NT, VM, AC or DF")
    vm.check(values["CPL3_FS"] == 0 and values["CPL3_GS"] == 0,
             "User entry does not inherit kernel FS or GS selectors")
    vm.check(values["CPL3_RSP0"] > SCHED.STACK_REGION
             and values["CPL3_RSP0"] % 16 == 0,
             "TSS RSP0 selects an aligned higher-half kernel stack")
    for label in ("KERNEL", "HEAP", "HHDM"):
        entry = values["CPL3_" + label + "_PML4"]
        vm.check(entry & 1 and entry & 4 == 0,
                 "Private CR3 keeps the shared " + label + " branch supervisor-only")
    for leaf in ("CODE", "DATA", "STACK"):
        for level in ("PML4", "PDPT", "PD"):
            entry = values["CPL3_" + leaf + "_" + level]
            vm.check(entry & 7 == 7 and entry & (1 << 63) == 0,
                     leaf + " " + level + " permits the intended user leaf")
            if level != "PML4":
                vm.check(entry & 0x80 == 0, leaf + " " + level + " is a table, not a huge leaf")
        entry = values["CPL3_" + leaf + "_PT"]
        writable = leaf != "CODE"
        vm.check(entry & 5 == 5 and bool(entry & 2) == writable
                 and bool(entry & (1 << 63)) == writable,
                 leaf + " uses a present user 4 KiB leaf with intended W/NX permissions")
    vm.check(values["CPL3_GUARD_PT"] & 1 == 0,
             "The private user stack guard has no present leaf")
    vm.report["cpl3_snapshot"] = values


def user_fault_checks(vm, response, description):
    matches = re.findall(
        r"^User probe result: probe (\d+) PID (\d+) exit (-?\d+) "
        r"vector (\d+) error 0x([0-9a-fA-F]+)$", response, re.M)
    vm.check(bool(matches), description + " contains structured per-probe results")
    results = [{"probe": int(probe), "pid": int(pid), "exit": int(status),
                "vector": int(vector), "error": int(error, 16)}
               for probe, pid, status, vector, error in matches]
    vm.check(len({item["pid"] for item in results}) == len(results)
             and all(item["pid"] > 0 for item in results),
             description + " assigns a distinct positive PID to each reported process")
    expected = {
        1: {(14, 5)}, 2: {(14, 7)}, 3: {(14, 21)}, 4: {(6, 0)},
        5: {(0, 0)}, 6: {(13, 0)}, 7: {(13, 0)}, 8: {(13, 0x782)},
        9: {(6, 0)}, 10: {(13, 0), (6, 0)}, 11: {(7, 0)},
        12: {(13, 0)}, 14: {(13, 0)},
    }
    faults = [item for item in results if item["probe"] in expected]
    vm.check(sorted(item["probe"] for item in faults) == sorted(expected),
             description + " reports all 13 controlled fault probes exactly once")
    success = [item for item in results if item["probe"] == 13]
    vm.check(len(success) == 1 and success[0]["exit"] == 0
             and success[0]["vector"] == 0 and success[0]["error"] == 0,
             description + " completes pointer rejection checks without faulting")
    vm.check(response.count("User isolated pair: PASS") == 5
             and "User isolated pair: FAIL" not in response,
             description + " completes all five isolated concurrent process pairs")
    vm.check(len(faults) + len(success) == len(results),
             description + " reports only defined embedded probes")
    blocks = re.findall(
        r"UTAMO OS USER PROCESS EXCEPTION\n(.*?)"
        r"Process terminated; kernel continues\.\n", response, re.S)
    vm.check(len(blocks) == len(expected),
             description + " records exactly 13 complete isolated exception dumps")
    diagnostics = {}
    for block in blocks:
        match = re.search(r"^PID:\s+(\d+)$", block, re.M)
        vm.check(bool(match), description + " exception dump contains its PID")
        pid = int(match.group(1))
        vm.check(pid not in diagnostics, description + " has one exception dump per failed PID")
        diagnostics[pid] = block
    for result in faults:
        label = description + " probe " + str(result["probe"])
        vector, error = result["vector"], result["error"]
        vm.check((vector, error) in expected[result["probe"]]
                 and result["exit"] == -(128 + vector),
                 label + " reports its intended exception and fatal process exit", result)
        vm.check(result["pid"] in diagnostics, label + " has a complete serial register dump")
        block = diagnostics[result["pid"]]
        registers = {}
        for register in ("RIP", "RSP", "CS", "SS", "RFLAGS", "Error",
                         "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP",
                         "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"):
            match = re.search(r"\b" + register + r":\s+0x([0-9a-fA-F]{16})\b", block)
            vm.check(bool(match), label + " preserves " + register + " in the dump")
            registers[register] = int(match.group(1), 16)
        reported_vector = re.search(r"^Vector:\s+(\d+)$", block, re.M)
        vm.check(bool(reported_vector) and int(reported_vector.group(1)) == vector
                 and registers["Error"] == error,
                 label + " dump matches the reaped process result")
        vm.check(registers["CS"] == USER_CODE_SELECTOR
                 and registers["SS"] == USER_DATA_SELECTOR
                 and registers["RFLAGS"] & 0x200 != 0,
                 label + " originated from CPL3 with IRQs enabled")
        expected_rsp = (0x0000800000000000 if result["probe"] == 12
                        else 0xffffffff80000000 if result["probe"] == 14 else None)
        vm.check(registers["RSP"] == expected_rsp if expected_rsp is not None else
                 USER_STACK_TOP - USER_STACK_BYTES <= registers["RSP"] < USER_STACK_TOP,
                 label + " preserves the actual user or hostile stack pointer")
        vm.check(registers["RIP"] == USER_DATA if result["probe"] == 3 else
                 USER_CODE <= registers["RIP"] < USER_CODE + PAGE_SIZE,
                 label + " preserves the faulting user instruction address")
        if vector == 14:
            address = re.search(r"^Fault address:\s+0x([0-9a-fA-F]{16})$", block, re.M)
            vm.check(bool(address) and int(address.group(1), 16) ==
                     (USER_DATA if result["probe"] == 3 else 0xffffffff80000000),
                     label + " preserves actual CR2")
            for marker in ("Present: Yes (protection)", "Mode: User",
                           "Reserved bit violation: No",
                           "Access: Write" if result["probe"] == 2 else "Access: Read",
                           "Instruction fetch: Yes" if result["probe"] == 3
                           else "Instruction fetch: No"):
                vm.check(marker in block, label + " decodes " + marker)
        summary = ("User process fault: PID " + str(result["pid"]) + " vector " +
                   str(vector) + " error 0x" + format(error, "x") + "; terminated")
        vm.check(summary in response, label + " is also reported through the normal kernel logger")
    vm.check(response.count("Hello from UTAMO ring 3!") == 10,
             description + " receives real write syscalls from all ten good processes")
    vm.report.setdefault("user_probe_results", []).append(
        {"label": description, "results": results})
    return results


def process_stress(vm, symbols, initial, initial_processes):
    previous = initial
    previous_processes = initial_processes
    stable = None
    for repetition in range(USERTEST_REPETITIONS):
        label = "User process stress " + str(repetition + 1)
        response = (usertest_with_cpl3_snapshot(vm, initial[2]) if repetition == 0
                    else vm.command("usertest"))
        vm.check("User process self-test: PASS" in response
                 and "User process self-test: FAIL" not in response,
                 label + " reports successful bounded isolation and lifecycle checks")
        user_fault_checks(vm, response, label)
        vm.check(response.count(
                     "User process capacity: PASS (16 active, extra creation rejected)") == 1
                 and "User process capacity: FAIL" not in response,
                 label + " fills all 16 process slots and rejects an extra creation transaction")
        selftest_values = {
            key: MEMORY.number(vm, response, marker, label) for key, marker in (
                ("created", "User process created delta:"),
                ("reaped", "User process reaped delta:"), ("faults", "User fault delta:"),
                ("syscalls", "User syscall delta:"),
                ("preemptions", "User timer preemption delta:"),
                ("switches", "Address-space switch delta:"),
                ("retained", "User retained kernel pages:"))}
        vm.check(selftest_values["created"] == PROCESSES_PER_USERTEST
                 and selftest_values["reaped"] == PROCESSES_PER_USERTEST
                 and selftest_values["faults"] == 13
                 and selftest_values["syscalls"] == SYSCALLS_PER_USERTEST,
                 label + " reports the exact deterministic process/fault/syscall counts",
                 selftest_values)
        vm.check(selftest_values["preemptions"] > 0 and selftest_values["switches"] > 0,
                 label + " observes real user timer preemption and CR3 switching")
        current_processes = process_snapshot(vm, label)
        vm.check(current_processes["faults"] - previous_processes["faults"] == 13,
                 label + " lifetime counters record exactly the 13 isolated faults")
        vm.check(all(current_processes[key] - previous_processes[key] == PROCESSES_PER_USERTEST
                     for key in LIFETIME_FIELDS),
                 label + " creates, exits and reaps exactly 40 processes")
        vm.check(all(current_processes[key] - previous_processes[key] == selftest_values[key]
                     for key in ("created", "reaped", "faults", "syscalls",
                                 "preemptions", "switches")),
                 label + " reconciles public process counters with self-test deltas")
        vm.check(all(current_processes[key] >= previous_processes[key]
                     for key in COUNTER_FIELDS),
                 label + " preserves monotonic process statistics")
        vm.check(current_processes["created"] > previous_processes["created"]
                 and current_processes["faults"] > previous_processes["faults"]
                 and current_processes["syscalls"] > previous_processes["syscalls"]
                 and current_processes["preemptions"] > previous_processes["preemptions"]
                 and current_processes["switches"] > previous_processes["switches"],
                 label + " observes process creation, isolated faults, syscalls and preemption")
        current = SCHED.memory_snapshots(vm, label)
        SCHED.check_memory_after(vm, previous, current, label)
        vm.check(current[1]["used"] - previous[1]["used"] == selftest_values["retained"],
                 label + " retains only the explicitly accounted kernel pages")
        vm.report.setdefault("user_stress", []).append(
            {"repetition": repetition + 1, **selftest_values})
        if stable is None:
            stable = current
        else:
            vm.check(all(current[0][key] == stable[0][key] for key in HEAP.STATE_FIELDS)
                     and current[1:] == stable[1:],
                     label + " reuses kernel metadata without leaking private user pages")
        vm.command("echo kernel survived user faults", ("kernel survived user faults",))
        SCHED.thread_listing(vm, "ps", label + " kernel thread cleanup")
        previous, previous_processes = current, current_processes
    architecture_checks(vm, symbols, "After user stress")
    return previous, previous_processes


def regressions(vm, previous):
    before_sched = SCHED.scheduler_snapshot(vm, "Before kernel-thread regression")
    response = vm.command("schedtest", ("Scheduler self-test: PASS",))
    vm.check(MEMORY.number(vm, response, "Scheduler completed operations:", "Scheduler regression")
             == 3105, "Kernel scheduler regression retains deterministic work")
    vm.check(MEMORY.number(vm, response, "Reaped thread delta:", "Scheduler regression")
             == SCHED.STRESS_THREADS, "Kernel scheduler regression reaps every worker")
    vm.check(MEMORY.number(vm, response, "Context switch delta:", "Scheduler regression")
             >= SCHED.MINIMUM_SWITCHES, "Kernel scheduler regression still switches under load")
    vm.check(MEMORY.number(vm, response, "Timer preemption delta:", "Scheduler regression") > 0,
             "Kernel scheduler regression retains timer-driven preemption")
    after_sched = SCHED.scheduler_snapshot(vm, "After kernel-thread regression")
    vm.check(all(after_sched[key] - before_sched[key] == SCHED.STRESS_THREADS
                 for key in SCHED.LIFETIME_FIELDS),
             "Kernel scheduler regression reconciles complete worker lifetimes")
    current = SCHED.memory_snapshots(vm, "After scheduler regression")
    SCHED.check_memory_after(vm, previous, current, "Scheduler after user processes")
    previous = current
    response = vm.command("heaptest", ("Heap self-test: PASS",))
    vm.check(MEMORY.number(vm, response, "Heap stress seed:", "Heap regression", True)
             == HEAP.HEAP_STRESS_SEED, "Heap regression retains its reproducible seed")
    vm.check(MEMORY.number(vm, response, "Heap stress operations:", "Heap regression")
             >= HEAP.HEAP_STRESS_OPERATIONS, "Heap regression performs at least 8192 operations")
    current = SCHED.memory_snapshots(vm, "After heap regression")
    SCHED.check_memory_after(vm, previous, current, "Heap after user processes")
    previous = current
    vm.command("pmmtest", ("PMM self-test: PASS",))
    vm.check(SCHED.memory_snapshots(vm, "After PMM regression") == previous,
             "PMM self-test restores exact accounting after process stress")
    for repetition in range(2):
        vm.command("vmmtest", ("VMM self-test: PASS",))
        current = SCHED.memory_snapshots(vm, "VMM regression " + str(repetition + 1))
        SCHED.check_memory_after(vm, previous, current, "VMM after user processes")
        vm.check(current[0] == previous[0], "VMM self-test leaves all heap counters unchanged")
        if repetition:
            vm.check(current == previous, "Repeated VMM self-test reuses retained tables")
        previous = current
    return previous


def suite(vm, symbols):
    boot = vm.wait_for(HARNESS.PROMPT)
    for marker in ("Version: " + vm.args.version, "GDT initialized", "IDT initialized",
                   "PMM initialized", "VMM initialized", "Kernel heap initialized",
                   "Kernel scheduler initialized", "PIT timer initialized",
                   "PS/2 keyboard initialized", "Interrupts enabled", "UTAMO OS ready."):
        vm.check(marker in boot, "Boot marker: " + marker)
    vm.command("help", ("processes", "usertest", "schedtest", "heaptest", "pmmtest", "vmmtest"))
    first_system = vm.command("sysinfo", ("Ticks:", "x86_64", "Limine"))
    initial_processes = process_snapshot(vm, "Initial process state")
    if "init: controlled startup complete" in boot:
        vm.check(all(initial_processes[key] == 4 for key in LIFETIME_FIELDS)
                 and initial_processes["faults"] == 0
                 and initial_processes["syscalls"] > 0
                 and "Hello from UTAMO ring 3!" not in boot,
                 "Native init reaps its four ELF processes without running embedded probes")
    else:
        vm.check(all(initial_processes[key] == 0 for key in COUNTER_FIELDS),
                 "Normal boot without native init does not execute embedded user tests")
    initial = SCHED.memory_snapshots(vm, "Initial process baseline")
    MEMORY.hardware_memory_checks(vm, initial[2])
    architecture_checks(vm, symbols, "Initial")
    HEAP.kernel_mapping_checks(vm, symbols, "Initial kernel")
    for address in (USER_CODE, USER_DATA, USER_STACK_TOP - 1):
        MEMORY.mapping(vm, address, "Kernel CR3 excludes user VA " + hex(address),
                       expected_mapped=False)
    for command in ("processes extra", "usertest extra"):
        vm.command(command, ("Unexpected arguments.",))
    vm.check(process_snapshot(vm, "Rejected process arguments") == initial_processes,
             "Rejected process commands preserve process counters")
    vm.check(SCHED.memory_snapshots(vm, "Rejected process arguments") == initial,
             "Rejected process commands preserve allocator state")
    if vm.args.expect_nx == "on":
        previous, processes = process_stress(vm, symbols, initial, initial_processes)
    else:
        for repetition in range(USERTEST_REPETITIONS):
            response = vm.command("usertest", ("User processes unavailable: NX is required",))
            vm.check("User process self-test: PASS" not in response,
                     "Unavailable user processes never claim a successful user test")
        processes = process_snapshot(vm, "NX-off user requests")
        vm.check(processes == initial_processes,
                 "NX-off requests cannot create, fault, run syscalls or switch a user CR3")
        previous = SCHED.memory_snapshots(vm, "NX-off user requests")
        vm.check(previous == initial, "NX-off requests allocate no user memory")
    previous = regressions(vm, previous)
    vm.check(process_snapshot(vm, "After kernel regressions") == processes,
             "Kernel-only regressions preserve all process counters")
    SCHED.idle_and_gate_checks(vm, symbols, "after-processes")
    SCHED.stack_mapping_checks(vm, "Final kernel stacks")
    HEAP.kernel_mapping_checks(vm, symbols, "Final kernel")
    HEAP.heap_mapping_checks(vm, previous[0], "Final heap")
    HEAP.check_running_if(vm)
    vm.command("sleep 25", ("Sleep completed.",))
    vm.command("echo process paths remain alive", ("process paths remain alive",))
    vm.command("echo AbC 123 !?", ("AbC 123 !?",))
    vm.command("versioxx\b\bn", ("UTAMO OS " + vm.args.version,))
    vm.command("uptime", ("Uptime:", "ticks"))
    final_system = vm.command("sysinfo", ("Ticks:",))
    vm.check(MEMORY.number(vm, final_system, "Ticks:", "Final PIT")
             > MEMORY.number(vm, first_system, "Ticks:", "Initial PIT"),
             "PIT advances across process faults and kernel regressions")
    cleared = vm.command("clear")
    vm.check("\x1b[2J\x1b[H" in cleared, "Clear erases serial output and restores the prompt")
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    start = len(vm.serial())
    vm.type_text("halt\n")
    vm.wait_for("System halted.", start)
    vm.check(HARNESS.PROMPT not in vm.serial()[start:], "Halt returns no prompt")
    vm.halt_checks()
    vm.check("UTAMO OS KERNEL EXCEPTION" not in vm.serial()
             and "UTAMO KERNEL PANIC" not in vm.serial(),
             "Isolated user faults do not become a kernel exception or panic")

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
    parser.add_argument("--disk", type=HARNESS.project_path, help="Readonly fixture under build/tests")
    parser.add_argument("--expect-disk", choices=("mounted", "rejected", "absent"), default="mounted")
    parser.add_argument("--network", action="store_true", help="Enable an explicit emulated E1000")
    parser.add_argument("--network-subnet", default="10.0.2.0/24")
    parser.add_argument("--network-mac", default="52:54:00:12:34:56")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_-]{1,40}", args.name):
        parser.error("--name must contain 1-40 letters, digits, underscores or hyphens")
    if not re.fullmatch(r"[1-9][0-9]{0,4}[MG]", args.ram):
        parser.error("--ram must be an integer followed by M or G")
    if not re.fullmatch(r"[A-Za-z0-9_.,+=-]{1,120}", args.cpu):
        parser.error("--cpu must be a QEMU model/features string without whitespace")
    if not 0 < args.timeout <= 600:
        parser.error("--timeout must be in (0, 600]")
    if args.network:
        import ipaddress
        try:
            subnet = ipaddress.IPv4Network(args.network_subnet)
        except ValueError:
            parser.error("--network-subnet must be a private IPv4 /24")
        if not subnet.is_private or subnet.prefixlen != 24:
            parser.error("--network-subnet must be a private IPv4 /24")
        if not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", args.network_mac):
            parser.error("--network-mac must be six hexadecimal octets")
        raw_mac = bytes.fromhex(args.network_mac.replace(":", ""))
        if raw_mac[0] & 1 or not any(raw_mac):
            parser.error("--network-mac must be nonzero unicast")
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

    if args.disk is not None:
        if not args.disk.is_file() or not args.disk.resolve().is_relative_to(ROOT / "build/tests") or "," in str(args.disk):
            parser.error("--disk must be an existing fixture inside build/tests without commas")

    # VM is the existing shared implementation, with no graphics/probe path.
    args.probe = None
    args.probe_at = "cpu_wait_interrupt"
    args.check_timer = True  # Reuse its private GDB socket; no timer probe is armed.
    args.gdb_port = None
    args.capture_framebuffer = False
    report = {
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "mode": "process suite", "checks": [],
        "git_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "git_status": subprocess.check_output(
            ["git", "status", "--short"], cwd=ROOT, text=True),
        "iso": str(args.iso.relative_to(ROOT)), "iso_sha256": HARNESS.digest(args.iso),
        "elf_sha256": HARNESS.digest(ROOT / "build/utamo-kernel.elf"),
        "expected_version": args.version, "expected_nx": args.expect_nx,
        "usertest_repetitions": USERTEST_REPETITIONS,
        "pending_manual": ["Physical keyboard and visual framebuffer review",
                           "UEFI and physical hardware"],
        "separate_host_coverage": [
            "Private page-table rollback and physical-page ownership",
            "User copy validation, overflow and cross-page access boundaries",
            "Frame sanitization, syscall errors and process lifecycle rejection"],
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
