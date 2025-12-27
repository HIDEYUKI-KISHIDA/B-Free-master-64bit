/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/lseek.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $  */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/lseek.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: lseek.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.2  1995-09-21 15:53:17  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1995/03/04  14:36:12  night
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
off_t
psys_lseek (void *argp)
{
  struct a {
    int fileid;
    off_t offset;
    int whence;
  } *args = (struct a *)argp;

  if (!args || args->fileid < 0 || args->fileid >= NFILE) {
    errno = EBADF;
    return -1;
  }
  // stdin/stdout/stderrは常に0を返す仮実装
  if (args->fileid == 0 || args->fileid == 1 || args->fileid == 2) {
    return 0;
  }
  errno = ENOSYS;
  return -1;
}
