/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* 
  This file is part of BTRON/386

  $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/i386/i386.h,v 1.1 2011/12/27 17:13:35 liu1 Exp $


*/

#ifndef _I386_H_
#define _I386_H_	1

#include "../h/types.h"

#define I386	1

#define I386_PAGESIZE	4096


/***********************************************************************
 *	directory table entry.
 */
typedef struct
{
  UW	present:1;
  UW	read_write:1;
  UW	u_and_s:1;
  UW	zero2:2;
  UW	access:1;		/* �������������å��ѥӥå� */
  UW	dirty:1;		/* �񤭹��ߥ����å��ѥӥå� */
  UW	zero1:2;
  UW	user:3;
  UW	frame_addr:20;
} I386_DIRECTORY_ENTRY;

/**********************************************************************
 * ���ɥ쥹�ޥåס�������
 *
 * ���η��ϡ�80386 �Υǥ��쥯�ȥ�ơ��֥륨��ȥ�� 1024 �¤٤Ƥ��롣
 * �������󤬡����ۥ���ޥåץơ��֥�Ȥʤ롣
 *
 */
typedef I386_DIRECTORY_ENTRY	*ADDR_MAP;


/***********************************************************************
 *	page table entry.
 */
typedef struct
{
  UW	present:1;
  UW	read_write:1;
  UW	u_and_s:1;
  UW	zero2:2;
  UW	access:1;
  UW	dirty:1;
  UW	zero1:2;
  UW	user:3;
  UW	frame_addr:20;
} I386_PAGE_ENTRY;

/****************************************************************************
 * T_I386_CONTEXT --- Task State Segment
 *
 */
typedef struct 
{
  UW		backlink;
  UW		esp0;
  UW		ss0;
  UW		esp1;
  UW		ss1;
  UW		esp2;
  UW		ss2;

  UW		cr3;
  UW		eip;
  UW		eflags;
  UW		eax;
  UW		ecx;
  UW		edx;
  UW		ebx;
  UW		esp;
  UW		ebp;
  UW		esi;
  UW		edi;
  UW		es;
  UW		cs;
  UW		ss;
  UW		ds;
  UW		fs;
  UW		gs;
  UW		ldtr;
  UH		t:1;
  UH		zero:15;
  UH		iobitmap;
} T_I386_CONTEXT;

/**************************************************************
 *
 *	�ǥ�������ץ�(���̷�)
 *
 */
typedef struct
{
  UH	limit0:16;	/*  104 */
  UH	base0:16;	/* �������١��� 0..15		*/
  UH	base1:8;	/* �������١��� 16..23		*/
  UH	type:4;
  UH 	zero0:1;	/* ̤���� (0)			*/
  UH	dpl:2;		/* �ǥ�������ץ�		*/
  UH	present:1;	/* �ǥ�������ץ� present bit	*/
  UH	limit1:4;
  UH	zero1:2;	/* ̤���� (0)			*/
  UH	d:1;
  UH	g:1;		/* γ�� (1 = 4K �Х���ñ��)	*/
  UH	base2:8;	/* �������١��� 24..31		*/
} GEN_DESC;


/**************************************************************
 *
 *	�������ǥ�������ץ�
 *
 */
typedef struct
{
  UH	limit0:16;	/*  104 */
  UH	base0:16;	/* �������١��� 0..15		*/
  UH	base1:8;	/* �������١��� 16..23		*/
  UH	type:4;
  UH 	zero0:1;	/* ̤���� (0)			*/
  UH	dpl:2;		/* �ǥ�������ץ�		*/
  UH	present:1;	/* �ǥ�������ץ� present bit	*/
  UH	limit1:4;
  UH	zero1:2;	/* ̤���� (0)			*/
  UH	d:1;
  UH	g:1;		/* γ�� (1 = 4K �Х���ñ��)	*/
  UH	base2:8;	/* �������١��� 24..31		*/
} TASK_DESC;



#define GET_TSS_ADDR(x)	((x).base0 | (x).base1 << 16 | (x).base2 << 24)
#define GET_TSS_LIMIT(x)	((x).limit0 | (x).limit1 << 16)
#define SET_TSS_ADDR(x,addr)	{ \
   (x).base0 = (UH)((UW)(addr) & 0x00ffff); \
   (x).base1 = (UH)((UW)(addr) >> 16 & 0x00ff); \
   (x).base2 = (UH)((UW)(addr) >> 24 & 0x00ff); }
#define SET_TSS_LIMIT(x,limit)	{\
   (x).limit0 = limit & 0x0000ffff; \
   (x).limit1 = limit >> 16 & 0x0f; }

#define STACK_DIR	SMALL
#define MAXINT		(0x7fffffff)

#define TSS_DESC	9
#define TSS_BASE	128		/* TSS �� GDT ��Ǥΰ��� */

#define ACC_KERNEL	0
#define ACC_USER	1

#define EFLAG_IBIT	0x0200

#define ADDR_MAP_SIZE	1024


/* low-level control register helpers (defined in locore.S) */
extern UW get_cr0 (void);
extern UW get_cr2 (void);
extern UW get_cr3 (void);
extern void set_cr3 (UW value);
extern void load_task_register (UW selector);
extern void flush_tlb (void);



#endif /* _I386_H_ */
