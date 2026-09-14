#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded QEMU/PS2 smoke tests. Run from the project in Linux/WSL.

Examples:
  python3 scripts/test-qemu.py --marker "GDT initialized" --name milestone-a
  python3 scripts/test-qemu.py --suite --name shell
  python3 scripts/test-qemu.py --fault ud2 --name exception-ud2

All modes force -display none. PS/2 modes use QMP keyboard injection
and serial evidence without opening a graphical window. Framebuffer capture is opt-in.

Only Python's standard library and the existing QEMU installation are used.
No guest serial input is injected: every character travels through QMP send-key
and the emulated PC keyboard controller. No kernel selftest runs at normal boot.
Artifacts (including failures) are kept under build/validation/<name>/.

QMP send-key and human-monitor-command:
https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html
HMP registers, PIC and framebuffer inspection:
https://www.qemu.org/docs/master/system/monitor.html
"""
import argparse
import hashlib
import fcntl
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
PROMPT = "utamo> "
KEYS = {
    " ": "spc", "\n": "ret", "\b": "backspace", "\t": "tab",
    "-": "minus", "=": "equal", "[": "bracket_left", "]": "bracket_right",
    ";": "semicolon", "'": "apostrophe", "\\": "backslash",
    ",": "comma", ".": "dot", "/": "slash", "`": "grave_accent",
}
SHIFTED = dict(zip('!@#$%^&*()_+{}:"|<>?~', '1234567890-=[];\'\\,./`'))


class CheckFailed(RuntimeError):
    pass


def project_path(path):
    """Refuse paths/symlinks escaping the only authorized project tree."""
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = ROOT / candidate
    resolved = candidate.resolve()
    if not resolved.is_relative_to(ROOT):
        raise CheckFailed("Path escapes project: " + str(candidate))
    return resolved


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


class VM:
    def __init__(self, args, report):
        self.args = args
        self.report = report
        self.directory = project_path(Path("build/validation") / args.name)
        # Separate runs never overwrite previous evidence by accident.
        if self.directory.exists():
            raise CheckFailed("Artifact directory already exists: " + str(self.directory))
        self.directory.mkdir(parents=True)
        self.serial_path = self.directory / "serial.log"
        self.socket_path = self.directory / "qmp.sock"
        self.gdb_path = self.directory / "gdb.sock"
        if len(os.fsencode(self.socket_path)) >= 104:
            raise CheckFailed("QMP socket path too long; use a shorter --name")
        self.process = None
        self.lockfile = None
        self.connection = None
        self.output = None
        self.buffer = b""
        self.command_id = 0
        self.deadline = time.monotonic() + args.timeout
        self.report["artifacts"] = str(self.directory.relative_to(ROOT))

    def check(self, condition, description, detail=None):
        record = {"check": description, "passed": bool(condition)}
        if detail is not None:
            record["detail"] = detail
        self.report["checks"].append(record)
        print(("PASS " if condition else "FAIL ") + description, flush=True)
        if not condition:
            raise CheckFailed(description + (": " + str(detail) if detail else ""))

    def remaining(self):
        value = self.deadline - time.monotonic()
        if value <= 0:
            raise CheckFailed("Overall VM timeout exceeded")
        return value

    def start(self):
        self.lockfile = (self.directory.parent / ".qemu.lock").open("a+")
        try:
            fcntl.flock(self.lockfile, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise CheckFailed("Another UTAMO QEMU harness holds the instance lock") from error
        # The lock serializes this harness; also refuse already running external
        # QEMU instances. Never terminate a process we did not create ourselves.
        for process_path in Path("/proc").iterdir():
            if not process_path.name.isdigit():
                continue
            try:
                executable = (process_path / "cmdline").read_bytes().split(b"\x00", 1)[0]
                if Path(os.fsdecode(executable)).name.startswith("qemu-system-"):
                    raise CheckFailed("Existing QEMU process " + process_path.name +
                                      "; close it before starting another VM")
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                continue
        command = [
            self.args.qemu, "-machine", "q35,accel=tcg",
            "-cpu", getattr(self.args, "cpu", "qemu64"),
            "-m", getattr(self.args, "ram", "256M"),
            "-smp", "1", "-cdrom", str(self.args.iso),
            "-boot", "d", "-display", "none", "-serial", "file:" + str(self.serial_path),
            "-qmp", "unix:" + str(self.socket_path) + ",server=on,wait=off",
            "-monitor", "none", "-nic", "none", "-no-reboot", "-no-shutdown",
        ]
        disk = getattr(self.args, "disk", None)
        if disk is not None:
            disk = Path(disk).resolve()
            if not disk.is_file() or not disk.is_relative_to(ROOT / "build/tests") or "," in str(disk):
                raise CheckFailed("Disk must be a disposable project build/tests fixture")
            command += ["-drive", "if=none,id=utamo_disk,format=raw,snapshot=on,file=" + str(disk),
                        "-device", "ide-hd,drive=utamo_disk,bus=ide.0"]
        if self.args.probe or self.args.check_timer:
            command += ["-chardev", "socket,path=" + str(self.gdb_path) +
                        ",server=on,wait=off,id=gdb0", "-gdb", "chardev:gdb0"]
            if self.args.probe:
                command += ["-S"]
        elif self.args.gdb_port:
            command += ["-gdb", "tcp:127.0.0.1:" + str(self.args.gdb_port)]
        if self.args.debug:
            command += ["-d", "int,cpu_reset,guest_errors",
                        "-D", str(self.directory / "qemu-debug.log")]
        self.report["qemu_command"] = command
        self.output = (self.directory / "qemu-stderr.log").open("wb")
        environment = os.environ.copy()
        if disk is not None:
            # ide-hd refuses a readonly block node. QEMU snapshot mode opens the
            # base readonly and directs any guest writes to an unlinked overlay.
            environment["TMPDIR"] = str(ROOT / "build/tests")
            self.report["disk_overlay_policy"] = "snapshot=on; temporary overlay in build/tests; base opened readonly"
        self.process = subprocess.Popen(
            command, cwd=ROOT, env=environment, stdin=subprocess.DEVNULL,
            stdout=self.output, stderr=subprocess.STDOUT,
        )
        self.report["qemu_pid"] = self.process.pid
        self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        while True:
            self.remaining()
            if self.process.poll() is not None:
                raise CheckFailed("QEMU exited before QMP connection; inspect qemu-stderr.log")
            try:
                self.connection.connect(str(self.socket_path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.03)
        greeting = self.receive()
        self.report["qmp_greeting"] = greeting
        self.qmp("qmp_capabilities")
        if self.args.probe:
            # Boot may now use HLT while native init waits for its children.
            # Observe the requested readiness marker before arming a later
            # pre-HLT breakpoint. Changing RIP on an already halted CPU alone
            # does not clear QEMU's internal halted state.
            self.qmp("cont")
            marker = getattr(self.args, "marker", None) or PROMPT
            self.wait_for(marker)
            self.check(True, "Readiness marker observed before fatal probe: " + marker)
            self.report["probe_after_marker"] = marker
            self.gdb("probe-arm", [
                "hbreak " + self.args.probe_at, "continue",
                "delete breakpoints", "monitor info registers",
                "set $rip = (unsigned long)&" + self.args.probe,
                "set $eflags = 2",
            ])

    def receive(self):
        while b"\n" not in self.buffer:
            self.connection.settimeout(min(3.0, self.remaining()))
            packet = self.connection.recv(65536)
            if not packet:
                raise CheckFailed("QMP socket closed")
            self.buffer += packet
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def qmp(self, name, arguments=None):
        self.command_id += 1
        request = {"execute": name, "id": self.command_id}
        if arguments is not None:
            request["arguments"] = arguments
        self.connection.sendall(json.dumps(request).encode() + b"\n")
        while True:
            message = self.receive()
            if "event" in message:
                self.report.setdefault("qmp_events", []).append(message)
            if message.get("id") == self.command_id:
                if "error" in message:
                    raise CheckFailed("QMP " + name + ": " + str(message["error"]))
                return message["return"]

    def hmp(self, command):
        return self.qmp("human-monitor-command", {"command-line": command})

    def serial(self):
        if not self.serial_path.exists():
            return ""
        return self.serial_path.read_text(errors="replace").replace("\r", "")

    def wait_for(self, marker, start=0, timeout=12.0):
        end = time.monotonic() + min(timeout, self.remaining())
        while time.monotonic() < end:
            if self.process.poll() is not None:
                raise CheckFailed("QEMU exited while waiting for " + repr(marker))
            text = self.serial()[start:]
            if marker in text:
                return text
            time.sleep(0.03)
        raise CheckFailed("Serial timeout waiting for " + repr(marker) +
                          "; last output: " + repr(self.serial()[-600:]))

    def key(self, character):
        shift = False
        if "A" <= character <= "Z":
            character = character.lower()
            shift = True
        elif character in SHIFTED:
            character = SHIFTED[character]
            shift = True
        qcode = KEYS.get(character, character)
        values = [{"type": "qcode", "data": qcode}]
        if shift:
            values.insert(0, {"type": "qcode", "data": "shift"})
        self.qmp("send-key", {"keys": values, "hold-time": 25})
        # Release is a separate emulated event. Avoid overlapping adjacent keys.
        time.sleep(0.055)

    def type_text(self, text):
        for character in text:
            self.remaining()
            self.key(character)

    def command(self, text, expected=()):
        start = len(self.serial())
        self.type_text(text + "\n")
        received = self.wait_for(PROMPT, start)
        # Ignore echoed input; a command appearing in help/input isn't proof.
        response = received.split("\n", 1)[1] if "\n" in received else ""
        response = response.rsplit(PROMPT, 1)[0]
        self.report.setdefault("commands", []).append({
            "input": text, "serial": received, "response": response,
        })
        for marker in expected:
            self.check(marker in response, repr(text) + " responds with " + repr(marker))
        return response

    def gdb(self, label, commands):
        self.qmp("stop")
        command = [
            "gdb", "--batch", "--nx", str(ROOT / "build/utamo-kernel.elf"),
            "-ex", "set pagination off", "-ex", "set confirm off",
            "-ex", "target remote " + str(self.gdb_path),
        ]
        for item in commands + ["detach"]:
            command += ["-ex", item]
        try:
            result = subprocess.run(command, cwd=ROOT, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=min(12.0, self.remaining()))
        except subprocess.TimeoutExpired as error:
            partial = error.stdout or ""
            if isinstance(partial, bytes):
                partial = partial.decode(errors="replace")
            (self.directory / (label + "-gdb.log")).write_text(partial + "\nGDB TIMEOUT\n")
            raise
        (self.directory / (label + "-gdb.log")).write_text(result.stdout)
        self.check(result.returncode == 0, "GDB operation: " + label,
                   {"returncode": result.returncode, "output": result.stdout})
        self.qmp("cont")
        return result.stdout

    def probe(self, symbol):
        """Validate the debugger-only fault armed at a pre-HLT breakpoint."""
        text = self.wait_for("UTAMO OS KERNEL EXCEPTION")
        start = text.index("UTAMO OS KERNEL EXCEPTION")
        text = self.wait_for("System halted", start)
        kind = "ud2" if "ud2" in symbol else ("div0" if "div0" in symbol else "pf")
        check_exception(self, text, kind)
        self.halt_checks()
        if self.args.capture_framebuffer:
            self.screenshot("probe")

    def timer_checks(self):
        values = []
        for index in range(2):
            if index:
                time.sleep(0.6)
            output = self.gdb("timer-" + str(index), [
                'printf "UTAMO_TIMER_TICKS=%llu\\n", (unsigned long long)' +
                self.args.timer_symbol,
            ])
            match = re.search(r"UTAMO_TIMER_TICKS=(\d+)", output)
            self.check(bool(match), "GDB observes real PIT tick counter sample " + str(index))
            values.append(int(match.group(1)))
        self.check(values[1] > values[0], "PIT ticks increase while the VM runs",
                   {"before": values[0], "after": values[1], "delay_seconds": 0.6})
        (self.directory / "pic.txt").write_text(self.hmp("info pic"))
        (self.directory / "irq.txt").write_text(self.hmp("info irq"))

    def registers(self, label):
        registers = self.hmp("info registers")
        (self.directory / (label + "-registers.txt")).write_text(registers)
        return registers

    def screenshot(self, label):
        path = self.directory / (label + ".ppm")
        self.qmp("screendump", {"filename": str(path)})
        self.check(path.exists() and path.stat().st_size > 100,
                   label + " framebuffer capture saved")
        return path

    def descriptor_checks(self, registers, idt=True):
        match = re.search(r"CS\s*=\s*([0-9a-fA-F]+)", registers)
        self.check(bool(match) and int(match.group(1), 16) == 8,
                   "Loaded kernel CS is selector 0x08")
        match = re.search(r"GDT=\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", registers)
        self.check(bool(match) and int(match.group(1), 16) >= 0xffffffff80000000
                   and int(match.group(2), 16) >= 23, "Own higher-half GDT loaded",
                   match.group(0) if match else registers)
        if idt:
            match = re.search(r"IDT=\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", registers)
            self.check(bool(match) and int(match.group(1), 16) >= 0xffffffff80000000
                       and int(match.group(2), 16) == 4095, "256-entry IDT loaded",
                       match.group(0) if match else registers)

    def halt_checks(self):
        time.sleep(0.2)
        first = self.registers("halt-first")
        time.sleep(0.2)
        second = self.registers("halt-second")
        flags = re.search(r"RFL=([0-9a-fA-F]+)", second)
        self.check(bool(flags) and int(flags.group(1), 16) & 0x200 == 0,
                   "Halted CPU has interrupts disabled")
        first_rip = re.search(r"RIP=([0-9a-fA-F]+)", first)
        second_rip = re.search(r"RIP=([0-9a-fA-F]+)", second)
        self.check(bool(first_rip) and bool(second_rip) and
                   first_rip.group(1) == second_rip.group(1),
                   "Halted CPU RIP is stable across observations")
        self.check("HLT=1" in second, "QEMU reports CPU halted (HLT=1)")

    def close(self):
        if self.process is not None:
            if self.process.poll() is None:
                try:
                    if self.connection is not None:
                        self.qmp("quit")
                except (OSError, ValueError, CheckFailed):
                    pass
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=3)
            self.report["qemu_exit_code"] = self.process.returncode
            self.report["qemu_process_reaped"] = self.process.poll() is not None
        if self.connection is not None:
            self.connection.close()
        if self.output is not None:
            self.output.close()
        if self.lockfile is not None:
            self.lockfile.close()
        for path in (self.socket_path, self.gdb_path):
            if path.exists():
                path.unlink()


def framebuffer_clear_check(vm, path):
    # QMP screendump defaults to binary RGB PPM. Permit comments in the header.
    content = path.read_bytes()
    position = 0
    tokens = []
    while len(tokens) < 4:
        while content[position:position + 1].isspace():
            position += 1
        if content[position:position + 1] == b"#":
            position = content.index(b"\n", position) + 1
            continue
        end = position
        while end < len(content) and not content[end:end + 1].isspace():
            end += 1
        tokens.append(content[position:end])
        position = end
    vm.check(tokens[0] == b"P6" and tokens[3] == b"255",
             "Clear capture has supported RGB PPM format")
    # One whitespace byte terminates a P6 header; do not consume RGB whitespace.
    position += 1
    width, height = int(tokens[1]), int(tokens[2])
    pixels = content[position:]
    vm.check(len(pixels) == width * height * 3 and height > 32,
             "Framebuffer capture dimensions and byte count are valid")
    background = pixels[-3:]
    body = pixels[width * 32 * 3:]
    vm.check(body == background * (width * (height - 32)),
             "Clear removes all framebuffer text below the prompt row")
    top = pixels[:width * 32 * 3]
    vm.check(top != background * (width * 32),
             "Prompt remains visible after framebuffer clear")


def suite(vm):
    boot = vm.wait_for(PROMPT)
    for marker in ("UTAMO OS", "Version: " + vm.args.version, "GDT initialized",
                   "IDT initialized", "PIC initialized", "PIT timer initialized",
                   "PS/2 keyboard initialized", "Interrupts enabled", "UTAMO OS ready."):
        vm.check(marker in boot, "Boot marker: " + marker)
    vm.descriptor_checks(vm.registers("boot"))
    if vm.args.capture_framebuffer:
        vm.screenshot("boot")
    vm.command("help", ("help", "clear", "version", "sysinfo", "mem",
                        "uptime", "echo", "halt", "fault"))
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    first = vm.command("sysinfo", ("x86_64", "Limine", "100 Hz", "Ticks:"))
    vm.command("mem", ("MiB",))
    uptime = vm.command("uptime")
    vm.check(bool(re.search(r"\d", uptime)), "Uptime command reports a numeric duration")
    vm.command("echo hello world", ("hello world",))
    vm.command("echo AbC 123 !?", ("AbC 123 !?",))
    vm.command("versioxx\b\bn", ("UTAMO OS " + vm.args.version,))
    # Delay between observations is bounded and runs with the guest alive.
    time.sleep(1.1)
    second = vm.command("sysinfo", ("Ticks:",))
    old = re.search(r"Ticks:\s*(\d+)", first)
    new = re.search(r"Ticks:\s*(\d+)", second)
    vm.check(bool(old) and bool(new) and int(new.group(1)) > int(old.group(1)),
             "PIT ticks increase across shell interaction",
             {"before": old.group(1) if old else first,
              "after": new.group(1) if new else second})
    (vm.directory / "pic.txt").write_text(vm.hmp("info pic"))
    (vm.directory / "irq.txt").write_text(vm.hmp("info irq"))
    vm.command("clear")
    if vm.args.capture_framebuffer:
        framebuffer_clear_check(vm, vm.screenshot("clear"))
    else:
        vm.report.setdefault("pending_manual", []).append("clear framebuffer appearance")
    vm.command("version", ("UTAMO OS " + vm.args.version,))
    start = len(vm.serial())
    vm.type_text("halt\n")
    vm.wait_for("System halted", start)
    vm.check(PROMPT not in vm.serial()[start:], "Halt does not return a shell prompt")
    vm.halt_checks()
    if vm.args.capture_framebuffer:
        vm.screenshot("halt")


def fault(vm, kind):
    vm.wait_for(PROMPT)
    start = len(vm.serial())
    vm.type_text("fault " + kind + "\n")
    text = vm.wait_for("System halted", start)
    check_exception(vm, text, kind)
    vm.halt_checks()
    if vm.args.capture_framebuffer:
        vm.screenshot("exception-" + kind)


def check_exception(vm, text, kind):
    expected_vector, expected_name = {
        "ud2": (6, "Invalid Opcode"), "div0": (0, "Divide Error"),
        "pf": (14, "Page Fault"),
    }[kind]
    vm.check("UTAMO OS KERNEL EXCEPTION" in text, "Kernel exception banner observed")
    vm.check(expected_name in text, "Expected CPU exception name: " + expected_name)
    vector = re.search(r"Vector:\s*(\d+)", text)
    vm.check(bool(vector) and int(vector.group(1)) == expected_vector,
             "Expected CPU exception vector " + str(expected_vector))
    for register in ("RIP", "CS", "SS", "RFLAGS", "RSP", "RAX", "RBX", "RCX", "RDX",
                     "RSI", "RDI", "RBP", "R8", "R9", "R10", "R11", "R12",
                     "R13", "R14", "R15"):
        vm.check(bool(re.search(r"\b" + register + r":\s*0x[0-9a-fA-F]+", text)),
                 "Exception context includes " + register)
    vm.check(bool(re.search(r"Error(?: code)?:\s*0x[0-9a-fA-F]+", text)),
             "Exception error code observed")
    error = re.search(r"Error(?: code)?:\s*0x([0-9a-fA-F]+)", text)
    expected_error = 2 if kind == "pf" else 0
    vm.check(bool(error) and int(error.group(1), 16) == expected_error,
             "Expected normalized CPU error code " + hex(expected_error))
    for register, expected in (("CS", 8), ("SS", 16)):
        match = re.search(r"\b" + register + r":\s*0x([0-9a-fA-F]+)", text)
        vm.check(bool(match) and int(match.group(1), 16) == expected,
                 "Exception frame " + register + " has kernel selector " + hex(expected))
    rip = re.search(r"\bRIP:\s*0x([0-9a-fA-F]+)", text)
    vm.check(bool(rip) and 0xffffffff80000000 <= int(rip.group(1), 16) < (1 << 64),
             "Exception RIP is in the canonical kernel image range")
    rsp = re.search(r"\bRSP:\s*0x([0-9a-fA-F]+)", text)
    symbols = {}
    output = subprocess.check_output(
        [str(ROOT / "toolchain/prefix/bin/x86_64-elf-nm"), "-n",
         str(ROOT / "build/utamo-kernel.elf")], text=True)
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    stack_ok = False
    if rsp:
        address = int(rsp.group(1), 16)
        stack_ok = (symbols["bootstrap_stack_bottom"] <= address <=
                    symbols["bootstrap_stack_top"])
        # v0.4 owns 64 guarded stacks, each 4 KiB guard + 64 KiB payload.
        # Earlier exception probes ran only on the ELF bootstrap stack.
        if "scheduler_on_interrupt" in symbols:
            base = 0xffffc00040000000
            stack_ok = stack_ok or any(
                base + slot * 69632 + 4096 <= address <= base + (slot + 1) * 69632
                for slot in range(64))
    vm.check(stack_ok, "Exception RSP belongs to bootstrap or a guarded thread stack")
    if kind == "pf":
        address = re.search(r"(?:Fault )?address:\s*0x([0-9a-fA-F]+)", text, re.I)
        vm.check(bool(address) and int(address.group(1), 16) == 0x00007ffffffff000,
                 "CR2 identifies the deliberate unmapped probe address")
        for expected in ("Present: No", "Access: Write", "Mode: Supervisor",
                         "Reserved bit violation: No", "Instruction fetch: No"):
            vm.check(expected in text, "Page fault flags decode to " + expected)
        for marker in ("Address:", "Present:", "Access:", "Mode:",
                       "Reserved", "Instruction fetch:"):
            vm.check(marker.lower() in text.lower(), "Page fault diagnostic: " + marker)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--marker", help="Wait for an incremental boot marker")
    mode.add_argument("--suite", action="store_true", help="Exercise normal PS/2 shell")
    mode.add_argument("--fault", choices=("ud2", "div0", "pf"))
    parser.add_argument("--name", required=True, help="Unique artifact directory name")
    parser.add_argument("--iso", type=project_path)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--version", help="Expected kernel version; defaults to version.h")
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--debug", action="store_true", help="Save QEMU interrupt/reset log")
    parser.add_argument("--gdb-port", type=int, help="Optional localhost-only GDB listener")
    parser.add_argument("--probe", help="After --marker, redirect RIP to a known fault probe ELF symbol")
    parser.add_argument("--probe-at", default="cpu_halt",
                        help="Hardware breakpoint before first HLT (default: cpu_halt)")
    parser.add_argument("--capture-framebuffer", action="store_true",
                        help="Opt in to headless framebuffer captures and visual checks")
    parser.add_argument("--check-timer", action="store_true",
                        help="Read PIT ticks twice through GDB, while VM runs between samples")
    parser.add_argument("--timer-symbol", default="ticks",
                        help="Real counter symbol used by --check-timer (default: ticks)")
    parser.add_argument("--hold", type=float, default=0.0,
                        help="After marker, allow a bounded external GDB inspection")
    parser.add_argument("--check-gdt", action="store_true")
    parser.add_argument("--check-idt", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_-]{1,40}", args.name):
        parser.error("--name must contain 1-40 letters, digits, underscores or hyphens")
    if not 0 < args.timeout <= 600 or not 0 <= args.hold <= 300:
        parser.error("Use timeout in (0, 600] and hold in [0, 300]")
    if args.probe and (not args.marker or args.gdb_port is not None or
                       not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.probe) or
                       not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.probe_at) or
                       not any(part in args.probe for part in ("ud2", "div0", "page"))):
        parser.error("--probe needs --marker, a ud2/div0/page ELF identifier, and no --gdb-port")
    if args.check_timer and (not args.marker or args.gdb_port is not None or
                            not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.timer_symbol)):
        parser.error("--check-timer needs --marker, a counter ELF identifier, and no --gdb-port")
    if args.gdb_port is not None and not 1024 <= args.gdb_port <= 65535:
        parser.error("GDB port must be between 1024 and 65535")
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
        args.iso = project_path(candidates[0])
    if not args.iso.is_file():
        parser.error("ISO does not exist: " + str(args.iso))
    report = {
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "mode": args.marker or ("suite" if args.suite else "fault " + args.fault),
        "git_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "git_status": subprocess.check_output(
            ["git", "status", "--short"], cwd=ROOT, text=True),
        "iso": str(args.iso.relative_to(ROOT)), "iso_sha256": digest(args.iso),
        "elf_sha256": digest(ROOT / "build/utamo-kernel.elf"), "checks": [],
        "expected_version": args.version,
        "pending_manual": ["PS/2 physical typing and visual framebuffer review"]
                          if not args.suite else [],
    }
    vm = None
    result = 1
    try:
        vm = VM(args, report)
        vm.start()
        if args.suite:
            suite(vm)
        elif args.fault:
            fault(vm, args.fault)
        else:
            vm.wait_for(args.marker)
            vm.check(True, "Incremental boot marker: " + args.marker)
            print("Marker reached; QEMU inspection ready.", flush=True)
            if args.hold:
                end = time.monotonic() + args.hold
                while time.monotonic() < end:
                    vm.remaining()
                    time.sleep(max(0.0, min(0.2, end - time.monotonic())))
            registers = vm.registers("marker")
            if args.check_gdt or args.check_idt:
                vm.descriptor_checks(registers, idt=args.check_idt)
            if args.capture_framebuffer:
                vm.screenshot("marker")
            if args.check_timer:
                vm.timer_checks()
            if args.probe:
                vm.probe(args.probe)
        report["passed"] = True
        result = 0
    except (CheckFailed, OSError, ValueError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        report["passed"] = False
        report["error"] = str(error)
        print("FAIL " + str(error), file=sys.stderr, flush=True)
    finally:
        if vm is not None:
            vm.close()
            report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            report["check_count"] = len(report["checks"])
            (vm.directory / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            print("Evidence: " + str(vm.directory.relative_to(ROOT)), flush=True)
    return result


if __name__ == "__main__":
    sys.exit(main())
