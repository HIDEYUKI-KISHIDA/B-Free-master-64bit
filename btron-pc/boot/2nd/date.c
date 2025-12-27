#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
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
#include "lib.h" // 代表的な関数はlib.cに集約
#endif
/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/*************************************************************************
 *
 *		2nd BOOT main routine.
 *
 * $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/date.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $
 *
 * $Log: date.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.3  1999-03-15 01:35:18  monaka
 * Minor modify. Function printf was renamed to boot_printf. Some cast was added. and so on.
 *
 * Revision 1.2  1998/11/20 08:02:23  monaka
 * *** empty log message ***
 *
 * Revision 1.1  1996/05/11 10:45:00  night
 * 2nd boot (IBM-PC �� B-FREE OS) �Υ�������
 *
 * Revision 1.2  1995/09/21  15:50:36  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1993/10/11  21:28:43  btron
 * btron/386
 *
 * Revision 1.1.1.1  93/01/14  12:30:17  btron
 * BTRON SYSTEM 1.0
 * 
 * Revision 1.1.1.1  93/01/13  16:50:30  btron
 * BTRON SYSTEM 1.0
 * 
 */

static char	rcsid[] = "$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/date.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

#include "types.h"
#include "location.h"
#include "date.h"
#include "lib.h"
#include "asm.h"

static int	bcd_to_bin (int bcd);
 
extern void busywait(int x);

void
init_clock (void)
{
  /* no work */
}

int
read_clock (struct date_t *date)
{
  BYTE data[6];		/* 40 bit */
  int i,j;
  int tmp;
  
  outb (0x20, 0x03);
  busywait (2);
  outb (0x20, 0x0b);
  busywait (4);
  outb (0x20, 0x03);
  busywait (20);

  outb (0x20, 0x01);
  busywait (2);
  outb (0x20, 0x09);
  busywait (4);
  outb (0x20, 0x01);
  busywait (40);

  for (i = 0; i < 6; i++)
    {
      data[i] = 0;
      for (j = 0; j < 8; j++)
	{
	  tmp = 0;
	  tmp = inb (0x33);
	  data[i] |= ((tmp & 0x1) << j);
	  outb (0x20, 0x11);
	  busywait (2);
	  outb (0x20, 0x01);
	  busywait (2);
	}
    }
  date->year = bcd_to_bin (data[5]);
  date->month = 0;
  date->day = bcd_to_bin (data[3]);
  date->week = 0;
  date->hour = bcd_to_bin (data[2]);
  date->minute = bcd_to_bin (data[1]);
  date->second = bcd_to_bin (data[0]);
  boot_printf ("%d%d:%d%d:%d%d\n",
	  (data[2] >> 4) & 0xf, data[2] & 0xf,
	  (data[1] >> 4) & 0xf, data[1] & 0xf,
	  (data[0] >> 4) & 0xf, data[0] & 0xf);
  return (1);
}

static int
bcd_to_bin (int bcd)
{
  UWORD32 tmp;
  
  tmp = ((UWORD32)bcd) >> 4;
  return ((tmp * 10) + (bcd & 0xf));
}


  
