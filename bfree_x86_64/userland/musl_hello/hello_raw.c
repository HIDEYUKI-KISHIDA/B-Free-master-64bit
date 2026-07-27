/* Freestanding hello — avoids musl TLS bootstrap; still exercises guest exec. */
void _start(void)
{
    const char msg[] = "B2_MUSL_HELLO_OK\n";
    long n = (long)(sizeof(msg) - 1);
    register long rax __asm__("rax") = 1; /* write */
    register long rdi __asm__("rdi") = 1;
    register long rsi __asm__("rsi") = (long)msg;
    register long rdx __asm__("rdx") = n;
    __asm__ volatile("syscall" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "rcx", "r11", "memory");
    rax = 231; /* exit_group */
    rdi = 0;
    __asm__ volatile("syscall" : : "r"(rax), "r"(rdi) : "rcx", "r11", "memory");
    for (;;) {
    }
}
