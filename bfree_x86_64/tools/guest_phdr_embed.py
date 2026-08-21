#!/usr/bin/env python3
"""Shared helpers: embedded bfree_guest_phdrs[] in desktop.elf."""
from __future__ import annotations

import struct
import subprocess
from pathlib import Path

PT_GNU_EH_FRAME = 0x6474E551


def read_elf_phdrs(data: bytes) -> tuple[dict, dict, int]:
    if data[:4] != b"\x7fELF":
        raise SystemExit("not ELF")
    e_entry = struct.unpack_from("<Q", data, 24)[0]
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
    return load, tls, e_entry


def va_to_file_offset(data: bytes, va: int) -> int:
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        ptype, pflags, poff, vaddr, paddr, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", data, off)
        if ptype == 1 and vaddr <= va < vaddr + memsz:
            return int(va - vaddr + poff)
    raise SystemExit(f"VA 0x{va:x} not in PT_LOAD")


def mk_phdr(ptype: int, flags: int, off: int, vaddr: int, filesz: int, memsz: int, align: int) -> bytes:
    return struct.pack("<IIQQQQQQ", ptype, flags, off, vaddr, vaddr, filesz, memsz, align)


def embedded_phdr_bytes(load: dict, tls: dict) -> bytes:
    return b"".join(
        [
            mk_phdr(1, load["flags"], load["off"], load["vaddr"], load["filesz"], load["memsz"], load["align"]),
            mk_phdr(7, tls["flags"], tls["off"], tls["vaddr"], tls["filesz"], tls["memsz"], tls["align"]),
            mk_phdr(PT_GNU_EH_FRAME, 4, 0, 0, 0, 0, 0x10),
        ]
    )


def nm_sym_va(path: Path, name: str) -> int:
    out = subprocess.check_output(["nm", str(path)], text=True, stderr=subprocess.DEVNULL)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1].endswith(name):
            return int(parts[0], 16)
    raise SystemExit(f"nm: *{name} not found in {path}")


def read_embedded(path: Path) -> tuple[int, bytes, bytes]:
    data = path.read_bytes()
    load, tls, _entry = read_elf_phdrs(data)
    expect = embedded_phdr_bytes(load, tls)
    sym_va = nm_sym_va(path, "bfree_guest_phdrs")
    sym_off = va_to_file_offset(data, sym_va)
    embed = data[sym_off : sym_off + len(expect)]
    return sym_off, embed, expect
