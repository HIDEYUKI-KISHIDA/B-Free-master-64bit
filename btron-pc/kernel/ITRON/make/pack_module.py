#!/usr/bin/env python3
"""Pack the PE-based itron.image into a faux a.out module for the legacy boot loader."""

from __future__ import annotations

import argparse
import pathlib
import re
import struct
import subprocess
import sys
from typing import Sequence

SECTIONS: Sequence[str] = (".text", ".data", ".rdata", ".eh_fram", ".idata", ".reloc")
ZMAGIC = 0o413
M_386 = 100
BLOCK_SIZE = 512
OBJTOOLS_ROOT = pathlib.Path("C:/msys64/usr/bin")
OBJCOPY = OBJTOOLS_ROOT / "objcopy.exe"
OBJDUMP = OBJTOOLS_ROOT / "objdump.exe"
NM = OBJTOOLS_ROOT / "nm.exe"

def run(cmd: Sequence[str], *, capture: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, text=True if capture else False, capture_output=capture)

def extract_payload(image: pathlib.Path, payload: pathlib.Path) -> None:
    cmd = [str(OBJCOPY), "-O", "binary", "--gap-fill=0"]
    for sec in SECTIONS:
        cmd.extend(["-j", sec])
    cmd.extend([str(image), str(payload)])
    run(cmd)

def parse_bss_size(objdump_stdout: str) -> int:
    m = re.search(r"\\.bss\\s+([0-9a-fA-F]+)", objdump_stdout)
    if not m:
        raise RuntimeError("Failed to locate .bss size in objdump output")
    return int(m.group(1), 16)

def parse_entry(nm_stdout: str, symbol: str) -> int:
    pattern = re.compile(r"^([0-9a-fA-F]+)\s+T\s+" + re.escape(symbol) + r"$", re.MULTILINE)
    m = pattern.search(nm_stdout)
    if not m:
        raise RuntimeError(f"Failed to find symbol '{symbol}' in nm output")
    return int(m.group(1), 16)

def build_exec_header(text_size: int, bss_size: int, entry: int) -> bytes:
    a_info = (ZMAGIC & 0xFFFF) | ((M_386 & 0xFF) << 16)
    fields = (
        a_info,
        text_size,
        0,              # a_data (we embed .data into the text blob)
        bss_size,
        0,              # a_syms
        entry,
        0,              # a_trsize
        0,              # a_drsize
    )
    header = struct.pack("<IIIIIIII", *fields)
    if len(header) > BLOCK_SIZE:
        raise RuntimeError("exec header exceeds block size")
    return header.ljust(BLOCK_SIZE, b"\x00")

def write_module(output: pathlib.Path, header: bytes, payload: bytes) -> None:
    output.write_bytes(header + payload)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=pathlib.Path, nargs="?", default=pathlib.Path("itron.image"))
    parser.add_argument("output", type=pathlib.Path, nargs="?", default=pathlib.Path("itron.module"))
    parser.add_argument("--symbol", default="startup", help="entry symbol (default: startup)")
    args = parser.parse_args()

    if not args.image.exists():
        raise SystemExit(f"input image {args.image} not found")

    payload_path = args.output.with_suffix(".payload")
    extract_payload(args.image, payload_path)
    payload = payload_path.read_bytes()

    objdump_out = run([str(OBJDUMP), "-h", str(args.image)], capture=True).stdout
    bss_size = parse_bss_size(objdump_out)

    nm_out = run([str(NM), str(args.image)], capture=True).stdout
    entry = parse_entry(nm_out, args.symbol)

    header = build_exec_header(len(payload), bss_size, entry)
    write_module(args.output, header, payload)

    if payload_path.exists():
        payload_path.unlink()

    print(f"Created module {args.output} (text+data={len(payload)} bytes, bss={bss_size} bytes, entry=0x{entry:x})")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
