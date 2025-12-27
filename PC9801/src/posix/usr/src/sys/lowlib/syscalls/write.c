/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/write.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $  */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/write.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: write.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.2  1995-09-21 15:53:28  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1995/03/04  14:36:40  night
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
ssize_t
psys_write (void *argp)
{
  struct a {
    int fileid;
    const void *buf;
    size_t count;
  } *args = (struct a *)argp;

  if (!args || args->fileid < 0 || args->fileid >= NFILE || !args->buf) {
    errno = EBADF;
    return -1;
  }
  // stdout/stderrのみ対応
  if (args->fileid == 1 || args->fileid == 2) {
    const char *cbuf = (const char *)args->buf;
    size_t i;
    for (i = 0; i < args->count; i++) {
      putc(cbuf[i], (args->fileid == 1) ? stdout : stderr);
    }
    return args->count;
  }
  errno = ENOSYS;
  return -1;
}
