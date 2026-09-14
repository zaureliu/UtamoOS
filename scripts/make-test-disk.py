#!/usr/bin/env python3
"""Disposable deterministic FAT32 superfloppy fixtures, strictly build/tests."""
from pathlib import Path
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
TOTAL, RESERVED, FAT_SECTORS, DATA = 131072, 32, 1024, 2080
FILE = b"UTAMO readonly AHCI and FAT32\n"
README = b"Nested FAT32 directory works\n"
CHAIN = bytes((i * 17 + 3) & 255 for i in range(2049))

def put(buf, offset, value, size):
    buf[offset:offset+size] = value.to_bytes(size, "little")

def directory_entry(name, attr, cluster, size):
    out = bytearray(32)
    out[:11] = name.encode("ascii")
    out[11] = attr
    put(out, 20, cluster >> 16, 2)
    put(out, 26, cluster & 65535, 2)
    put(out, 28, size, 4)
    return out

def generate(path, variant="good"):
    boot = bytearray(512)
    boot[:3], boot[3:11] = b"\xeb\x58\x90", b"UTAMO   "
    put(boot,11,512,2); boot[13] = 1; put(boot,14,RESERVED,2); boot[16] = 2
    boot[21] = 0xf8; put(boot,24,63,2); put(boot,26,255,2)
    put(boot,32,TOTAL,4); put(boot,36,FAT_SECTORS,4); put(boot,44,2,4)
    put(boot,48,1,2); put(boot,50,6,2)
    boot[64],boot[66] = 0x80,0x29; put(boot,67,0x5554414d,4)
    boot[71:82],boot[82:90],boot[510:] = b"UTAMO TEST ",b"FAT32   ",b"\x55\xaa"
    fat = bytearray(FAT_SECTORS*512)
    for cluster in range(13): put(fat,cluster*4,0x0fffffff,4)
    put(fat,0,0x0ffffff8,4)
    # Root=2, FILE=3, DOCS=4, README=5, CHAIN fragmented=6,9,7,11,8.
    for a,b in zip([6,9,7,11],[9,7,11,8]): put(fat,a*4,b,4)
    root = bytearray(512)
    entries = [("FILE    TXT",0x20,3,len(FILE)),("DOCS       ",0x10,4,0),
               ("CHAIN   BIN",0x20,6,len(CHAIN)),("EMPTY   TXT",0x20,0,0)]
    for i,args in enumerate(entries): root[i*32:(i+1)*32] = directory_entry(*args)
    docs = bytearray(512)
    for i,args in enumerate([(".          ",0x10,4,0),("..         ",0x10,0,0),
                            ("README  TXT",0x20,5,len(README))]):
        docs[i*32:(i+1)*32] = directory_entry(*args)
    if variant == "bad-bpb": boot[13] = 0
    elif variant == "root-loop": put(fat,8,2,4)
    elif variant == "file-loop": put(fat,9*4,6,4)
    elif variant == "bad-cluster": put(fat,9*4,TOTAL,4)
    elif variant == "cross-link": put(root,2*32+26,3,2)
    elif variant not in ("good","mirror-mismatch"): raise ValueError(variant)
    fsinfo = bytearray(512)
    put(fsinfo,0,0x41615252,4);put(fsinfo,484,0x61417272,4)
    put(fsinfo,488,0xffffffff,4);put(fsinfo,492,0xffffffff,4);put(fsinfo,508,0xaa550000,4)
    with path.open("wb") as f:
        f.truncate(TOTAL*512)
        for lba,data in [(0,boot),(1,fsinfo),(6,boot),(7,fsinfo),
                         (RESERVED,fat),(RESERVED+FAT_SECTORS,fat),
                         (DATA,root),(DATA+1,FILE),(DATA+2,docs),(DATA+3,README)]:
            f.seek(lba*512); f.write(data)
        for i,cluster in enumerate([6,9,7,11,8]):
            f.seek((DATA+cluster-2)*512);f.write(CHAIN[i*512:(i+1)*512])
        if variant == "mirror-mismatch":
            f.seek((RESERVED+FAT_SECTORS)*512+12);f.write(struct.pack("<I",0))
    return {"name":path.name,"variant":variant,"bytes":TOTAL*512,
            "sha256":hashlib.file_digest(path.open("rb"),"sha256").hexdigest()}

def main():
    folder = ROOT/"build"/"tests"
    if (ROOT/"build").is_symlink() or folder.is_symlink(): raise SystemExit("Refusing symlink output")
    folder.mkdir(parents=True,exist_ok=True)
    if folder.resolve() != ROOT/"build"/"tests": raise SystemExit("Unsafe output")
    results=[]
    for variant in ["good","bad-bpb","root-loop","file-loop","bad-cluster","cross-link","mirror-mismatch"]:
        path=folder/("fat32-"+variant+".img")
        if path.is_symlink(): raise SystemExit("Refusing symlink fixture")
        results.append(generate(path,variant))
    manifest=folder/"fat32-fixtures.json"
    if manifest.is_symlink(): raise SystemExit("Refusing symlink manifest")
    manifest.write_text(json.dumps(results,indent=2)+"\n")
    print("Created 7 disposable FAT32 fixtures in build/tests")
if __name__ == "__main__":
    main()
