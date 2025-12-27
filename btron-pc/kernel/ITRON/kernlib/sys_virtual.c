/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/kernlib/sys_virtual.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */
static char	rcsid[] = "$Id: sys_virtual.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: sys_virtual.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.1  1999-04-18 17:48:34  monaka
 * Port-manager and libkernel.a is moved to ITRON. I guess it is reasonable. At least they should not be in BTRON/.
 *
 * Revision 1.8  1998/02/25 12:33:35  night
 * vmap_reg () �ΰ����ο����ҤȤ����������Ȥˤ���ѹ���
 *
 * Revision 1.7  1998/02/16 14:14:18  night
 * vget_phs() �ؿ����ɲá�
 *
 * Revision 1.6  1997/10/24 14:04:24  night
 * vdel_reg() ���ɲá�
 *
 * Revision 1.5  1997/10/23 14:31:06  night
 * vunm_reg () ���ɲ�
 *
 * Revision 1.4  1996/11/20 12:08:29  night
 * vput_reg() ���ɲá�
 *
 * Revision 1.3  1996/11/07  12:40:43  night
 * �ؿ� vget_reg () ���ɲá�
 *
 * Revision 1.2  1996/07/23  17:17:08  night
 * IBM PC �Ѥ� make �Ķ��� merge
 *
 * Revision 1.1  1996/07/22  23:52:06  night
 * �ǽ����Ͽ
 *
 *
 */

/*
 *	���ۥ����Ϣ
 *
 */


#include "../ITRON/h/types.h"
#include "../ITRON/h/itron.h"
#include "../ITRON/h/syscall.h"
#include "../ITRON/h/errno.h"

/*
 * 
 */
ER
vcre_reg (ID	id, 	/* task ID */
	  VP	start,	/* �꡼�����γ��ϥ��ɥ쥹 */
	  W	min,	/* �꡼�����κǾ�(���)������ */
	  W	max,	/* �꡼�����κ��祵���� */
	  UW	perm,	/* �꡼�����Υѡ��ߥå���� */
	  FP	handle)	/* �꡼�������ǥڡ����ե�����Ȥ�ȯ�������� */
			/* ���ν����λ��� */
{
  return (call_syscall (SYS_VCRE_REG, id, start, min, max, perm, handle));
}


/* vdel_reg - �꡼�������˴�
 *
 */
ER
vdel_reg (ID taskid, VP start)
{
  return (call_syscall (SYS_VDEL_REG, taskid, start));
}

/*
 *
 */
ER
vunm_reg (ID id, VP start, UW size)
     /* 
      * id        ������ ID
      * start     ����ޥåפ��벾�ۥ����ΰ����Ƭ���ɥ쥹
      * size      ����ޥåפ��벾�ۥ����ΰ���礭��(�Х���ñ��)
      */
{
  return (call_syscall (SYS_VUNM_REG, id, start, size));
}


/*
 *
 */
ER
vmap_reg (ID id, VP start, UW size, W accmode)
     /* 
      * id        ������ ID
      * start     �ޥåפ��벾�ۥ����ΰ����Ƭ���ɥ쥹
      * size      �ޥåפ��벾�ۥ����ΰ���礭��(�Х���ñ��)
      */
{
  return (call_syscall (SYS_VMAP_REG, id, start, size, accmode));
}


/*
 *
 */
ER
vget_reg (ID id, VP start, UW size, VP buf) 
     /*
      * id     �꡼��������ĥ�����
      * start  �ɤ߹����ΰ����Ƭ���ɥ쥹
      * size   �꡼����󤫤��ɤ߹��ॵ����
      * buf    �꡼����󤫤��ɤ߹�����ǡ���������Хåե�
      */
{
  return (call_syscall (SYS_VGET_REG, id, start, size, buf));
}

/*
 *
 */
ER
vput_reg (ID id, VP start, UW size, VP buf) 
     /*
      * id     �꡼��������ĥ�����
      * start  �񤭹����ΰ����Ƭ���ɥ쥹
      * size   �꡼�����˽񤭹��ॵ����
      * buf    �꡼�����˽񤭹���ǡ���������Хåե�
      */
{
  return (call_syscall (SYS_VPUT_REG, id, start, size, buf));
}


/*
 */
ER
vget_phs (ID id, VP addr, UW *paddr)
{
  return (call_syscall (SYS_VGET_PHS, id, addr, paddr));
}
