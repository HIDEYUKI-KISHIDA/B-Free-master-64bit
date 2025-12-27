/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/read.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $  */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/read.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: read.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.2  1995-09-21 15:53:19  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1995/03/04  14:36:17  night
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
psys_read (void *argp)
{
  struct a {
    int fileid;
    void *buf;
    size_t count;
  } *args = (struct a *)argp;

  if (!args || args->fileid < 0 || args->fileid >= NFILE || !args->buf) {
    errno = EBADF;
    return -1;
  }
  // stdinのみ対応（1バイト読み取り）
  if (args->fileid == 0) {
    char *cbuf = (char *)args->buf;
    cbuf[0] = getc(stdin);
    return 1;
  }
  errno = ENOSYS;
  return -1;
}
