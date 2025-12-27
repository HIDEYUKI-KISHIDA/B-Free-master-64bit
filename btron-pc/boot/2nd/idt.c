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
extern void setup_dma(void *a, int b, int c, int d);
extern void lock(void);
extern void unlock(void);

/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/***********************************************************************
 * idt.c
 *
 * $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/idt.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $
 *
 * $Log: idt.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.3  1998-11-20 08:02:30  monaka
 * *** empty log message ***
 *
 * Revision 1.2  1996/07/08 14:52:01  night
 * �ǥХå��Ѥ� printf �� #ifdef DEBUG �ǰϤ����
 *
 * Revision 1.1  1996/05/11  10:45:02  night
 * 2nd boot (IBM-PC �� B-FREE OS) �Υ�������
 *
 * Revision 1.2  1995/09/21 15:50:39  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1993/10/11  21:29:07  btron
 * btron/386
 *
 * Revision 1.1.1.1  93/01/14  12:30:21  btron
 * BTRON SYSTEM 1.0
 * 
 * Revision 1.1.1.1  93/01/13  16:50:30  btron
 * BTRON SYSTEM 1.0
 * 
 */

static char	rcsid[] = "$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/idt.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

#include "types.h"
#include "errno.h"
#include "idt.h"
#include "interrupt.h"

/*********************************************************************
 * init_idt
 */
void
init_idt (void)
{
  int	i;

  for (i = 0; i < MAX_IDT; i++)
    {
      set_idt (i, 0x08, (int)ignore_handler, TRAP_DESC, 0);
    }
}

/*********************************************************************
 * set_idt
 */
int
set_idt (int entry, int selector, int offset, int type, int dpl)
{
  struct idt_t	*table;
  
  table = (struct idt_t *)IDT_TABLE_ADDR;
  SET_OFFSET_IDT (table[entry], offset);
  table[entry].p = 1;
  table[entry].selector = selector;
  table[entry].dpl = dpl;
  table[entry].type = type;
  table[entry].dt0 = 0;
  table[entry].zero = 0;
#ifdef nodef
  printf ("set_idt: entry = %d, selector = %d, type = %d\n",
	  entry, selector, type);
  tmp = (ULONG *)&table[entry];
  printf ("idt[%d] = 0x%x, 0x%x\n", entry, tmp[0], tmp[1]);
#endif

  return E_OK;
}


/*********************************************************************
 *	get_idt
 */
struct idt_t *
get_idt (int entry)
{
  struct idt_t *table;

  table = (struct idt_t *)IDT_TABLE_ADDR;
  return (&table[entry]);
}
