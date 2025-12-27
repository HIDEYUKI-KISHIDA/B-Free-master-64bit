/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/getcwd.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $  */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/getcwd.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: getcwd.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.2  1995-09-21 15:53:13  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1995/02/27  14:23:39  night
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
char *
psys_getcwd (void *argp)
{
  struct a {
    char *buf;
    size_t size;
  } *args = (struct a *)argp;

  if (!args || !args->buf || args->size < 2) {
    errno = EINVAL;
    return NULL;
  }
  // 仮実装: ルートディレクトリのみ返す
  args->buf[0] = '/';
  args->buf[1] = '\0';
  return args->buf;
}
