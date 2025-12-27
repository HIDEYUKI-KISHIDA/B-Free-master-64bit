/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/*************************************************************************
 *
 *		2nd BOOT interrupt routines.
 *
 * $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/cinterrupt.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $
 *
 * $Log: cinterrupt.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.9  2000-01-15 15:11:57  naniwa
 * to follow MINIX/Linux
 *
 * Revision 1.8  1999/03/31 07:57:01  monaka
 * Minor fixes.
 *
 * Revision 1.7  1999/03/15 01:35:14  monaka
 * Minor modify. Function printf was renamed to boot_printf. Some cast was added. and so on.
 *
 * Revision 1.6  1998/11/20 08:02:20  monaka
 * *** empty log message ***
 *
 * Revision 1.5  1996/07/30 18:22:40  night
 * IDE HD �Ѥν������������ӳ����߽������ɲá�
 *
 * Revision 1.4  1996/07/23  17:04:54  night
 * ���󥿡��Х륿�����Ѥγ����߽������ɲá�
 *
 * Revision 1.3  1996/07/06  17:33:08  night
 * �ǥХå��Ѥ� printf ����
 *
 * Revision 1.2  1996/07/06  13:07:34  night
 * �����ߥ���ȥ�����Υ⡼�ɥӥå����� PC �ߴ����Ѥ��ѹ���
 *
 * Revision 1.1  1996/05/11  10:44:59  night
 * 2nd boot (IBM-PC �� B-FREE OS) �Υ�������
 *
 * Revision 1.3  1995/09/21 15:50:35  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.2  1994/07/30  17:37:13  night
 * �ե�����������ܸ�ʸ���򤹤٤� EUC �����ɤ��ѹ���
 *
 * Revision 1.1  1993/10/11  21:28:38  btron
 * btron/386
 *
 * Revision 1.1.1.1  93/01/14  12:30:16  btron
 * BTRON SYSTEM 1.0
 * 
 * Revision 1.1.1.1  93/01/13  16:50:24  btron
 * BTRON SYSTEM 1.0
 * 
 */

static char	rcsid[] = "$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/cinterrupt.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

#include "types.h"
#include "asm.h"
#include "interrupt.h"
#include "lib.h"

/****************************************************************************
 * init_8259A ---
 */
void
init_8259A (void)
{
/* init master 8259A */
  outb (MASTER_8259A_COM, 0x11);
  outb (MASTER_8259A_DATA, 0x20);
  outb (MASTER_8259A_DATA, 0x04);
#ifdef notdef
  outb (MASTER_8259A_DATA, 0x1d);
#else
  outb (MASTER_8259A_DATA, 0x01);
#endif
  
/* init slave 8259A */
  outb (SLAVE_8259A_COM, 0x11);
  outb (SLAVE_8259A_DATA, 0x28);
#ifdef notdef
  outb (SLAVE_8259A_DATA, 0x07);
  outb (SLAVE_8259A_DATA, 0x09);
#else
  outb (SLAVE_8259A_DATA, 0x02);
  outb (SLAVE_8259A_DATA, 0x01);
#endif

/* set mask */
  outb (MASTER_8259A_DATA, 0xfb);	/* 1111 1011 */
  outb (SLAVE_8259A_DATA,  0xff);	/* 1111 1111 */
}  

void
reset_intr_mask (int intn)
{
  lock ();
  if (intn < 8)
    outb (MASTER_8259A_DATA, inb (MASTER_8259A_DATA) & ~(1 << intn));
  else
    outb (SLAVE_8259A_DATA, inb (SLAVE_8259A_DATA) & ~(1 << (intn - 8)));
  unlock ();
}    

/***************************************************************************
 * interrupt --- ���������ߤν���
 */
void
interrupt (int intn)
{
  switch (intn)
    {
    default:
      /* error!! */
      boot_printf ("unknown interrupt from %d\n", intn);
      break;
    case 32:
      break;

    case INT_KEYBOARD:
      intr_keyboard ();
      break;

    case INT_FD:
      intr_fd ();
      break;

    case INT_IDE:
      intr_ide ();
      break;
    case INT_IDE2:
      intr_ide ();
      break;
    }
}

void
trap (int intn)
{
  boot_printf ("trap %d\n", intn);
}

void
fault (int intn)
{
  boot_printf ("fault %d\n", intn);
}

/**************************************************************************
 *
 */
extern int wait_int (int *flag);

