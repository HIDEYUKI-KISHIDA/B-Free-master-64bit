#ifndef BFREE_TK_TYPEDEF_H
#define BFREE_TK_TYPEDEF_H

#include <stdint.h>
#include <stddef.h>

#ifndef CONST
#define CONST const
#endif

typedef int32_t INT;
typedef uint32_t UINT;
typedef int32_t ID;
typedef int32_t ER;
typedef int32_t ATR;
typedef int32_t PRI;
typedef int32_t TMO;
typedef void *VP;
typedef unsigned char UB;
typedef void (*FP)(INT, void *);

#endif