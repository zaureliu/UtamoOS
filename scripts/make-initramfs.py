#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deterministic newc metadata; payload bytes are the separately built native ELFs."""
from pathlib import Path
import hashlib
ROOT = Path(__file__).resolve().parent.parent

def newc(entries):
    output = bytearray()
    for ino, (name, mode, data) in enumerate([*entries, ("TRAILER!!!", 0, b"")], 1):
        encoded = name.encode("ascii") + b"\0"
        fields = (ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0)
        output += b"070701" + b"".join(f"{x:08x}".encode() for x in fields)
        output += encoded
        output += bytes(-len(output) % 4)
        output += data
        output += bytes(-len(output) % 4)
    return bytes(output)

def main():
    programs = ("init", "hello", "echo", "sysinfo", "filetest", "badptr")
    entries = [(name, 0o40755, b"") for name in ("bin", "dev", "etc")]
    entries += [("bin/" + name, 0o100755, (ROOT / "build/userspace" / name).read_bytes())
                for name in programs]
    entries += [("etc/motd", 0o100644, b"UTAMO native VFS and initramfs\n"),
                ("etc/empty", 0o100644, b""),
                ("etc/not-elf", 0o100755, b"deliberately malformed ELF\n")]
    image = newc(entries)
    target = ROOT / "build/initramfs.cpio"
    target.write_bytes(image)
    print(f"initramfs: {len(image)} bytes; sha256 {hashlib.sha256(image).hexdigest()}")

if __name__ == "__main__":
    main()
