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

64-bit memory management implementation

*/

#include "types.h"
#include "location.h"
#include "config.h"
#include "memory64.h"
#include "lib.h"

/* 64-bit memory state */
UWORD64 total_mem_64 = 0;
UWORD64 available_mem_64 = 0;
UWORD64 kernel_heap_start_64 = KERNEL_ADDR64 + 0x100000;	/* Start at 1MB into kernel space */
UWORD64 kernel_heap_end_64 = KERNEL_ADDR64 + 0x40000000;	/* 1GB heap */

static alloc_entry64_t *alloc_reg_64 = NULL;

#define TRUE_SIZE64(size)	(size + sizeof(alloc_entry64_t))

/*
 * Enable NXE (No-Execute) bit for DEP (Data Execution Prevention)
 */
void
enable_nxe(void)
{
	UWORD64 efer;
	
	/* Read EFER MSR (Extended Feature Enable Register) */
	__asm__ __volatile__ (
		"movl $0xC0000080, %%ecx\n\t"
		"rdmsr\n\t"
		"movq %%rax, %0\n\t"
		: "=r" (efer)
		: : "%eax", "%ecx", "%edx"
	);
	
	/* Set NXE bit */
	efer |= (1 << 11);
	
	/* Write back EFER MSR */
	__asm__ __volatile__ (
		"movl $0xC0000080, %%ecx\n\t"
		"movq %0, %%rax\n\t"
		"wrmsr\n\t"
		: : "r" (efer)
		: "%eax", "%ecx", "%edx"
	);
}

/*
 * Initialize malloc for 64-bit mode
 */
static void
init_malloc64(UWORD64 start, UWORD64 size)
{
	alloc_entry64_t *entry = (alloc_entry64_t *)start;
	
	entry->next = 0;
	entry->size = size - sizeof(alloc_entry64_t);
	alloc_reg_64 = entry;
}

/*
 * Allocate memory in 64-bit mode
 */
void *
malloc64(UWORD64 size)
{
	alloc_entry64_t *current, *prev;
	UWORD64 true_size = TRUE_SIZE64(size);
	
	if (alloc_reg_64 == NULL) {
		return NULL;
	}
	
	current = alloc_reg_64;
	prev = NULL;
	
	while (current != NULL) {
		if (current->size >= true_size) {
			if (current->size == true_size) {
				/* Exact fit */
				if (prev == NULL) {
					alloc_reg_64 = (alloc_entry64_t *)current->next;
				} else {
					prev->next = current->next;
				}
			} else {
				/* Split the block */
				alloc_entry64_t *new_entry;
				new_entry = (alloc_entry64_t *)((UWORD64)current + true_size);
				new_entry->next = current->next;
				new_entry->size = current->size - true_size;
				current->next = (UWORD64)new_entry;
				current->size = size;
			}
			return (void *)current->body;
		}
		prev = current;
		current = (alloc_entry64_t *)current->next;
	}
	
	return NULL;
}

/*
 * Free allocated memory in 64-bit mode
 */
void
free64(void *ptr)
{
	alloc_entry64_t *entry, *current;
	
	if (ptr == NULL) {
		return;
	}
	
	entry = (alloc_entry64_t *)((UWORD64)ptr - sizeof(alloc_entry64_t));
	
	/* Insert back into free list */
	if (alloc_reg_64 == NULL || entry < alloc_reg_64) {
		entry->next = (UWORD64)alloc_reg_64;
		alloc_reg_64 = entry;
	} else {
		current = alloc_reg_64;
		while (current->next != 0 && (alloc_entry64_t *)current->next < entry) {
			current = (alloc_entry64_t *)current->next;
		}
		entry->next = current->next;
		current->next = (UWORD64)entry;
	}
	
	/* Coalesce adjacent free blocks */
	current = alloc_reg_64;
	while (current != NULL && current->next != 0) {
		alloc_entry64_t *next = (alloc_entry64_t *)current->next;
		if ((UWORD64)current + TRUE_SIZE64(current->size) == (UWORD64)next) {
			current->size += TRUE_SIZE64(next->size);
			current->next = next->next;
		} else {
			current = next;
		}
	}
}

/*
 * Initialize 64-bit memory management
 * Called after paging is enabled in 64-bit mode
 */
void
init_memory64(void)
{
	/* Enable NXE for data execution prevention */
	enable_nxe();
	
	/* Initialize heap allocator */
	init_malloc64(kernel_heap_start_64, 
		      kernel_heap_end_64 - kernel_heap_start_64);
	
	/* Mark memory regions from BIOS memory map if available */
	/* This will be populated by the boot loader from INT 0x15 call */
	
	available_mem_64 = kernel_heap_end_64 - kernel_heap_start_64;
}
