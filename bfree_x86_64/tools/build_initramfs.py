#!/usr/bin/env python3
"""Build minimal newc cpio archive for M8 kernel embed."""
from __future__ import annotations

import os
import sys


def hex8(value: int) -> str:
    return f"{value:08x}"


def align4(n: int) -> int:
    return (n + 3) & ~3


def write_entry(out, name: str, data: bytes, mode: int = 0o100644) -> None:
    namesz = len(name) + 1
    hdr = (
        "070701"
        + hex8(1)
        + hex8(mode)
        + hex8(0)
        + hex8(0)
        + hex8(1)
        + hex8(0)
        + hex8(len(data))
        + hex8(0)
        + hex8(0)
        + hex8(0)
        + hex8(0)
        + hex8(namesz)
        + hex8(0)
    )
    out.write(hdr.encode("ascii"))
    out.write(name.encode("ascii") + b"\0")
    out.write(b"\0" * (align4(namesz) - namesz))
    out.write(data)
    out.write(b"\0" * (align4(len(data)) - len(data)))


def main() -> int:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    src = os.path.join(root, "guest", "initramfs")
    out_path = os.path.join(root, "build", "initramfs.cpio")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as out:
        for dirpath, _, files in sorted(os.walk(src)):
            for fname in sorted(files):
                full = os.path.join(dirpath, fname)
                rel = os.path.relpath(full, src).replace(os.sep, "/")
                with open(full, "rb") as f:
                    write_entry(out, rel, f.read())

        write_entry(out, "TRAILER!!!", b"")

    print(f"OK: {out_path} ({os.path.getsize(out_path)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
