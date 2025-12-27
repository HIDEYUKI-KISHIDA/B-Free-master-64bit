/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/*************************************************************************
 *
 *		2nd BOOT floppy driver routines.
 *
 * $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/kernlib/dma.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $
 *
 * $Log: dma.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.5  2000-02-29 14:07:56  naniwa
 * minor fix
 *
 * Revision 1.4  2000/02/27 15:31:29  naniwa
 * minor change
 *
 * Revision 1.3  1999/11/30 16:46:54  naniwa
 * minor fix
 *
 * Revision 1.2  1999/11/19 10:15:53  naniwa
 * minor fix
 *
 * Revision 1.1  1999/04/18 17:48:33  monaka
 * Port-manager and libkernel.a is moved to ITRON. I guess it is reasonable. At least they should not be in BTRON/.
 *
 * Revision 1.4  1999/03/29 07:07:51  monaka
 * Modified some casts
 *
 * Revision 1.3  1997/10/18 12:41:49  night
 * DMA �Υ⡼�ɤ����ꤹ��Ȥ��ˡ�����ͥ��ֹ����ꤷ�Ƥ��ʤ��ä���
 * �����ͥ��ֹ����ꤹ��褦�ѹ���
 *
 * Revision 1.2  1996/07/28  19:56:35  night
 * IBM PC �Υϡ��ɥ������˹�碌�ƽ������ѹ���
 *
 * Revision 1.1  1996/07/22  23:52:05  night
 * �ǽ����Ͽ
 *
 * Revision 1.3  1995/10/01  13:03:56  night
 * setup_dma () �δؿ����󥿥ե��������ѹ���
 * DMA ����ͥ��ֹ���������ǻ���Ǥ���褦�ˤ�����
 *
 * Revision 1.2  1995/09/21  15:51:40  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1995/09/14  04:38:09  night
 * �ǽ����Ͽ
 *
 * Revision 1.1  1993/10/11  21:28:47  btron
 * btron/386
 *
 * Revision 1.1.1.1  93/01/14  12:30:18  btron
 * BTRON SYSTEM 1.0
 * 
 * Revision 1.1.1.1  93/01/13  16:50:34  btron
 * BTRON SYSTEM 1.0
 * 
 */

static char	rcsid[] = "$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/kernlib/dma.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

#include "types.h"
#include "dma.h"
#include "../h/itron_misc_func.h"


/****************************************************************************
 *
 */
int
init_dma (void)
{
}

/****************************************************************************
 * setup_dma --- �ģͣ��������Ԥ�
 *
 */
int
setup_dma (UW chan, VP addr, int mode, int length, int mask)
{
  unsigned long	p;
  static int	dma_init_flag = 0;
  
  length -= 1;
  p = (unsigned long)addr;
#ifdef DMADEBUG
  dbg_printf ("setup_dma: mode = 0x%x, addr = 0x%x, length = %d, mask = 0x%x\n",
	  mode, addr, length, mask);
#endif

#ifdef notdef
  if (!dma_init_flag)
    {
      dma_init_flag = 1;
      outb(0x439, (inb(0x439) & 0xfb)); /* DMA Accsess Control over 1MB */
      outb(0x29, (0x0c | 0));	/* Bank Mode Reg. 16M mode */
      outb(0x29, (0x0c | 1));	/* Bank Mode Reg. 16M mode */
      outb(0x29, (0x0c | 2));	/* Bank Mode Reg. 16M mode */
      outb(0x29, (0x0c | 3));	/* Bank Mode Reg. 16M mode */
#if 1 /* rotate priority mode */
      outb(0x11, 0x50);	/* PC98 must be 0x40 */
#else /* fixed priority mode */
      outb(0x11, 0x40);	/* PC98 must be 0x40 */
#endif
  }
  outb (DMA_WRITE_MODE, mode);
  outb (DMA_CLEAR_BYTE, mode);
#endif
  if (chan == 2)
    {
      /* fdc �� dis_int ���Ƥ���֤˸ƤӽФ���롣���Τ��ᤳ���Ǥ�
	 dis_int/ena_int �ϹԤ�ʤ� */
      outb (DMA_WRITE_SINGLE_MASK, 0x06);/* DMAC �Υꥻ�å� */
      outb (DMA_CLEAR_BYTE, 0);		 /* flip flop �ؽ񤭹��� */
#if 1
      outb (DMA_WRITE_MODE, mode | mask);
#else
      outb (DMA_WRITE_MODE, mode);
      /* outb (DMA_CLEAR_BYTE, mode);*/
#endif
      outb (DMA_CHANNEL2_ADDR, p & 0xff);
      outb (DMA_CHANNEL2_ADDR, (p >> 8) & 0xff);
      outb (DMA_CHANNEL2_BANK, (p >> 16) & 0xff);
      outb (DMA_CHANNEL2_COUNT, (length) & 0xff);
      outb (DMA_CHANNEL2_COUNT, ((length) >> 8) & 0xff);
      outb (DMA_WRITE_SINGLE_MASK, mask);
    }
}

