/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/creat.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $  */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/creat.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: creat.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.2  1995-09-21 15:53:09  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1995/02/27  14:23:32  night
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
psys_creat (void *argp)
{
  struct a {
    const char *path;
    int mode;
  } *args = (struct a *)argp;

  // 仮実装: /dev/stdoutのみ作成成功
  if (!args || !args->path) {
    errno = EINVAL;
    return -1;
  }
  if (strcmp(args->path, "/dev/stdout") == 0) return 1;
  errno = ENOSYS;
  return -1;
}
