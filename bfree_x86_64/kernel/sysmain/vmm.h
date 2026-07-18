/*
 * Guest address-space helpers (M2 fork/exec).
 */
#ifndef BFREE_VMM_H
#define BFREE_VMM_H

#include "process.h"

int  bfree_as_init(struct bfree_as *as, size_t size);
void bfree_as_free(struct bfree_as *as);
int  bfree_as_fork_copy(struct bfree_as *child, const struct bfree_as *parent);
uint8_t *bfree_as_ptr(struct bfree_as *as);

#endif /* BFREE_VMM_H */
