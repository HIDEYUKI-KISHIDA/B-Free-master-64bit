#include "security_policy.h"

extern void uart_puts(const char *s);
extern void uart_puthex64(uint64_t val);

static bfree_role_t g_current_role = BFREE_ROLE_KERNEL;

static void audit_prefix(void)
{
    uart_puts("[AUDIT] ");
}

void bfree_audit_log(const char *event, const char *subject, uint64_t value)
{
    audit_prefix();
    uart_puts(event ? event : "event");
    uart_puts(" subject=");
    uart_puts(subject ? subject : "-");
    uart_puts(" value=");
    uart_puthex64(value);
    uart_puts("\n");
}

void bfree_security_init(void)
{
    g_current_role = BFREE_ROLE_KERNEL;
    bfree_audit_log("security_init", "kernel", 0);
}

void bfree_security_set_role(bfree_role_t role)
{
    g_current_role = role;
    bfree_audit_log("role_switch", "role", (uint64_t)role);
}

bfree_role_t bfree_security_get_role(void)
{
    return g_current_role;
}

/*
 * Service-level syscall allowlist.
 * Keep kernel role permissive for bring-up; restrict service roles by default.
 */
int bfree_syscall_allowed(long syscall_num)
{
    switch (g_current_role) {
    case BFREE_ROLE_KERNEL:
    case BFREE_ROLE_INIT:
        return 1;
    case BFREE_ROLE_COMPOSITOR:
        if (syscall_num == 0 || syscall_num == 1 || syscall_num == 2 || syscall_num == 3 ||
            syscall_num == 4 || syscall_num == 20 || syscall_num == 21 || syscall_num == 22 ||
            syscall_num == 25 || syscall_num == 26 || syscall_num == 27 || syscall_num == 28 ||
            syscall_num == 29 || syscall_num == 30 || syscall_num == 31 || syscall_num == 32 ||
            syscall_num == 33 || syscall_num == 34 || syscall_num == 35 || syscall_num == 38 ||
            syscall_num == 39 || syscall_num == 40 || syscall_num == 41) {
            return 1;
        }
        return 0;
    case BFREE_ROLE_NETWORK:
        if (syscall_num == 3 || syscall_num == 20 || syscall_num == 22 || syscall_num == 24 ||
            syscall_num == 29 || syscall_num == 30 || syscall_num == 31 || syscall_num == 32 ||
            syscall_num == 33 || syscall_num == 34 || syscall_num == 35 || syscall_num == 37) {
            return 1;
        }
        return 0;
    case BFREE_ROLE_APP:
    default:
        /* Ring3 Qt/musl uses Linux syscall numbers; only B-Free nr 24/26 hit the switch. */
        if (syscall_num == 24 || syscall_num == 26) {
            return 1;
        }
        return 0;
    }
}

int bfree_verify_boot_stage(const char *stage_name)
{
#if defined(BFREE_STRICT_BOOT_VERIFY) && BFREE_STRICT_BOOT_VERIFY
    /* Strict mode placeholder: fail closed until signature verifier is wired. */
    bfree_audit_log("verify_fail", stage_name, 0);
    return -1;
#else
    bfree_audit_log("verify_ok", stage_name, 0);
    return 0;
#endif
}
