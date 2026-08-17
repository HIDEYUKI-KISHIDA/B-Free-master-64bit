#!/usr/bin/env python3
"""Compile-time-style host check that §8.1 macros are usable from C."""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = r"""
#include "bsp_lena_pdx213.h"
int main(void)
{
	_Static_assert(STOS_DRAM_BASE == 0x80000000ULL, "DRAM_BASE");
	_Static_assert(STOS_GICD_BASE == 0x17A00000ULL, "GICD");
	_Static_assert(STOS_GICR_BASE(0) == 0x17A60000ULL, "GICR0");
	_Static_assert(STOS_GICR_BASE(1) == 0x17A80000ULL, "GICR1");
	_Static_assert(STOS_UART_DBG_BASE == 0x0098C000ULL, "UART");
	_Static_assert(STOS_TIMER_PNSIRQ == 30U, "PPI30");
	_Static_assert(STOS_TIMER_CLK_HZ == 19200000U, "CNTFRQ");
	_Static_assert(STOS_CPU_COUNT == 8U, "CPUS");
	_Static_assert(STOS_HYP_MEM_BASE + STOS_HYP_MEM_SIZE - 1ULL == 0x805FFFFFULL, "hyp");
	_Static_assert(STOS_KERNEL_LOAD_SAFE >= 0xA2400000ULL, "safe load after dfps");
	return stos_addr_in_hyp_mem(STOS_HYP_MEM_BASE) ? 0 : 1;
}
"""


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(tmp) / "check_bsp.c"
        exe = pathlib.Path(tmp) / "check_bsp"
        src.write_text(SRC, encoding="utf-8")
        subprocess.check_call(
            [
                "gcc",
                "-I",
                str(ROOT / "bsp/include"),
                "-o",
                str(exe),
                str(src),
            ]
        )
        subprocess.check_call([str(exe)])
    print("host C include of bsp_lena_pdx213.h OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
