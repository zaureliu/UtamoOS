#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""VFS, initramfs and real ELF processes through the existing headless VM owner."""
import importlib.util
import re
import subprocess
import time
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("utamo_process_gate", ROOT / "scripts/test-process-qemu.py")
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)
H = P.HARNESS

def observe_elf(vm, kernel_root):
    vm.qmp("stop")
    log = vm.directory / "elf-cpl3-gdb.log"
    command = ["gdb", "--batch", "--nx", str(ROOT / "build/utamo-kernel.elf"),
               "-ex", "set pagination off", "-ex", "set confirm off",
               "-ex", "target remote " + str(vm.gdb_path)]
    for item in ["hbreak *0x400000", 'printf "ELF_BREAK_ARMED\\n"', "continue",
                 "info registers cs ss rip rsp cr3", "monitor info registers", "detach"]:
        command += ["-ex", item]
    debugger = None
    start = len(vm.serial())
    try:
        with log.open("w") as stream:
            debugger = subprocess.Popen(command, cwd=ROOT, text=True, stdout=stream,
                                        stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
            deadline = time.monotonic() + min(12, vm.remaining())
            while "ELF_BREAK_ARMED" not in log.read_text():
                if debugger.poll() is not None or time.monotonic() >= deadline:
                    raise H.CheckFailed("ELF breakpoint was not armed")
                time.sleep(0.02)
            vm.type_text("exec /bin/hello\n")
            debugger.wait(timeout=min(15, vm.remaining()))
        text = log.read_text()
        vm.check(debugger.returncode == 0, "ELF observer completes")
        values = {}
        for name in ("cs", "ss", "rip", "rsp", "cr3"):
            match = re.search(r"^" + name + r"\s+0x([0-9a-fA-F]+)", text, re.M)
            vm.check(bool(match), "ELF hardware register " + name)
            values[name] = int(match[1], 16)
        vm.check(values["cs"] == 0x1b and values["ss"] == 0x23,
                 "Separate /bin/hello ELF executes at CPL3")
        vm.check(values["rip"] == 0x400000 and values["rsp"] == 0x6ffffef0,
                 "ELF entry and native aligned stack match the loader ABI")
        vm.check(values["cr3"] != kernel_root and values["cr3"] % 4096 == 0,
                 "ELF uses its private page-table root")
        vm.report["elf_cpl3"] = values
        vm.qmp("cont")
        text = vm.wait_for(H.PROMPT, start)
        vm.check("Hello from UTAMO userspace!" in text and "Exec: PASS" in text,
                 "Observed CPL3 ELF calls WRITE and exits successfully")
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
        if vm.process.poll() is None:
            vm.qmp("cont")

def suite(vm, symbols):
    vm.report["mode"] = "VFS/initramfs/ELF suite"
    vm.report["separate_host_coverage"] = [
        "newc bounds, bad paths, duplicate nodes, deterministic malformed archives",
        "ELF bounds, permissions, overlap and failure-injected VM rollback",
        "Native file syscall descriptors, copy failure and offset rollback"]
    vm.report["initramfs_sha256"] = H.digest(ROOT / "build/initramfs.cpio")
    vm.report["user_elf_sha256"] = {p.name: H.digest(p)
        for p in (ROOT / "build/userspace").iterdir() if p.is_file() and p.suffix == ""}
    boot = vm.wait_for(H.PROMPT)
    vm.check("VFS initramfs mounted:" in boot and "UTAMO OS ready." in boot,
             "Required boot module mounts and the kernel reaches its shell")
    vm.check("Version: " + vm.args.version in boot, "Candidate keeps the last GREEN version")
    if vm.args.expect_nx == "on":
        for marker in ("init: PID 1", "Hello from UTAMO userspace!",
                       "echo from an ELF process", "UTAMO native userspace: x86_64",
                       "init: controlled startup complete"):
            vm.check(marker in boot, "Native startup: " + marker)
    else:
        vm.check("init: PID 1" not in boot and "Ring 3 unavailable" in boot,
                 "NX-off configuration preserves the kernel and does not execute users")
    vm.command("ls", ("/bin", "/dev", "/etc"))
    vm.command("ls /bin", ("/bin/init", "/bin/hello", "/bin/echo", "/bin/sysinfo"))
    vm.command("cat /etc/motd", ("UTAMO native VFS and initramfs",))
    vm.command("cat /missing", ("File unavailable.",))
    vm.command("ls /etc/../bin", ("Directory unavailable.",))
    vm.command("ls /etc/motd", ("Directory unavailable.",))
    vm.command("cat /etc/empty")
    baseline = P.SCHED.memory_snapshots(vm, "VFS baseline")
    if vm.args.expect_nx == "on":
        observe_elf(vm, baseline[2]["root"])
        vm.command("exec /bin/echo native argument", ("native argument", "Exec: PASS"))
        vm.command("exec /bin/sysinfo", ("UTAMO native userspace: x86_64", "Exec: PASS"))
        vm.command("exec /etc/not-elf", ("Exec: FAIL",))
        vm.command("exec /missing", ("Exec: FAIL",))
        steady = None
        for repetition in range(3):
            before = P.process_snapshot(vm, "Before ELF stress")
            text = vm.command("fstest", ("Filesystem self-test: PASS",
                "exact PMM/heap restoration: PASS"))
            vm.check("FAIL" not in text and text.count("filetest: PASS") == 9
                     and text.count("badptr: PASS") == 9,
                     "All userspace VFS, BSS, hostile-pointer and FD-capacity checks pass")
            after = P.process_snapshot(vm, "After ELF stress")
            vm.check(after["created"] - before["created"] == 35
                     and after["reaped"] - before["reaped"] == 35
                     and after["faults"] == before["faults"],
                     "Each ELF stress run creates and reaps all 35 processes without faults")
            current = P.SCHED.memory_snapshots(vm, "ELF stress " + str(repetition))
            if steady is not None:
                vm.check(all(current[0][key] == steady[0][key] for key in P.HEAP.STATE_FIELDS)
                         and current[1:] == steady[1:],
                         "Repeated ELF creation restores exact allocator accounting")
            steady = current
            vm.report.setdefault("elf_stress", []).append(
                {"rounds": 8, "created_reaped": 35, "filetest": 9, "badptr": 9})
        vm.command("echo kernel survived ELF stress", ("kernel survived ELF stress",))
    else:
        vm.command("exec /bin/hello", ("Exec: FAIL",))
        vm.check(P.SCHED.memory_snapshots(vm, "NX-off ELF refusal") == baseline,
                 "NX-off exec rejection allocates no private memory")
    vm.command("heap", ("Heap integrity: OK",))
    vm.command("schedulerstats", ("Scheduler integrity: OK",))
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    vm.check("UTAMO OS KERNEL EXCEPTION" not in vm.serial() and
             "UTAMO KERNEL PANIC" not in vm.serial(), "No kernel exception or panic")
    start = len(vm.serial())
    vm.type_text("halt\n")
    vm.wait_for("System halted.", start)
    vm.halt_checks()

if __name__ == "__main__":
    P.suite = suite
    raise SystemExit(P.main())
