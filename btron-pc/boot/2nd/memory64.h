/*

B-Free Project - GNU Generic PUBLIC LICENSE

64-bit memory management routines

*/

/*
 * memory64.h - 64-bit memory management header
 */

#ifndef __MEMORY64_H__
#define __MEMORY64_H__

#include "types.h"

/* 64-bit memory management structures */
typedef struct {
	UWORD64 next;
	UWORD64 size;
	BYTE body[0];
} alloc_entry64_t;

/* Memory regions */
typedef struct {
	UWORD64 base;
	UWORD64 length;
	UWORD32 type;		/* 1=usable, 2=reserved, 3=ACPI, 4=NVS */
	UWORD32 extended_attr;
} memory_region64_t;

extern UWORD64 total_mem_64;
extern UWORD64 available_mem_64;
extern UWORD64 kernel_heap_start_64;
extern UWORD64 kernel_heap_end_64;

void	init_memory64(void);
void	*malloc64(UWORD64 size);
void	free64(void *ptr);
void	init_malloc64(UWORD64 start, UWORD64 size);
void	enable_nxe(void);		/* Enable No-Execute bit */

#endif /* __MEMORY64_H__ */
