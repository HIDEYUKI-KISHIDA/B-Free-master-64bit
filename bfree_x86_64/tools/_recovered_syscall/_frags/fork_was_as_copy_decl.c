static int g_guest_fork_active;
static int g_guest_fork_pid;
static int g_guest_fork_status;
static int g_guest_fork_status_ready;
/* 1 if this coop child was created via SYS_fork AS-copy (parent kept running).
 * Distinct from has_private_as: vfork+exec also gains a private AS, but the
 * parent was frozen and still needs the shared-AS stack snapshot restored. */
static int g_guest_fork_was_as_copy;