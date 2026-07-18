/*
 * fpu_state.h - x86_64 XSAVE/XRSTOR FPU/SSE保存領域定義
 * 仕様: TK2_x86_64_Spec.md v2.4準拠
 */
#ifndef __TK_FPU_STATE_H__
#define __TK_FPU_STATE_H__

#include <stdint.h>

/* XSAVE/XRSTOR用FPU状態保存領域（64バイトアライン） */
typedef struct {
    uint8_t data[1024] __attribute__((aligned(64)));
} fpu_state_t;

#endif /* __TK_FPU_STATE_H__ */
