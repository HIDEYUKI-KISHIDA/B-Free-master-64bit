#ifndef BFREE_TK_DEVMGR_H
#define BFREE_TK_DEVMGR_H

#include <tk/typedef.h>

typedef struct {
    ATR devatr;
    INT blksz;
    FP openfn;
    FP closefn;
    FP execfn;
} T_DDEV;

typedef struct {
    INT dummy;
} T_IDEV;

#endif