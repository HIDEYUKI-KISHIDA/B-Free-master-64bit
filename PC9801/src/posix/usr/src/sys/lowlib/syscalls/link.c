/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/link.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $  */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/link.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: link.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.2  1995-09-21 15:53:17  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1995/03/04  14:36:11  night
 * �ǽ����Ͽ
 *
 *
 */

/*
 *
 *
 */

#include <sys/types.h>
#include <errno.h>
#include "../funcs.h"
#include "../global.h"

/*
 *
 *
 */
int
psys_link (void *argp)
{
  struct a {
    const char *oldpath;
    const char *newpath;
  } *args = (struct a *)argp;

  if (!args || !args->oldpath || !args->newpath) {
    errno = EINVAL;
    return -1;
  }
  // 仮実装: 両方のパスがNULLでなければ成功
  return 0;
}
