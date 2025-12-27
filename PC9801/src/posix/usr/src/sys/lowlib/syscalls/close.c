/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/close.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $  */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/PC9801/src/posix/usr/src/sys/lowlib/syscalls/close.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: close.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.3  1995-09-21 15:53:09  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.2  1995/03/18  14:30:31  night
 * �����ѹ�����ӥ����ƥॳ��������Τ���ι�¤�Τ������
 *
 * Revision 1.1  1995/02/27  14:23:31  night
 * �ǽ����Ͽ
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
psys_close (void *argp)
{
  struct a {
    int fileid; // ファイルディスクリプタ
  } *args = (struct a *)argp;

  // fileidが有効範囲かチェック
  if (args->fileid < 0 || args->fileid >= NFILE) {
    errno = EBADF;
    return -1;
  }

  FILE *fp = &__file_table__[args->fileid];
  // 参照カウント減算
  fp->count--;
  if (fp->count <= 0) {
    // バッファ解放やデバイスクローズ処理（必要なら）
    fp->length = 0;
    fp->bufsize = 0;
    // TODO: デバイス固有のclose処理があれば呼び出す
  }

  return 0;
}
