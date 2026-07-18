#ifndef BFREE_SECURITY_POLICY_H
#define BFREE_SECURITY_POLICY_H

#include <stdint.h>

typedef enum {
    BFREE_ROLE_KERNEL = 0,
    BFREE_ROLE_INIT = 1,
    BFREE_ROLE_COMPOSITOR = 2,
    BFREE_ROLE_NETWORK = 3,
    BFREE_ROLE_APP = 4
} bfree_role_t;

void bfree_security_init(void);
void bfree_security_set_role(bfree_role_t role);
bfree_role_t bfree_security_get_role(void);

int bfree_syscall_allowed(long syscall_num);

void bfree_audit_log(const char *event, const char *subject, uint64_t value);

/*
 * Secure-boot chain hook.
 * Returns 0 on success.
 * In current stage this is a policy gate hook that can be made strict
 * with BFREE_STRICT_BOOT_VERIFY=1 and wired to real signature checks later.
 */
int bfree_verify_boot_stage(const char *stage_name);

#endif
