#!/usr/bin/env python3
"""Generate assembly constants from the native ABI headers, without C evaluation."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
HEADERS = ("syscall_abi", "user_probe")

def main():
    destination = ROOT / "build" / "generated"
    destination.mkdir(parents=True, exist_ok=True)
    for stem in HEADERS:
        source = ROOT / "kernel" / "include" / "utamo" / (stem + ".h")
        entries = []
        for line in source.read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"#define (UTAMO_[A-Z0-9_]+) (-?(?:0x[0-9a-fA-F]+|[0-9]+))", line)
            if match:
                entries.append("%define " + match[1] + " " + match[2])
            elif line.startswith("#define UTAMO_") and len(line.split()) > 2:
                raise ValueError("Unsupported assembly constant: " + line)
        if not entries:
            raise ValueError("No numeric constants in " + str(source))
        text = "; Generated from utamo/" + stem + ".h; do not edit.\n"
        text += "\n".join(entries) + "\n"
        target = destination / (stem + ".inc")
        if not target.exists() or target.read_text(encoding="ascii") != text:
            target.write_text(text, encoding="ascii")

if __name__ == "__main__":
    main()
