#!/usr/bin/env python3
"""Verify bfree_guest_phdrs[] embedded in desktop.elf matches ELF program headers."""
from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path


def read_phdrs(path: Path) -> tuple[dict, dict]:
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise SystemExit("not ELF")
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)
    load = tls = None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        ptype, pflags, poff, vaddr, paddr, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", data, off)
        row = {"off": poff, "vaddr": vaddr, "filesz": filesz, "memsz": memsz, "align": align, "flags": pflags}
        if ptype == 1:
            load = row
        elif ptype == 7:
            tls = row
    if not load or not tls:
        raise SystemExit("ELF missing PT_LOAD or PT_TLS")
    return load, tls


def mk_phdr(ptype: int, flags: int, off: int, vaddr: int, filesz: int, memsz: int, align: int) -> bytes:
    return struct.pack("<IIQQQQQQ", ptype, flags, off, vaddr, vaddr, filesz, memsz, align)


def nm_sym(path: Path, name: str) -> int:
    out = subprocess.check_output(["nm", str(path)], text=True, stderr=subprocess.DEVNULL)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1].endswith(name):
            return int(parts[0], 16)
    raise SystemExit(f"nm: *{name} not found")


def main() -> int:
    elf = Path(sys.argv[1] if len(sys.argv) > 1 else "userland/desktop_qt/desktop.elf")
    if not elf.is_file():
        print(f"FAIL: missing {elf}", file=sys.stderr)
        return 1
    load, tls = read_phdrs(elf)
    sym = nm_sym(elf, "bfree_guest_phdrs")
    data = elf.read_bytes()
    expect = b"".join(
        [
            mk_phdr(1, load["flags"], load["off"], load["vaddr"], load["filesz"], load["memsz"], load["align"]),
            mk_phdr(7, tls["flags"], tls["off"], tls["vaddr"], tls["filesz"], tls["memsz"], tls["align"]),
            mk_phdr(0x6474E551, 4, 0, 0, 0, 0, 0x10),
        ]
    )
    embed = data[sym : sym + len(expect)]
    if embed == expect:
        print(f"[phdr-check] OK embedded phdrs match ELF (0x{sym:x})")
        return 0
    print("FAIL: stale bfree_guest_phdrs[] (musl __copy_tls GP on vfork exec)", file=sys.stderr)
    print(f"  embedded@0x{sym:x} != ELF PT_LOAD/PT_TLS", file=sys.stderr)
    print("  bash tools/relink_desktop_phdrs_only.sh", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
