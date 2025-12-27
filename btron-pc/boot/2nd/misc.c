#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
#define __attribute__(x)
#define __asm__(x)
#define __volatile__
#define __restrict
#define __inline__
#define __extension__
#define __builtin_va_list int
#define __builtin_va_start(a,b)
#define __builtin_va_end(a)
#define __builtin_va_arg(a,b) (0)
#define __builtin_offsetof(type, member) ((size_t)&(((type *)0)->member))
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *d, const void *s, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern int printf(const char *fmt, ...);
extern int puts(const char *s);
extern int putchar(int c);
extern int sprintf(char *str, const char *fmt, ...);
extern int snprintf(char *str, size_t size, const char *fmt, ...);
extern int sscanf(const char *str, const char *fmt, ...);
extern void abort(void);
extern void exit(int code);
extern int atexit(void (*f)(void));
extern int setjmp(jmp_buf env);
extern void longjmp(jmp_buf env, int val);
#endif
#ifndef DEL
#define DEL 0x7F
#endif
extern int putchar(int c);
void get_coursor(int *x, int *y);
void set_coursor(int x, int y);
/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/*************************************************************************
 *
 *		2nd BOOT misc routine.
 *
 * $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/misc.c,v 1.2 2011/12/30 00:57:06 liu1 Exp $
 *
 * $Log: misc.c,v $
 * Revision 1.2  2011/12/30 00:57:06  liu1
 * コンパイルエラーの修正。
 *
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.3  2000-01-30 18:50:12  kishida0
 * use same keyboard define file & you can use BS key
 *
 * Revision 1.2  1999/03/31 07:57:07  monaka
 * Minor fixes.
 *
 * Revision 1.1  1996/05/11 10:45:06  night
 * 2nd boot (IBM-PC �� B-FREE OS) �Υ�������
 *
 * 
 * ------------------------------------------------------------------------
 * 
 * Revision 1.3  1995/09/21  15:50:41  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.2  1994/07/30  17:37:18  night
 * �ե�����������ܸ�ʸ���򤹤٤� EUC �����ɤ��ѹ���
 *
 * Revision 1.1  1993/10/11  21:29:38  btron
 * btron/386
 *
 * Revision 1.1.1.1  93/01/14  12:30:25  btron
 * BTRON SYSTEM 1.0
 * 
 * Revision 1.1.1.1  93/01/13  16:50:23  btron
 * BTRON SYSTEM 1.0
 * 
 *
 * �����ʴؿ��ν���
 */

static char	rcsid[] = "$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/misc.c,v 1.2 2011/12/30 00:57:06 liu1 Exp $";

#include "console.h"
#include "keyboard.h"
#include "types.h"
#include "lib.h"
#include "misc.h"

#include "keycode.h"


/***************************************************************************
 *
 */
int
getchar (void)
{
  int	ch;
  
  ch = read_keyboard ();
  putchar (ch);
  return (ch);
}
int getch(void)
{
  int ch;
  ch = read_keyboard ();
  return ch;
}

/***************************************************************************
 *
 */
char *
gets (char *line)
{
  int	ch;
  int   x,y;
  char	*p;
  int   len;
  
  p = line;
  len = 0;
  ch = getch ();
  while (ch != '\n')
    {
      get_coursor(&x,&y);
      if(ch == DEL){
        if(len>0){
          set_coursor(x-1,y);
          putchar(' ');
          set_coursor(x-1,y);
          p--;
          len--;
        }
      }else{
        *p++ = ch;
        len++;
        putchar(ch);
      }
      ch = getch ();
    }
  *p = '\0';
  putchar('\n');
  return (line);
}

void
__stack_chk_fail(void)
{
  boot_printf("stack over flow\n");
  for(;;) {
    ;
  }
}
