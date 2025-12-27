// --- クロスビルド用ダミー定義 ---
#ifndef E_OK
#define E_OK 0
#endif
#ifndef E_DEV
#define E_DEV -3
#endif
#ifndef DMA_READ
#define DMA_READ 0
#endif
#ifndef DMA_MASK
#define DMA_MASK 0
#endif
#ifndef ROUNDUP
#define ROUNDUP(x,align) (((x + (align - 1)) / align) * align)
#endif
extern void busywait(int x);
extern void wait_int(int *x);
extern void setup_dma(void *a, int b, int c, int d);
extern void lock(void);
extern void unlock(void);

/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/*************************************************************************
 *
 *		2nd BOOT memory allocate/deallocate routine.
 *
 * $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/memory.c,v 1.2 2011/12/30 00:57:06 liu1 Exp $
 *
 * $Log: memory.c,v $
 * Revision 1.2  2011/12/30 00:57:06  liu1
 * コンパイルエラーの修正。
 *
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.6  1999-03-15 01:35:30  monaka
 * Minor modify. Function printf was renamed to boot_printf. Some cast was added. and so on.
 *
 * Revision 1.5  1998/11/20 08:02:36  monaka
 * *** empty log message ***
 *
 * Revision 1.4  1996/08/12 19:34:42  night
 * ʪ�����ꥵ����������å�����Ȥ��κ��祵������ 16MB ���� 256 MB ���ѹ�������
 *
 * Revision 1.3  1996/07/24  14:02:13  night
 * ;ʬ�� print ʸ������
 *
 * Revision 1.2  1996/07/22  13:35:08  night
 * A20 �򥤥͡��֥�ˤ���������ɲá�
 *
 * Revision 1.1  1996/05/11  10:45:05  night
 * 2nd boot (IBM-PC �� B-FREE OS) �Υ�������
 *
 * Revision 1.5  1995/09/21  15:50:41  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.4  1995/09/20  15:32:20  night
 * malloc �ѤΥ���� 640K - 100K ʬ���ѹ���
 *
 * Revision 1.3  1995/06/28  14:11:07  night
 * ���ꥢ�������Ȥδ����ΰ�� (640K - 100K) �� 640K ���� (2M - 100K) ��
 * 2M ���ѹ�������
 *
 * Revision 1.2  1995/06/26  15:06:12  night
 * malloc �ؿ����ɲá�
 *
 * Revision 1.1  1993/10/11  21:29:33  btron
 * btron/386
 *
 * Revision 1.1.1.1  93/01/14  12:30:24  btron
 * BTRON SYSTEM 1.0
 * 
 * Revision 1.1.1.1  93/01/13  16:50:27  btron
 * BTRON SYSTEM 1.0
 * 
 */

static char	rcsid[] = "$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/memory.c,v 1.2 2011/12/30 00:57:06 liu1 Exp $";

#include "types.h"
#include "location.h"
#include "config.h"
#include "memory.h"
#include "asm.h"

struct alloc_entry
{
  struct alloc_entry	*next;
  int size;
  BYTE body[0];
};

static struct alloc_entry	*alloc_reg;


extern int	end;

static void	init_malloc (void *last, int size);
void		*last_addr;
UWORD32	base_mem, ext_mem, real_mem;

#define TRUE_SIZE(size)	(size + sizeof (struct alloc_entry))

static void
enable_A20 ()
{
  for (;;)
    {
      if ((inb (0x64) & 0x02) == 0)
	break;
    }
  outb (0x64, 0xD1);
  for (;;)
    {
      if ((inb (0x64) & 0x02) == 0)
	break;
    }
  outb (0x60, 0xdf);
}


/**************************************************************************
 * init_memory
 *
 *
 */
void
init_memory (void)
{
  volatile int	*p;

  last_addr = (void *)&end;
#ifdef	PC9801
  outb (0x00f2, 0);	/* 1M �ʾ���ΰ����ѤǤ���褦�ˤ��롣*/
#elif IBMPC
  /* IBMPC (�ߴ���) �� A20 �򥤥͡��֥�ˤ��롣
   * 0xD1 -> out (0x64)
   * 0xDF -> out (0x60)
   */
  enable_A20 ();
#endif
  for (p = (int *)0x100000; (int)p < 0xf000000; p = (int *)((int)p + 0x100000))
    {
      *p = 0;
      *p = 0xAA;
      if (*p != 0xAA)
	break;
    }
  boot_printf ("Extended Memory = %d K bytes\n", ((int)p - 0x100000) / 1000);
  boot_printf ("USE Memory      = %d bytes\n", last_addr);

  ext_mem = ((int)p - 0x100000);
  base_mem = BASE_MEM;
  real_mem = ext_mem + BASE_MEM;

#ifdef nodef
  /* malloc �����ν���� */
  init_malloc ((void *)(2 * 1024 * 1024), MALLOC_SIZE);
#endif
  /* 640K �Х��Ȥ��鲼�� 100 K �Х��Ȥ��ΰ�� malloc �Ѥ˻��Ѥ��� */
  init_malloc ((void *)(640 * 1024), MALLOC_SIZE);
}

/*
 * boot �ǥ������Ѥ��뵡���ν������
 *
 * �裱�����ǻ��ꤷ������κǸ夫�顢�裲�����ǻ��ꤷ��ʬ������ 
 * malloc �ǻ��Ѥ��롣
 *
 */
