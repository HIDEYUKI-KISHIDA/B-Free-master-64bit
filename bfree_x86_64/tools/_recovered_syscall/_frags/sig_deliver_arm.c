    sig = g_sig_deliver_sig;
    g_sig_deliver_sig = 0;
    handler = g_guest_sig_handler[sig];
    restorer = g_guest_sig_restorer[sig];

    /* Require CATCH handler + restorer. SA_SIGINFO uses rsi/rdx → info/uc. */
    if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH ||
        handler == 0 || handler == (void *)(uintptr_t)1 ||
        restorer == 0 ||
        !bfree_user_ptr_mapped((long)(uintptr_t)handler) ||
        !bfree_user_ptr_mapped((long)(uintptr_t)restorer)) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }

    rsp = g_bfree_user_sysret_rsp;
    /* H17: SA_ONSTACK → grow down from registered altstack top if not already on it. */
    if ((g_guest_sig_flags[sig] & BFREE_SA_ONSTACK) != 0UL &&
        !g_sigalt_disable && g_sigalt_sp != 0 && g_sigalt_size >= (size_t)BFREE_MINSIGSTKSZ &&
        !bfree_sigalt_onstack(rsp)) {
        rsp = ((uint64_t)(uintptr_t)g_sigalt_sp + (uint64_t)g_sigalt_size) & ~15ULL;
    }
    if (rsp < (uint64_t)sizeof(bfree_rt_sigframe_t) + 16ULL) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }
    rsp &= ~15ULL;
    frame_base = rsp - (uint64_t)sizeof(bfree_rt_sigframe_t);
    if (!bfree_user_range_mapped(frame_base, sizeof(bfree_rt_sigframe_t))) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }
    frame = (bfree_rt_sigframe_t *)(uintptr_t)frame_base;
    memset(frame, 0, sizeof(*frame));
    frame->pretcode = (uint64_t)(uintptr_t)restorer;
    frame->info.si_signo = sig;
    frame->info.si_code = 0; /* SI_USER-ish */

    g_sig_saved_rax = (uint64_t)(int64_t)syscall_ret;
    g_sig_saved_rdi = 0; /* syscall arg regs are call-clobbered at this point */
    g_sig_saved_rsi = 0;
    g_sig_saved_rdx = g_bfree_user_sysret_rdx;
    g_sig_saved_rbx = g_bfree_user_sysret_rbx;
    g_sig_saved_rbp = g_bfree_user_sysret_rbp;
    g_sig_saved_r12 = g_bfree_user_sysret_r12;
    g_sig_saved_r13 = g_bfree_user_sysret_r13;
    g_sig_saved_r14 = g_bfree_user_sysret_r14;
    g_sig_saved_r15 = g_bfree_user_sysret_r15;
    g_sig_saved_rip = g_bfree_user_sysret_rcx;
    g_sig_saved_rsp = g_bfree_user_sysret_rsp;
    g_sig_saved_rflags = g_bfree_user_sysret_r11;
    g_sig_saved_mask = g_guest_sig_mask;

    frame->uc.pad_ss_sp = (uint64_t)(uintptr_t)g_sigalt_sp;
    frame->uc.pad_ss_flags = g_sigalt_disable ? (uint64_t)BFREE_SS_DISABLE :
        (bfree_sigalt_onstack(g_sig_saved_rsp) ? (uint64_t)BFREE_SS_ONSTACK : 0ULL);
    frame->uc.pad_ss_size = (uint64_t)g_sigalt_size;
    frame->uc.mc.r12 = g_sig_saved_r12;
    frame->uc.mc.r13 = g_sig_saved_r13;
    frame->uc.mc.r14 = g_sig_saved_r14;
    frame->uc.mc.r15 = g_sig_saved_r15;
    frame->uc.mc.rdi = g_sig_saved_rdi;
    frame->uc.mc.rsi = g_sig_saved_rsi;
    frame->uc.mc.rbp = g_sig_saved_rbp;
    frame->uc.mc.rbx = g_sig_saved_rbx;
    frame->uc.mc.rdx = g_sig_saved_rdx;
    frame->uc.mc.rax = g_sig_saved_rax;
    frame->uc.mc.rsp = g_sig_saved_rsp;
    frame->uc.mc.rip = g_sig_saved_rip;
    frame->uc.mc.efl = g_sig_saved_rflags;
    frame->uc.mc.oldmask = g_sig_saved_mask;
    frame->uc.uc_sigmask = g_sig_saved_mask;

    g_guest_sig_mask |= g_guest_sig_sa_mask[sig];
    if ((g_guest_sig_flags[sig] & BFREE_SA_NODEFER) == 0UL) {
        g_guest_sig_mask |= (1ULL << (unsigned)(sig - 1));
    }
    /* Never mask SIGKILL/SIGSTOP. */
    g_guest_sig_mask &= ~((1ULL << 8) | (1ULL << 18));
    g_sig_mask_pushed = 1;
    g_sig_in_handler = 1;

    /* Handler entry: rsp → pretcode (restorer); rdi = signo. */
    g_bfree_sysret_exec_rsp = frame_base;
    g_bfree_sysret_exec_rcx = (uint64_t)(uintptr_t)handler;
    g_bfree_sysret_exec_r11 = g_bfree_user_sysret_r11 | 0x200ULL;
    g_bfree_sysret_exec_rdi = (uint64_t)(unsigned)sig;
    if ((g_guest_sig_flags[sig] & BFREE_SA_SIGINFO) != 0UL) {
        g_bfree_sysret_exec_rsi = frame_base + offsetof(bfree_rt_sigframe_t, info);
        g_bfree_sysret_exec_rdx = frame_base + offsetof(bfree_rt_sigframe_t, uc);
    } else {
        g_bfree_sysret_exec_rsi = 0;
        g_bfree_sysret_exec_rdx = 0;
    }
    g_bfree_sysret_exec_cr3 = 0;
    g_bfree_sysret_sig_rax = 0;
    /* H01: publish callee-saved for syscall_entry.S (handler entry keeps them;
     * rt_sigreturn reloads these after the handler may have clobbered them). */
    g_bfree_sig_saved_rbx = g_sig_saved_rbx;
    g_bfree_sig_saved_rbp = g_sig_saved_rbp;
    g_bfree_sig_saved_r12 = g_sig_saved_r12;
    g_bfree_sig_saved_r13 = g_sig_saved_r13;
    g_bfree_sig_saved_r14 = g_sig_saved_r14;
    g_bfree_sig_saved_r15 = g_sig_saved_r15;
    g_bfree_sig_saved_rdx = g_sig_saved_rdx;
    return BFREE_SYSRET_SIGNAL;
}