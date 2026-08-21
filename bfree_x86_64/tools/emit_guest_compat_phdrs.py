#!/usr/bin/env python3
"""Emit bfree_guest_phdrs[] C initializer from desktop.elf (patch guest_link_compat.cpp)."""
from __future__ import annotations

import struct
import sys
from pathlib import Path


def read_phdrs(path: Path) -> tuple[dict, dict, int]:
    data = path.read_bytes()
    e_entry = struct.unpack_from("<Q", data, 24)[0]
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)
    load = tls = None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        ptype, pflags, poff, vaddr, paddr, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", data, off)
        row = (poff, vaddr, filesz, memsz, align, pflags)
        if ptype == 1:
            load = row
        elif ptype == 7:
            tls = row
    if not load or not tls:
        raise SystemExit("ELF missing PT_LOAD or PT_TLS")
    return load, tls, e_entry


def fmt_row(pt: str, flags: int, off: int, vaddr: int, filesz: int, memsz: int, align: int) -> str:
    return (
        f"    {{ {pt}, PF_R"
        + (" | PF_W | PF_X" if pt == "PT_LOAD" else "")
        + f", 0x{off:x}, 0x{vaddr:x}, 0x{vaddr:x}, 0x{filesz:x}, 0x{memsz:x}, 0x{align:x} }},"
    )


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    elf = Path(sys.argv[1] if len(sys.argv) > 1 else root / "userland/desktop_qt/desktop.elf")
    cpp = Path(sys.argv[2] if len(sys.argv) > 2 else root / "tools/guest_link_compat.cpp")
    load, tls, entry = read_phdrs(elf)
    block = "\n".join(
        [
            "static const Elf64_Phdr bfree_guest_phdrs[] = {",
            fmt_row("PT_LOAD", load[5], load[0], load[1], load[2], load[3], load[4]),
            fmt_row("PT_TLS", tls[5], tls[0], tls[1], tls[2], tls[3], tls[4]),
            "    { PT_GNU_EH_FRAME, PF_R, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10 },",
            "};",
        ]
    )
    text = cpp.read_text(encoding="utf-8")
    start = text.index("static const Elf64_Phdr bfree_guest_phdrs[] = {")
    end = text.index("};", start) + 2
    text = text[:start] + block + text[end:]
    text = text.replace("bfree_guest_auxv_sparse[AT_ENTRY] = 0x2800000;",
                        f"bfree_guest_auxv_sparse[AT_ENTRY] = 0x{entry:x};")
    text = text.replace("bfree_guest_auxv_pairs[9] = 0x2800000;",
                        f"bfree_guest_auxv_pairs[9] = 0x{entry:x};")
    cpp.write_text(text, encoding="utf-8")
    print(f"[emit-phdrs] {cpp} <= {elf} entry=0x{entry:x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
