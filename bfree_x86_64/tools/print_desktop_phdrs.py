#!/usr/bin/env python3
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "userland/desktop_qt/desktop.elf"
with open(path, "rb") as f:
    f.seek(0)
    if f.read(4) != b"\x7fELF":
        raise SystemExit("not ELF")
    f.seek(0)
    hdr = f.read(64)
    e_entry, e_phoff = struct.unpack_from("<QQ", hdr, 24)
    e_phentsize, e_phnum = struct.unpack_from("<HH", hdr, 54)
    print(f"entry=0x{e_entry:x} phnum={e_phnum} phentsize={e_phentsize}")
    f.seek(e_phoff)
    for i in range(e_phnum):
        ph = f.read(e_phentsize)
        ptype, pflags, off, vaddr, paddr, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", ph)
        names = {1: "LOAD", 2: "DYNAMIC", 7: "TLS", 0x6474E551: "GNU_EH_FRAME", 0x6474E552: "GNU_RELRO"}
        print(
            f"  [{i}] {names.get(ptype, hex(ptype))} flags=0x{pflags:x} "
            f"off=0x{off:x} vaddr=0x{vaddr:x} filesz=0x{filesz:x} memsz=0x{memsz:x} align=0x{align:x}"
        )
