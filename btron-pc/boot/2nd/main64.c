#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
#define NULL ((void*)0)
#define __attribute__(x)
#define __asm__(x)
#define __volatile__
#define __restrict
#define __inline__
#define __extension__
#define __builtin_va_list int
#define __builtin_va_start(a,b)
#define __builtin_va_end(a)
#define __builtin_va_arg(a,b) (0)
#define __builtin_offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
/*

B-Free Project - GNU Generic PUBLIC LICENSE

64-bit kernel main entry point

*/

#include "types.h"
#include "location.h"
#include "memory64.h"
#include "page.h"
#include "gdt_idt_64.h"
#include "lib.h"
#include "console.h"

/* Forward declarations */
extern void init_vm64(void);
extern void setup_gdt64(void);
extern void setup_idt64(void);
extern int init_main(void);

/*
 * 64-bit kernel main entry point
 * Called by start64.S after 64-bit mode is enabled
 */
int
_main64(void)
{
	/* Clear interrupts */
	__asm__ __volatile__("cli");
	
	/* Initialize console for output */
	console_init();
	
	boot_printf("=== B-Free OS 64-bit Kernel ===\n");
	boot_printf("Initializing 64-bit mode...\n");
	
	/* Setup GDT for 64-bit mode */
	boot_printf("Setting up GDT...\n");
	setup_gdt64();
	
	/* Setup IDT for exception handling */
	boot_printf("Setting up IDT...\n");
	setup_idt64();
	
	/* Initialize memory management */
	boot_printf("Initializing memory management...\n");
	init_memory64();
	
	/* Initialize virtual memory / paging */
	boot_printf("Initializing paging...\n");
	init_vm64();
	
	boot_printf("64-bit kernel initialization complete!\n");
	boot_printf("\n");
	
	/* Start system init (mount root, start shell) */
	init_main();
	
	/* After shell exits, halt */
	boot_printf("Kernel halting...\n");
	for (;;) {
		__asm__ __volatile__("hlt");
	}
	
	return 0;
}

/*
 * Default exception handler for 64-bit mode
 */
void
default_exception_handler_64(UWORD64 vector, UWORD64 error_code)
{
	boot_printf("Exception %lld (error code: %llx)\n", vector, error_code);
	
	/* Halt system */
	for (;;) {
		__asm__ __volatile__("hlt");
	}
}
