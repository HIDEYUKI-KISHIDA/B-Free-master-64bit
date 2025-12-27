/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/POSIX/lowlib/funcs.h,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */

/*
 * $Log: funcs.h,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.2  1997-08-31 13:13:42  night
 * �Ȥꤢ������OS �ե�����������Ȥ����ޤǤǤ�����
 *
 * Revision 1.1  1996/11/11  13:36:06  night
 * IBM PC �Ǥؤκǽ����Ͽ
 *
 * ----------------
 *
 * Revision 1.4  1995/09/21  15:52:56  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.3  1995/03/18  14:24:16  night
 * �����ƥॳ����ؿ��Υץ��ȥ�����������ѹ���
 * ���ϡ������򥷥��ƥॳ����ΰ����ΤȤ���˽񤤤Ƥ�����
 * ����򡢰����Ϥ��٤� void * ���ѹ�������
 *
 * Revision 1.2  1995/02/20  15:20:18  night
 * RCS �� Log �ޥ������������������ɲá�
 *
 *
 */

/*
 * �����ƥॳ��������ؿ��Υץ��ȥ��������
 *
 */

#ifndef __FUNCS_H__
#define __FUNCS_H__	1

#ifdef notdef

#include <sys/types.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/utime.h>


#endif

/*
 * �����ƥॳ��������Ѵؿ��������
 */
#include "syscalls/funcs.h"

/*
 * POSIX �Ǥϥ���ץ���Ȱ�¸�ȤʤäƤ��륷���ƥॳ����
 */
extern int	psys_mount (void *);
extern int	psys_umount (void *);
extern void	*psys_grow_heap ();

/*
 * lowlib.c
 */
extern ER	lowlib_exit ();
extern ER	lowlib_start (VP stack_top);
extern ER	lowlib_syscall (W syscallno, VP arg);

#endif /* __FUNCS_H__ */
