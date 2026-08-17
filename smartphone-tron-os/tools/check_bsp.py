#!/usr/bin/env python3
"""Phase 1 exit check: bsp_lena_pdx213.h has every §8.1 symbol filled."""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "bsp/include/bsp_lena_pdx213.h"

REQUIRED = {
    "STOS_DRAM_BASE": 0x80000000,
    "STOS_DRAM_SIZE": 0x180000000,
    "STOS_KERNEL_LOAD_BASE": 0x88000000,
    "STOS_GICD_BASE": 0x17A00000,
    "STOS_UART_DBG_BASE": 0x0098C000,
    "STOS_TIMER_PNSIRQ": 30,
    "STOS_TIMER_CLK_HZ": 19200000,
    "STOS_CPU_COUNT": 8,
    "STOS_HYP_MEM_BASE": 0x80000000,
    "STOS_HYP_MEM_SIZE": 0x600000,
}


def parse_defines(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for m in re.finditer(
        r"^\s*#define\s+(STOS_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)[UuLl]*",
        text,
        re.M,
    ):
        name, raw = m.group(1), m.group(2)
        values[name] = int(raw, 0)
    return values


def main() -> int:
    text = HEADER.read_text(encoding="utf-8")
    values = parse_defines(text)

    missing = [n for n in REQUIRED if n not in values]
    if missing:
        print("FAIL: missing", ", ".join(missing), file=sys.stderr)
        return 1

    bad = []
    for name, expected in REQUIRED.items():
        got = values[name]
        if got != expected:
            bad.append(f"{name}: got {got:#x} want {expected:#x}")
    if "STOS_GICR_BASE" not in text:
        bad.append("STOS_GICR_BASE(cpu) macro missing")
    if "0x17A60000" not in text:
        bad.append("GICR CPU0 base 0x17A60000 missing")

    hyp_end = values["STOS_HYP_MEM_BASE"] + values["STOS_HYP_MEM_SIZE"] - 1
    if hyp_end != 0x805FFFFF:
        bad.append(f"hyp_mem end {hyp_end:#x} != 0x805FFFFF")

    load = values["STOS_KERNEL_LOAD_BASE"]
    if values["STOS_HYP_MEM_BASE"] <= load <= hyp_end:
        bad.append("kernel load sits inside hyp_mem (A-5)")

    if bad:
        print("FAIL:", file=sys.stderr)
        for line in bad:
            print(" ", line, file=sys.stderr)
        return 1

    print("Phase 1 BSP header OK")
    for name in REQUIRED:
        print(f"  {name} = {values[name]:#x}")
    print("  STOS_GICR_BASE(0) = 0x17a60000")
    print("  note: spec load 0x88000000 is inside DTS pil_cdsp;")
    print("        STOS_KERNEL_LOAD_SAFE = 0xa4000000")
    return 0


if __name__ == "__main__":
    sys.exit(main())
