/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/BTRON/servers/gname/gname_internal.h,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */

/* 
 * $Log: gname_internal.h,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.1  1999-12-29 17:24:53  monaka
 * Still mock-up code...
 *
 * Revision 1.1  1998/12/19 07:50:25  monaka
 * Pre release version.
 *
 */


#ifndef	__GNAME_INTERNAL_H__
#define	__GNAME_INTERNAL_H__	1


#include "../../ITRON/h/itron.h"
#include "../../ITRON/h/errno.h"
#include "../../BTRON/device/console/console.h"
#include "../../ITRON/servers/port-manager.h"


/*** �����˥ǥХ���������򤫤��Ƥ������� ***/


/* �ǥХ�����¤�� 
 * GNAME �ǥХ����ϡ��ơ����ι�¤�ΤǴ������Ƥ���
 */
struct device
{
  /*** ������ ***/
};




#include "global.h"
#include "funcs.h"
#include "../../ITRON/h/itron_misc_func.h"

extern ER probe (struct device *dev);


#endif /* __GNAME_INTERNAL_H__ */
