/*
 * H01 CATCH delivery: minimal rt_sigframe on user stack
 * (pretcode=restorer + padded ucontext/mcontext gregs + optional siginfo).
 * SYSRET restores via g_bfree_sysret_exec_* + g_bfree_sig_saved_* in syscall_entry.S.
 * SA_ONSTACK uses g_sigalt_* (H17). sa_mask OR'd for handler (+self unless NODEFER).
 * SA_SIGINFO: rdi=sig, rsi=&info, rdx=&uc (minimal siginfo si_signo/si_code).
 * Residuals: no full glibc fpstate, no nested CATCH.
 */
static int g_sig_deliver_sig;
static int g_sig_in_handler;
static int g_sig_mask_pushed;
static uint64_t g_sig_saved_rax;
static uint64_t g_sig_saved_rdi;
static uint64_t g_sig_saved_rsi;
static uint64_t g_sig_saved_rdx;
static uint64_t g_sig_saved_rbx;
static uint64_t g_sig_saved_rbp;
static uint64_t g_sig_saved_r12;
static uint64_t g_sig_saved_r13;
static uint64_t g_sig_saved_r14;
static uint64_t g_sig_saved_r15;
static uint64_t g_sig_saved_rip;
static uint64_t g_sig_saved_rsp;
static uint64_t g_sig_saved_rflags;
static uint64_t g_sig_saved_mask;

/* Minimal Linux-ish mcontext gregs blob (not byte-identical to glibc). */
typedef struct {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, efl;
    uint64_t csgsfs, err, trapno, oldmask, cr2;
} bfree_sig_mcontext_t;

typedef struct {
    uint64_t pad_uc_flags;
    uint64_t pad_uc_link;
    uint64_t pad_ss_sp;
    uint64_t pad_ss_flags;
    uint64_t pad_ss_size;
    bfree_sig_mcontext_t mc;
    uint64_t uc_sigmask;
} bfree_sig_ucontext_t;

/* Compact siginfo (first fields matter for musl SA_SIGINFO handlers). */
typedef struct {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    int32_t si_pad;
    uint64_t si_addr;
    int32_t si_status;
    int32_t si_pad2;
    uint64_t si_value;
    uint8_t pad[96];
} bfree_siginfo_min_t;

typedef struct {
    uint64_t pretcode; /* restorer — return address for classic sa_handler */
    bfree_sig_ucontext_t uc;
    bfree_siginfo_min_t info;
} bfree_rt_sigframe_t;