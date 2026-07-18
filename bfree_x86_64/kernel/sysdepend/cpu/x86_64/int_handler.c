/*
 * int_handler.c - x86_64 T-Kernel2.0 割込み共通ハンドラ
 * 仕様: TK2_x86_64_Spec.md v2.3準拠
 */
#include <stdint.h>

#include "tk/sysdef_depend.h"  // T_REGS定義

// knl_dispatch_request, knl_interrupt_mainは新しい割り込み基盤に移行
