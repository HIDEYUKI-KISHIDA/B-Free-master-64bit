#ifndef BFREE_SYSCALL_DISPATCH_H
#define BFREE_SYSCALL_DISPATCH_H

void bfree_syscall_registry_init(void);
int bfree_syscall_is_implemented(unsigned long nr);
int bfree_syscall_enosys_count(void);

#endif
