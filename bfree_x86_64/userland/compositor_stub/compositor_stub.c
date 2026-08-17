/* Freestanding guest compositor stub. Not Linux tron_gui_server.
 * COMPOSITOR role: native syscall 24 = sys_debug_serial_write(ptr, len).
 * Do not use Linux write(1). Do not set BFREE_BOOT_GUI_FIRST on the daily kernel.
 */
void _start(void)
{
    const char msg[] = "[compositor] guest stub hello\n";
    register long rax __asm__("rax") = 24;
    register long rdi __asm__("rdi") = (long)msg;
    register long rsi __asm__("rsi") = (long)(sizeof(msg) - 1);
    __asm__ volatile("syscall"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi)
                     : "rcx", "r11", "memory");
    for (;;) {
    }
}
