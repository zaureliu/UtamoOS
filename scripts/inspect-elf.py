#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check ELF64 load, interrupt-table and emitted SysV ABI contracts.

Only Python's standard library is needed. This inspects the actual linked
bytes; it does not execute privileged code or infer success from C sources.
"""
from pathlib import Path
import struct
import sys

path = Path(sys.argv[1] if len(sys.argv) > 1 else "build/utamo-kernel.elf")
data = path.read_bytes()
checks = 0


def check(value, label):
    global checks
    checks += 1
    if not value:
        raise SystemExit("ELF FAIL: " + label)


check(len(data) >= 64, "complete ELF header")
check(data[:7] == b"\x7fELF\x02\x01\x01", "ELF64 little endian")
header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
_, kind, machine, _, entry, phoff, shoff, _, ehsize, phsize, phnum, shsize, shnum, shstrndx = header
check(kind == 2 and machine == 62 and ehsize == 64, "static AMD64 EXEC")
check(phsize == 56 and shsize == 64, "ELF table entry sizes")
check(phoff + phsize * phnum <= len(data), "program headers bounded")
check(shoff + shsize * shnum <= len(data), "section headers bounded")
loads = []
file_loads = []
stack = []
for index in range(phnum):
    ptype, flags, offset, vaddr, _, filesz, memsz, align = struct.unpack_from(
        "<IIQQQQQQ", data, phoff + index * phsize)
    check(ptype not in (2, 3), "no DYNAMIC/INTERP")
    if ptype == 1:
        check(vaddr >= 0xffffffff80000000, "higher-half LOAD")
        check(flags & 3 != 3, "no writable executable LOAD")
        check(filesz <= memsz and offset + filesz <= len(data), "LOAD bounds")
        check(align == 4096 and vaddr % align == offset % align,
              "page aligned LOAD")
        loads.append((vaddr, vaddr + memsz, flags))
        file_loads.append((vaddr, vaddr + filesz, offset, flags))
    if ptype == 0x6474e551:
        stack.append(flags)
check(len(loads) == 4, "requests/text/rodata/data LOADs")
check(stack == [6], "non-executable stack")
check(any(start <= entry < end and flags & 1 for start, end, flags in loads),
      "entry in executable LOAD")
sections = [
    struct.unpack_from("<IIQQQQIIQQ", data, shoff + index * shsize)
    for index in range(shnum)
]
check(shstrndx < shnum, "section names table")
strings = sections[shstrndx]
check(strings[4] + strings[5] <= len(data), "section names bounded")
names = data[strings[4]:strings[4] + strings[5]]
request_found = False
symbols = []
named_symbols = {}
symbol_bindings = {}
named_sections = {}


def string_at(table, offset):
    if not 0 <= offset < len(table):
        raise SystemExit("ELF FAIL: string offset out of bounds")
    end = table.find(b"\0", offset)
    if end < 0:
        raise SystemExit("ELF FAIL: unterminated string")
    return table[offset:end].decode("ascii")


for section in sections:
    name_offset, stype, flags, address, offset, size, link, _, _, entsize = section
    name = string_at(names, name_offset)
    named_sections[name] = section
    if stype != 8:  # SHT_NOBITS has no corresponding bytes in the file.
        check(offset + size <= len(data), "section file bounds: " + name)
    if flags & 2:
        check(any(start <= address and address + size <= end
                  for start, end, _ in loads),
              "allocated section mapped: " + name)
        check(stype not in (4, 9), "no runtime relocations")
    if name == ".limine_requests":
        request_found = True
        check(size >= 32 and flags & 3 == 3, "writable retained Limine requests")
    if name.startswith(".debug"):
        check(flags & 2 == 0, "DWARF non-ALLOC")
    if stype == 2:
        check(entsize == 24 and link < shnum and size % 24 == 0,
              "symbol table layout")
        symbol_strings = sections[link]
        check(symbol_strings[1] == 3 and
              symbol_strings[4] + symbol_strings[5] <= len(data),
              "symbol string table layout")
        symbol_names = data[symbol_strings[4]:
                            symbol_strings[4] + symbol_strings[5]]
        for symbol_offset in range(offset + 24, offset + size, 24):
            symbol = struct.unpack_from("<IBBHQQ", data, symbol_offset)
            symbols.append(symbol)
            check(symbol[3] != 0, "no undefined symbols")
            symbol_name = string_at(symbol_names, symbol[0])
            if symbol_name:
                named_symbols[symbol_name] = symbol[4]
                symbol_bindings[symbol_name] = symbol[1] >> 4
check(request_found and len(symbols) > 0, "requests and symbols present")


def symbol_address(name):
    if name not in named_symbols:
        raise SystemExit("ELF FAIL: missing symbol " + name)
    return named_symbols[name]


def executable_bytes(address, size):
    for start, end, offset, flags in file_loads:
        if start <= address and address + size <= end and flags & 1:
            begin = offset + address - start
            return data[begin:begin + size]
    raise SystemExit(f"ELF FAIL: executable bytes unavailable at {address:#x}")


check(entry == symbol_address("_start"), "entry matches _start")
check(".text" in named_sections, "text section exists")
text_section = named_sections[".text"]
check(text_section[2] & 7 == 6, "text is allocated executable and read-only")

# The table uses signed 32-bit offsets from its own base, not from each slot.
# Same-section relative offsets avoid NASM 3.01 absolute relocation warnings.
table = symbol_address("interrupt_stub_table")
check(table % 4 == 0 and text_section[3] <= table and
      table + 256 * 4 <= text_section[3] + text_section[5],
      "entire relative interrupt table in text")
table_data = executable_bytes(table, 256 * 4)
common = symbol_address("common_entry")
error_vectors = {8, 10, 11, 12, 13, 14, 17, 21, 29, 30}
stub_checks_start = checks


def pushed_constant(value):
    # PUSH imm8 sign-extends; vectors 128..255 must use positive imm32.
    if value < 128:
        return b"\x6a" + struct.pack("<b", value)
    return b"\x68" + struct.pack("<i", value)


for vector in range(256):
    relative = struct.unpack_from("<i", table_data, vector * 4)[0]
    target = (table + relative) & ((1 << 64) - 1)
    check(target == symbol_address(f"interrupt_stub_{vector}"),
          f"vector {vector}: relative table resolves to own stub")
    prefix = (b"" if vector in error_vectors else pushed_constant(0))
    prefix += pushed_constant(vector)
    code = executable_bytes(target, len(prefix) + 5)
    check(code[:len(prefix)] == prefix,
          f"vector {vector}: vector and CPU/synthetic error slots")
    branch = code[len(prefix):]
    branch_address = target + len(prefix)
    if branch[0] == 0xe9:
        destination = branch_address + 5 + struct.unpack_from("<i", branch, 1)[0]
    elif branch[0] == 0xeb:
        destination = branch_address + 2 + struct.unpack_from("<b", branch, 1)[0]
    else:
        raise SystemExit(f"ELF FAIL: vector {vector}: not a relative JMP")
    check(destination == common,
          f"vector {vector}: jump reaches common entry")
stub_checks = checks - stub_checks_start

# The sequence below is the actual ABI contract of interrupt_frame. Separate
# assertions give useful diagnostics if a future edit mismatches offsets,
# register order, the ABI call alignment or the five-slot hardware return.
# The dispatcher returns the selected frame in RAX. Its stack may differ from
# the interrupted thread's stack; a callee-saved anchor must not override it.
abi_checks_start = checks
cursor = common


def expect_bytes(encoded, label):
    global cursor
    expected = bytes.fromhex(encoded)
    check(executable_bytes(cursor, len(expected)) == expected, label)
    cursor += len(expected)


register_pushes = [
    ("rax", "50"), ("rbx", "53"), ("rcx", "51"), ("rdx", "52"),
    ("rsi", "56"), ("rdi", "57"), ("rbp", "55"),
    ("r8", "41 50"), ("r9", "41 51"), ("r10", "41 52"),
    ("r11", "41 53"), ("r12", "41 54"), ("r13", "41 55"),
    ("r14", "41 56"), ("r15", "41 57"),
]
for register, encoded in register_pushes:
    expect_bytes(encoded, "common: saves " + register)
expect_bytes("fc", "common: clears DF before C")
expect_bytes("48 89 e7", "common: RDI points to saved frame")
expect_bytes("48 83 e4 f0", "common: RSP aligned to 16 bytes before CALL")
call = executable_bytes(cursor, 5)
check(call[0] == 0xe8, "common: near relative CALL")
check(cursor + 5 + struct.unpack_from("<i", call, 1)[0] ==
      symbol_address("interrupt_dispatch"), "common: C dispatcher target")
cursor += 5
expect_bytes("48 89 c4", "common: adopts dispatcher-selected frame from RAX")
register_pops = [
    ("r15", "41 5f"), ("r14", "41 5e"), ("r13", "41 5d"),
    ("r12", "41 5c"), ("r11", "41 5b"), ("r10", "41 5a"),
    ("r9", "41 59"), ("r8", "41 58"),
    ("rbp", "5d"), ("rdi", "5f"), ("rsi", "5e"), ("rdx", "5a"),
    ("rcx", "59"), ("rbx", "5b"), ("rax", "58"),
]
for register, encoded in register_pops:
    expect_bytes(encoded, "common: restores " + register)
expect_bytes("48 83 c4 10", "common: discards vector/error slots only")
expect_bytes("48 cf", "common: 64-bit IRETQ restores hardware frame")
check(executable_bytes(symbol_address("idt_load"), 4) ==
      bytes.fromhex("0f 01 1f c3"), "IDT loader executes LIDT [RDI], RET")
# INT imm8 uses an unsigned vector byte, unlike the sign-extended PUSH imm8
# checked above. Vector 240 is a kernel scheduling trap, outside PIC 32..47.
# The corresponding stub is already checked among all 256 vectors. Gate DPL0
# is covered by the IDT encoder host checks and live IDT validation.
yield_vector = 240
check(yield_vector not in error_vectors and yield_vector >= 48,
      "yield vector has a synthetic error slot and does not overlap PIC")
check(executable_bytes(symbol_address("thread_yield_trap"), 3) ==
      bytes((0xcd, yield_vector, 0xc3)),
      "kernel yield trap emits INT 240, RET")
# The bootstrap thread adopts the boot stack; scheduler ownership must refer
# to the same entire static span that _start actually installs in RSP.
bootstrap_bottom = symbol_address("bootstrap_stack_bottom")
bootstrap_top = symbol_address("bootstrap_stack_top")
for name in ("bootstrap_stack_bottom", "bootstrap_stack_top"):
    check(symbol_bindings.get(name) == 1, "exported bootstrap stack bound: " + name)
check(bootstrap_top - bootstrap_bottom == 65536,
      "bootstrap stack retains its full 64 KiB extent")
check(bootstrap_bottom % 16 == 0 and bootstrap_top % 16 == 0,
      "bootstrap stack bounds preserve SysV alignment")
bss = named_sections[".bss"]
check(bss[1] == 8 and bss[3] <= bootstrap_bottom < bootstrap_top <= bss[3] + bss[5],
      "bootstrap stack is entirely zero-filled BSS")
check(any(start <= bootstrap_bottom < bootstrap_top <= end and flags == 6
          for start, end, flags in loads),
      "bootstrap stack is in writable non-executable LOAD")
boot_entry = executable_bytes(entry, 13)
check(boot_entry[:5] == bytes.fromhex("fa fc 48 8d 25"),
      "boot disables interrupts, clears DF and loads RSP with RIP-relative LEA")
check(entry + 9 + struct.unpack_from("<i", boot_entry, 5)[0] == bootstrap_top,
      "boot installs the exported bootstrap stack top")
check(boot_entry[9:13] == bytes.fromhex("48 83 e4 f0"),
      "boot aligns the adopted stack before entering C")
abi_checks = checks - abi_checks_start

# Memory v0.2 retains four LOADs and the 176-byte interrupt frame layout.
memory_checks_start = checks
for name in ("__kernel_start", "__kernel_end", "__text_start", "__text_end",
             "__rodata_start", "__rodata_end", "__data_start", "__data_end",
             "__bss_start", "__bss_end"):
    address = symbol_address(name)
    check(address >= 0xffffffff80000000, "higher-half linker symbol " + name)
    if name != "__bss_end":
        check(address % 4096 == 0, "page-aligned linker boundary " + name)
ordered = ["__kernel_start", "__text_start", "__text_end", "__rodata_start",
           "__rodata_end", "__data_start", "__data_end", "__bss_start",
           "__bss_end", "__kernel_end"]
check(all(symbol_address(a) <= symbol_address(b)
          for a, b in zip(ordered, ordered[1:])), "ordered memory section boundaries")
for section_name, symbol_name in ((".text", "__text_start"),
                                 (".rodata", "__rodata_start"),
                                 (".data", "__data_start"), (".bss", "__bss_start")):
    check(named_sections[section_name][3] == symbol_address(symbol_name),
          "section starts at explicit symbol: " + section_name)
check(named_sections[".bss"][1] == 8, "BSS remains zero-fill NOBITS")
check(not any(name.startswith(("__atomic_", "__sync_"))
              for name in named_symbols), "64-bit atomics need no runtime helpers")
for name, encoded in (
        ("cpu_read_cr3", "0f 20 d8 c3"), ("cpu_write_cr3", "0f 22 df c3"),
        ("cpu_read_cr4", "0f 20 e0 c3"), ("cpu_invlpg", "0f 01 3f c3")):
    expected = bytes.fromhex(encoded)
    check(executable_bytes(symbol_address(name), len(expected)) == expected,
          "emitted paging instruction: " + name)
cpuid = executable_bytes(symbol_address("cpu_cpuid"),
                         symbol_address("cpu_read_cr0") - symbol_address("cpu_cpuid"))
check(cpuid.startswith(b"\x53") and cpuid.endswith(b"\x5b\xc3") and b"\x0f\xa2" in cpuid,
      "CPUID wrapper preserves SysV callee-saved RBX")
for name in ("pmm_alloc_page", "pmm_free_page", "pmm_pin_page",
             "vmm_space_map", "vmm_space_query", "vmm_space_unmap",
             "vmm_space_protect", "memory_fault_readonly", "memory_fault_nx"):
    check(any(start <= symbol_address(name) < end and flags & 1
              for start, end, flags in loads), "memory implementation linked: " + name)
request_section = named_sections[".limine_requests"]
request_bytes = data[request_section[4]:request_section[4] + request_section[5]]
for label, words in (
        ("HHDM", (0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                  0x48dcf1cb8ad2b852, 0x63984e959a98244b)),
        ("Executable Address", (0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                                0x71ba76863cc55f63, 0xb2644a48c516a487))):
    pattern = struct.pack("<4Q", *words)
    offset = request_bytes.find(pattern)
    check(offset >= 0 and offset % 8 == 0 and request_bytes.count(pattern) == 1,
          "exactly one aligned " + label + " request")
memory_checks = checks - memory_checks_start

# CPL3 v0.5 keeps the same normalized frame/IRETQ ABI checked above.
# The controlled payload lives in read-only kernel data, then the loader copies
# it into a private RW/NX page and seals that user leaf RX before publication.
user_checks_start = checks
probe_start = symbol_address("user_probe_start")
probe_end = symbol_address("user_probe_end")
rodata = named_sections[".rodata"]
for name in ("user_probe_start", "user_probe_end"):
    check(symbol_bindings.get(name) == 1, "exported user payload bound: " + name)
check(probe_start % 16 == 0 and 0 < probe_end - probe_start <= 4096,
      "embedded user payload fits one page with an aligned entry")
check(rodata[3] <= probe_start < probe_end <= rodata[3] + rodata[5],
      "entire embedded user payload belongs to rodata")
check(any(start <= probe_start < probe_end <= end and flags == 4
          for start, end, flags in loads),
      "embedded user source is read-only and non-executable in the kernel")


def probe_bytes(name, size):
    address = symbol_address("user_probe_start" + name)
    check(probe_start <= address and address + size <= probe_end,
          "controlled probe instruction lies within the copied blob: " + name)
    begin = rodata[4] + address - rodata[3]
    return data[begin:begin + size]


# Check meaningful emitted hostile instructions at their own symbols, not
# substring matches that could accidentally match immediates or string data.
# Their resulting CPL3 exceptions and kernel survival are separate QEMU tests.
# NASM encodes the canonical kernel RSP via MOV r64, sign-extended imm32:
# 0x80000000 becomes exactly 0xffffffff80000000 (not a truncated address).
for name, encoded in (
        ("", "49 89 ff"),                     # mov r15,rdi: probe parameter
        (".ud2", "0f 0b"), (".cli", "fa"),
        (".out", "66 ba 80 00 31 c0 ee"),
        (".int240", "cd f0"), (".syscall", "0f 05"),
        (".sysenter", "0f 34"), (".fpu", "d9 ee"),
        (".div0", "b8 01 00 00 00 31 d2 31 c9 48 f7 f1"),
        (".bad_rsp", "49 89 e6 48 bc 00 00 00 00 00 80 00 00"),
        (".kernel_rsp", "49 89 e6 48 c7 c4 00 00 00 80")):
    expected = bytes.fromhex(encoded)
    check(probe_bytes(name, len(expected)) == expected,
          "emitted controlled user entry/probe: " + (name or "entry"))
for name in (".success", ".failure", ".unexpected"):
    address = symbol_address("user_probe_start" + name)
    following = (symbol_address("user_probe_start.failure") if name == ".success"
                 else symbol_address("user_probe_start.unexpected") if name == ".failure"
                 else symbol_address("user_probe_start.message"))
    block = probe_bytes(name, following - address)
    check(block.endswith(bytes.fromhex("cd 80 0f 0b")),
          "user completion uses INT128 with unreachable UD2: " + name)
for name, encoded in (("cpu_write_cr4", "0f 22 e7 c3"),
                      ("cpu_user_clear_segments", "31 c0 8e e0 8e e8 c3")):
    expected = bytes.fromhex(encoded)
    check(executable_bytes(symbol_address(name), len(expected)) == expected,
          "emitted user architecture primitive: " + name)
for name in ("gdt_set_rsp0", "arch_user_init", "arch_user_prepare_return",
             "idt_enable_user_syscall", "user_vm_create", "user_vm_destroy",
             "user_vm_alloc_page", "user_vm_protect_page", "user_vm_copy_from",
             "user_vm_copy_to", "process_spawn_probe", "process_on_syscall",
             "process_on_fault", "process_user_return_valid"):
    check(any(start <= symbol_address(name) < end and flags & 1
              for start, end, flags in loads),
          "user implementation linked in kernel executable section: " + name)
user_checks = checks - user_checks_start

print(f"UTAMO ELF inspection: {checks} checks, 0 failures "
      f"({stub_checks} interrupt stub checks, {abi_checks} ABI checks, "
      f"{memory_checks} memory checks, {user_checks} user-mode checks)")