static void
init_malloc (void *last, int size)
{
  alloc_reg = (struct alloc_entry *)(last - size);
  alloc_reg->size = size;
  alloc_reg->next = alloc_reg;
#ifdef nodef
  boot_printf ("init_malloc: last = 0x%x\n", last);
  boot_printf ("init_malloc: alloc_reg = 0x%x\n", alloc_reg);
  boot_printf ("init_malloc: alloc_reg->size = %d\n", alloc_reg->size);
  boot_printf ("init_malloc: alloc_reg->next = 0x%x\n", alloc_reg->next);
#endif
}

/*
 * malloc --- ���ꤷ���������Υ�����������
 *
 * alloc_reg �ˤϡ����������ȤǤ���ե꡼����Υ���ȥ꤬�Ĥʤ��ä�
 * ���롣�������椫��ǽ�˥��������ȤǤ��륵�������ä���Τ���
 * ������
 */
void *
malloc (int size)
{
  int			true_size;	/* �������ΰ��ޤ��"����"���������� */
					/* ������ */
  struct alloc_entry	*p, *prev;
  struct alloc_entry	*alloced;


  if (alloc_reg == NULL)	/* �������٤��ե꡼���꤬�ʤ� */
    {
      return (NULL);
    }

  true_size = TRUE_SIZE (size);

/*  boot_printf ("alloc_reg = 0x%x, alloc_reg->next = 0x%x\n", alloc_reg, */
/*  alloc_reg->next); */

  for (prev = alloc_reg, p = alloc_reg->next;
       p->size < true_size;
       prev = p, p = p->next)
    {
      if (p == alloc_reg)	/* �����Ǥ��륨��ȥ꤬�ʤ��ä� */
	{
	  return (NULL);
	}
    }

  if (p->size == true_size)	/* ���٤Υ��������ä� */
    {
      if (p->next == p)	/* �Ĥʤ��äƤ��륨��ȥ�ϤҤȤĤ������ä� */
	{
	  alloc_reg = NULL;
	  p->next = NULL;
	}
      else
	{
	  /* �ե꡼�ꥹ�Ȥ��� p �ǻ��ꤵ��Ƥ��륨��ȥ�򳰤� */
	  prev->next = p->next;
	}
      
      return ((void *)(p->body));	/* ������������ؤΥݥ��� */
					/* ���֤������������ΰ�ϡ� */
					/* alloc_reg ��¤�Τ� body �� */
					/* �ǤǤ��뤳�Ȥ����� */
    }

  alloced = (struct alloc_entry *)(((char *)p) + (p->size - true_size));
  p->size -= true_size;
  alloced->size = true_size;
  return (alloced->body);
}

/*
 * �������Ϥ��줿�ݥ��󥿤��ؤ��ΰ��ե꡼�ꥹ�Ȥ�����롣
 * �⤷���ΰ褬�����ե꡼�ꥹ�Ȥˤʤ���Τ��ä��顢�ʤˤ⤷�ʤ���
 */
void
free (void *ptr)
{
  struct alloc_entry	*current, *prev;
  struct alloc_entry	*new_entry;

  new_entry = (struct alloc_entry *)((char *)ptr - sizeof (struct alloc_entry));

  /* ��������ꤵ�줿�ΰ�� malloc �Ǵ������Ƥ����ΰ�ǤϤʤ�. */
  if (((char *)new_entry < (char *)(void *)ext_mem 
                            + (1024 * 1024) - MALLOC_SIZE) 
      || ((char *)new_entry > (char *)(void *)ext_mem + (1024 * 1024)))
    {
      return;
    }

  /* �ե꡼�ꥹ�Ȥ˥���ȥ�ϤҤȤĤ����ʤ� */
  if (alloc_reg == alloc_reg->next)
    {
      if (((char *)new_entry + new_entry->size) == (char *)alloc_reg)
	{
	  new_entry->size += alloc_reg->size;
	  new_entry->next = new_entry;
	  alloc_reg = new_entry;
	}
      else if (((char *)alloc_reg + alloc_reg->size) == (char *)new_entry)
	{
	  alloc_reg->size += new_entry->size;
	}
      else
	{
	  alloc_reg->next = new_entry;
	  new_entry->next = alloc_reg;
	}
      return;
    }

  /* �ե꡼�ꥹ�Ȥ���Ƭ����é�ꡢ�ꥹ�Ȥ���������ݥ���Ȥ���� */
  /* �ե꡼�ꥹ�Ȥϥ��ɥ쥹��ˤʤäƤ��ꡢcurrent �Υ��ɥ쥹���ɲä� */
  /* ���ΰ�κǸ�����礭���ʤä��顢���������������롣*/
  for (prev = alloc_reg, current = alloc_reg->next;
       current != alloc_reg;
       prev = current, current = current->next)
    {
      /* current �����ܤ��Ƥ��롣current ���ΰ��Ĥʤ��� */
      if ((char *)current == ((char *)new_entry + new_entry->size))
	{
	  new_entry->size += current->size;
	  new_entry->next = current->next;
	  current = new_entry;
	  prev->next = current;
	}
      else
	{
	  /* current �ˤ����ܤ��Ƥ��ʤ����ꥹ�Ȥ��������� */
	  new_entry->next = current;
	  prev->next = new_entry;
	}
      
      /* prev �����ܤ��Ƥ����硣prev �Υ����������䤹��*/
      /* ���ΤȤ���current ��ʻ�礷�Ƥ��褦����ñ���������Ƥ��褦���� */
      /* �ɤ���ˤ��Ƥ� prev->next �ϡ�new_entry��ؤ��Ƥ��뤳�Ȥ����� */
      if ((char *)new_entry == ((char *)prev + prev->size))
	{
	  prev->size += new_entry->size;
	  prev->next = new_entry->next;
	}
    }
}
