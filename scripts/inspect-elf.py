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
expect_bytes("48 89 e3", "common: callee-saved RBX holds original frame")
expect_bytes("48 83 e4 f0", "common: RSP aligned to 16 bytes before CALL")
call = executable_bytes(cursor, 5)
check(call[0] == 0xe8, "common: near relative CALL")
check(cursor + 5 + struct.unpack_from("<i", call, 1)[0] ==
      symbol_address("interrupt_dispatch"), "common: C dispatcher target")
cursor += 5
expect_bytes("48 89 dc", "common: restores RSP from frame anchor")
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
abi_checks = checks - abi_checks_start

# Memory v0.2 retains four LOADs and the existing interrupt ABI.
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

print(f"UTAMO ELF inspection: {checks} checks, 0 failures "
      f"({stub_checks} interrupt stub checks, {abi_checks} ABI checks, "
      f"{memory_checks} memory checks)")
