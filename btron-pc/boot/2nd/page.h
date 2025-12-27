/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/*************************************************************************
 *
 *
 */

#ifndef __PAGE_H__
#define __PAGE_H__	1

#define PAGE_SIZE	4096

#define PAGE_PRESENT	1
#define PAGE_NONPRESENT	0

#define DIR_SIZE	(4096 * 4096)

/* 32-bit page table structures (for compatibility) */
struct page_directory_entry
{
  unsigned int	present:1;
  unsigned int	read_write:1;
  unsigned int	u_and_s:1;
  unsigned int	zero2:2;
  unsigned int	access:1;
  unsigned int	dirty:1;
  unsigned int	zero1:2;
  unsigned int	user:3;
  unsigned int	frame_addr:20;
};

struct page_table_entry
{
  unsigned int	present:1;
  unsigned int	read_write:1;
  unsigned int	u_and_s:1;
  unsigned int	zero2:2;
  unsigned int	access:1;
  unsigned int	dirty:1;
  unsigned int	zero1:2;
  unsigned int	user:3;
  unsigned int	frame_addr:20;
};

/* 64-bit page table structures */
typedef struct {
	UWORD64 present:1;
	UWORD64 read_write:1;
	UWORD64 user_supervisor:1;
	UWORD64 write_through:1;
	UWORD64 cache_disable:1;
	UWORD64 accessed:1;
	UWORD64 dirty:1;
	UWORD64 page_size:1;
	UWORD64 global:1;
	UWORD64 available:3;
	UWORD64 frame_addr:40;
	UWORD64 reserved:12;
} pte64_t;

typedef struct {
	UWORD64 present:1;
	UWORD64 read_write:1;
	UWORD64 user_supervisor:1;
	UWORD64 write_through:1;
	UWORD64 cache_disable:1;
	UWORD64 accessed:1;
	UWORD64 reserved:6;
	UWORD64 page_addr:40;
	UWORD64 reserved2:12;
} pde64_t;

/* Page Map Level 4 (PML4) entry for 64-bit mode */
typedef struct {
	UWORD64 present:1;
	UWORD64 read_write:1;
	UWORD64 user_supervisor:1;
	UWORD64 write_through:1;
	UWORD64 cache_disable:1;
	UWORD64 accessed:1;
	UWORD64 reserved:6;
	UWORD64 page_addr:40;
	UWORD64 reserved2:12;
} pml4e_t;

void	init_vm (void);
void	init_vm64 (void);
int	map_vm (ULONG raddr, ULONG vaddr, ULONG size);
int	map_vm64 (ULONG64 raddr, ULONG64 vaddr, ULONG64 size);
struct page_table_entry *get_page_entry (unsigned long addr);
pte64_t *get_page_entry64 (UWORD64 addr);

unsigned long get_cr0 (void);
unsigned long get_cr2 (void);
unsigned long get_cr3 (void);

#endif /* __PAGE_H__ */

