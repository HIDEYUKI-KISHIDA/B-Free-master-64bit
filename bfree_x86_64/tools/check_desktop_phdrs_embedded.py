#!/usr/bin/env python3
"""Verify bfree_guest_phdrs[] embedded in desktop.elf matches ELF program headers."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from guest_phdr_embed import read_embedded  # noqa: E402


def main() -> int:
    elf = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "userland/desktop_qt/desktop.elf")
    if not elf.is_file():
        print(f"FAIL: missing {elf}", file=sys.stderr)
        return 1

    sym_off, embed, expect = read_embedded(elf)
    if embed == expect:
        print(f"[phdr-check] OK embedded phdrs match ELF (file@0x{sym_off:x})")
        return 0

    print("FAIL: stale bfree_guest_phdrs[] (musl __copy_tls GP on vfork exec)", file=sys.stderr)
    print(f"  embedded@0x{sym_off:x} != ELF PT_LOAD/PT_TLS", file=sys.stderr)
    print("  bash tools/relink_desktop_phdrs_only.sh", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
