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

64-bit GDT and IDT setup

*/

#include "types.h"
#include "location.h"
#include "gdt_idt_64.h"
#include "lib.h"

/* GDT table (64-bit) */
static gdt_descriptor_t gdt64[GDT_SIZE];

/* IDT table (64-bit) */
static idt_gate_64_t idt64[256];

/* TSS for ring 0 */
static tss_t kernel_tss;

/*
 * Setup Global Descriptor Table for 64-bit mode
 */
void
setup_gdt64(void)
{
	tss_descriptor_64_t *tss_desc;
	UWORD64 tss_addr;
	
	/* Clear GDT */
	bzero((char *)gdt64, sizeof(gdt64));
	
	/* Null descriptor */
	gdt64[GDT_NULL].limit_low = 0;
	gdt64[GDT_NULL].base_low = 0;
	
	/* 64-bit Code Segment */
	gdt64[GDT_CODE64].limit_low = 0xFFFF;
	gdt64[GDT_CODE64].limit_high_attr2 = (0x0F | (DESC_G | DESC_L | DESC_P | DESC_S | DESC_EXEC | DESC_WRITE));
	gdt64[GDT_CODE64].attr1 = (DESC_P | DESC_S | DESC_EXEC | DESC_READ);
	gdt64[GDT_CODE64].base_low = 0;
	gdt64[GDT_CODE64].base_mid = 0;
	gdt64[GDT_CODE64].base_high = 0;
	
	/* 64-bit Data Segment */
	gdt64[GDT_DATA64].limit_low = 0xFFFF;
	gdt64[GDT_DATA64].limit_high_attr2 = (0x0F | (DESC_G | DESC_P | DESC_S | DESC_WRITE));
	gdt64[GDT_DATA64].attr1 = (DESC_P | DESC_S | DESC_WRITE);
	gdt64[GDT_DATA64].base_low = 0;
	gdt64[GDT_DATA64].base_mid = 0;
	gdt64[GDT_DATA64].base_high = 0;
	
	/* Initialize TSS */
	bzero((char *)&kernel_tss, sizeof(kernel_tss));
	kernel_tss.rsp0 = 0x00080000;	/* Ring 0 stack pointer */
	
	/* TSS Descriptor */
	tss_addr = (UWORD64)&kernel_tss;
	tss_desc = (tss_descriptor_64_t *)&gdt64[GDT_TSS];
	
	tss_desc->limit_low = sizeof(kernel_tss) - 1;
	tss_desc->base_low = (UWORD16)(tss_addr & 0xFFFF);
	tss_desc->base_mid = (BYTE)((tss_addr >> 16) & 0xFF);
	tss_desc->attr1 = 0x89;		/* TSS descriptor, present, DPL=0 */
	tss_desc->limit_high_attr2 = 0x00;
	tss_desc->base_high = (BYTE)((tss_addr >> 24) & 0xFF);
	tss_desc->base_upper = (UWORD32)((tss_addr >> 32) & 0xFFFFFFFF);
	tss_desc->reserved = 0;
	
	/* Load GDT */
	load_gdt64(gdt64, sizeof(gdt64) - 1);
}

/*
 * Setup Interrupt Descriptor Table for 64-bit mode
 */
void
setup_idt64(void)
{
	int i;
	UWORD64 handler_addr;
	
	/* Clear IDT */
	bzero((char *)idt64, sizeof(idt64));
	
	/* Fill IDT entries with default exception handler */
	for (i = 0; i < 256; i++) {
		/* Default to interrupt gate pointing to common handler */
		handler_addr = (UWORD64)&ignore_handler64;
		
		idt64[i].offset_low = (UWORD16)(handler_addr & 0xFFFF);
		idt64[i].offset_mid = (UWORD16)((handler_addr >> 16) & 0xFFFF);
		idt64[i].offset_high = (UWORD32)((handler_addr >> 32) & 0xFFFFFFFF);
		
		idt64[i].selector = (GDT_CODE64 << 3);	/* Code segment selector */
		idt64[i].ist = 0;			/* IST = 0 (use RSP0) */
		idt64[i].type_attr = 0x8E;		/* Interrupt gate, present, DPL=0 */
		idt64[i].reserved = 0;
	}
	
	/* Setup specific exception handlers */
	setup_exception_handlers_64();
	
	/* Load IDT */
	load_idt64(idt64, sizeof(idt64) - 1);
}

/*
 * Load GDT (assembly code to be called)
 * For use with lgdt instruction
 */
void
load_gdt64(gdt_descriptor_t *gdt, UWORD16 limit)
{
	struct {
		UWORD16 limit;
		UWORD64 base;
	} __attribute__((packed)) gdtr;
	
	gdtr.limit = limit;
	gdtr.base = (UWORD64)gdt;
	
	__asm__ __volatile__ (
		"lgdt %0\n\t"
		"movq $0x10, %%rax\n\t"		/* Load data segment (GDT_DATA64 << 3) */
		"movq %%rax, %%ds\n\t"
		"movq %%rax, %%es\n\t"
		"movq %%rax, %%fs\n\t"
		"movq %%rax, %%gs\n\t"
		"movq %%rax, %%ss\n\t"
		"pushq $0x8\n\t"			/* Push code segment (GDT_CODE64 << 3) */
		"pushq $1f\n\t"
		"lretq\n\t"
		"1:\n\t"
		: : "m" (gdtr)
		: "%rax", "memory"
	);
}

/*
 * Load IDT (assembly code to be called)
 * For use with lidt instruction
 */
void
load_idt64(idt_gate_64_t *idt, UWORD16 limit)
{
	struct {
		UWORD16 limit;
		UWORD64 base;
	} __attribute__((packed)) idtr;
	
	idtr.limit = limit;
	idtr.base = (UWORD64)idt;
	
	__asm__ __volatile__ (
		"lidt %0\n\t"
		: : "m" (idtr)
	);
}

/*
 * Setup specific exception handlers
 * This would be called to register actual exception handlers
 * For now, all exceptions point to a default handler
 */
void
setup_exception_handlers_64(void)
{
	extern void de_handler64(void), gp_handler64(void), pf_handler64(void);
	UWORD64 de_addr = (UWORD64)&de_handler64;
	UWORD64 gp_addr = (UWORD64)&gp_handler64;
	UWORD64 pf_addr = (UWORD64)&pf_handler64;
	// #DE (Divide Error)
	idt64[0].offset_low = (UWORD16)(de_addr & 0xFFFF);
	idt64[0].offset_mid = (UWORD16)((de_addr >> 16) & 0xFFFF);
	idt64[0].offset_high = (UWORD32)((de_addr >> 32) & 0xFFFFFFFF);
	// #GP (General Protection Fault)
	idt64[13].offset_low = (UWORD16)(gp_addr & 0xFFFF);
	idt64[13].offset_mid = (UWORD16)((gp_addr >> 16) & 0xFFFF);
	idt64[13].offset_high = (UWORD32)((gp_addr >> 32) & 0xFFFFFFFF);
	// #PF (Page Fault)
	idt64[14].offset_low = (UWORD16)(pf_addr & 0xFFFF);
	idt64[14].offset_mid = (UWORD16)((pf_addr >> 16) & 0xFFFF);
	idt64[14].offset_high = (UWORD32)((pf_addr >> 32) & 0xFFFFFFFF);
	// 他の例外も同様に追加可能
}

/*
 * Stub for exception handler (defined in assembly)
 * This would be implemented in start64.S
 */
extern void ignore_handler64(void);
