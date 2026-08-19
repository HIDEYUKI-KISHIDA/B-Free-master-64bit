/* PID1 on the stub ISO (registered as init.elf). INIT role can exec_initrd.
 * That transfer sets APP, so compositor.elf can vfork + pipe. Do not rebuild
 * the daily kernel. Do not exec the client from here (that would replace us
 * before the server exists).
 */
static long sys6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    register long rax __asm__("rax") = n;
    register long rdi __asm__("rdi") = a1;
    register long rsi __asm__("rsi") = a2;
    register long rdx __asm__("rdx") = a3;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("syscall"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return rax;
}

void _start(void)
{
    static const char msg[] = "[init] exec compositor.elf\n";
    static const char path[] = "compositor.elf";
    (void)sys6(24, (long)msg, (long)(sizeof(msg) - 1), 0, 0, 0, 0);
    (void)sys6(41, (long)path, 0, 0, 0, 0, 0);
    for (;;) {
    }
}
