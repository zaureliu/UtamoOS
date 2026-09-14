#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent inspection of the linked native user ELF/entry/runtime contract."""
from pathlib import Path
import struct
import subprocess
ROOT=Path(__file__).resolve().parent.parent
checks=0
def check(value, message):
    global checks
    checks+=1
    if not value:
        raise SystemExit("FAIL userspace ELF: "+message)
for name in ("init","hello","echo","sysinfo","filetest","badptr"):
    path=ROOT/"build/userspace"/name
    data=path.read_bytes()
    header=struct.unpack_from("<16sHHIQQQIHHHHHH",data)
    check(header[0][:9]==b"\x7fELF\x02\x01\x01\x00\x00","ELF64 little-endian "+name)
    check(header[1:4]==(2,62,1),"native static ET_EXEC x86_64 "+name)
    check(header[8:11]==(64,56,3),"three fixed program headers "+name)
    entry,phoff=header[4:6]
    segments=[struct.unpack_from("<IIQQQQQQ",data,phoff+i*56) for i in range(3)]
    check([p[0] for p in segments]==[1,1,1],"LOAD only "+name)
    check([p[1] for p in segments]==[5,4,6],"RX/R/RW and no W+X "+name)
    end=0
    for p in segments:
        _,flags,offset,address,_,filesz,memsz,alignment=p
        check(0x10000<=address<0x6ffef000 and memsz>0 and memsz>=filesz,"bounded segment "+name)
        check(offset+filesz<=len(data) and offset%4096==address%4096,"file-backed bytes "+name)
        check(alignment==4096 and address%4096==0 and address>=end,"disjoint aligned pages "+name)
        end=(address+memsz+4095)&~4095
    check(entry==0x400000 and segments[0][3]<=entry<segments[0][3]+segments[0][5],"native entry "+name)
    undefined=subprocess.check_output([str(ROOT/"toolchain/prefix/bin/x86_64-elf-nm"),"-u",str(path)],text=True)
    check(not undefined.strip(),"no undefined symbols or host runtime "+name)
    if name=="filetest":
        check(segments[2][6]-segments[2][5]>=5000,"real BSS payload")
print(f"UTAMO userspace ELF inspection: {checks} checks, 0 failures")
