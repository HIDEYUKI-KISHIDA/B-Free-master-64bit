#!/usr/bin/env python3
"""Patch bfree_guest_phdrs[] inside desktop.elf (no guest_link_compat recompile)."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from guest_phdr_embed import read_elf_phdrs, read_embedded, va_to_file_offset, embedded_phdr_bytes, nm_sym_va  # noqa: E402


def main() -> int:
    elf = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "userland/desktop_qt/desktop.elf")
    if not elf.is_file():
        print(f"FAIL: missing {elf}", file=sys.stderr)
        return 1

    sym_off, embed, expect = read_embedded(elf)
    if embed == expect:
        print(f"[phdr-patch] already OK @0x{sym_off:x}")
        return 0

    data = bytearray(elf.read_bytes())
    data[sym_off : sym_off + len(expect)] = expect
    elf.write_bytes(data)

    sym_off2, embed2, expect2 = read_embedded(elf)
    if embed2 != expect2:
        print("FAIL: patch did not stick", file=sys.stderr)
        return 1

    load, tls, entry = read_elf_phdrs(bytes(data))
    print(f"[phdr-patch] OK @0x{sym_off2:x} LOAD memsz=0x{load['memsz']:x} TLS va=0x{tls['vaddr']:x} entry=0x{entry:x}")
    print(f"[phdr-patch] {elf}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
