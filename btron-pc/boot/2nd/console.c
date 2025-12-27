// --- クロスビルド用ダミー定義 ---
// #define E_OK, E_DEV, ROUNDUP などは errno.h, macros.h 側に統一
extern void busywait(int x);
extern void wait_int(int *x);
extern void setup_dma(void *a, int b, int c, int d);
#if 0 // lib.cに集約
void lock(void) {}
void unlock(void) {}
#endif
extern void lock(void);
extern void unlock(void);

/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/*************************************************************************
 *
 *		2nd BOOT main routine.
 *
 * $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/console.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $
 *
 * $Log: console.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.5  2000-01-30 18:50:07  kishida0
 * use same keyboard define file & you can use BS key
 *
 * Revision 1.4  1998/11/20 08:02:21  monaka
 * *** empty log message ***
 *
 * Revision 1.3  1997/04/24 15:28:53  night
 * ���󥽡���ν�������ˡ����̹Կ�ʬ�����������Ԥ��������ɲä�����
 *
 * ʸ��°���⥯�ꥢ���뤿�ᡣconsole_clear() �Ǥϡ�ʸ��°���ϥ��ꥢ���ʤ���
 * ����ˤ�äƤϡ���ư����ʸ��°�����ü�(���դ��ʤ�)�ξ�礬���롣
 *
 * Revision 1.2  1996/05/11  15:49:51  night
 * ���󥽡���ɽ�����˥��ȥ�ӥ塼�Ȥ򥻥åȤ��ʤ���
 *
 * Revision 1.1  1996/05/11  10:45:00  night
 * 2nd boot (IBM-PC �� B-FREE OS) �Υ�������
 *
 *
 * ------------------------------------------------------------------------
 *
 * Revision 1.2  1995/09/21 15:50:35  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.1  1993/10/11  21:28:41  btron
 * btron/386
 *
 * Revision 1.1.1.1  93/01/14  12:30:17  btron
 * BTRON SYSTEM 1.0
 * 
 * Revision 1.1.1.1  93/01/13  16:50:24  btron
 * BTRON SYSTEM 1.0
 * 
 *
 *	init_console
 *	console_clear
 *	write_cr
 *	write_tab
 *	putchar
 *
 */

static char	rcsid[] = "$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/console.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

#include "lib.h"
#include "errno.h"
#include "location.h"
#include "memory.h"
#include "console.h"
#include "macros.h"

static	int	x;
static	int	y;

void	write_cr (void);
void	write_tab (void);

/************************************************************************
 *
 */     
int
init_console (void)
{
  int	i;

  console_clear ();
  for (i = 0; i < MAX_HEIGHT; i++)
    {
      scroll_up ();
    }

  x = y = 0;
  set_cursor_pos (x, y);

  return E_OK;
}

void
console_clear (void)
{
  int	x, y;
  
  for (y = 0; y <= MAX_HEIGHT; y++)
    for (x = 0; x <= MAX_WIDTH; x++)
      {
	write_vram (x, y, ' ', 0xE1);
      }
}
/***********************************************************************
 *
 */
void
write_cr ()
{
  x = 0;
  if (y >= MAX_HEIGHT)
    {
      scroll_up ();
    }
  else
    {
      y++;
    }
  set_cursor_pos (x, y);
}

/***********************************************************************
 *
 */
void
write_tab ()
{
  int	tmp;
  if (x < MAX_WIDTH)
    {
      tmp = ROUNDUP (x + 1, 8) - 1;
      while (x < tmp)
	{
	  write_vram (x, y, ' ', 0);
	  x++;
	}
    }
}


/***********************************************************************
 *
 */
extern int putchar(int ch);

void set_coursor(int sx, int sy)
{
  x = sx;
  y = sy;
  set_cursor_pos (x,y);
}
void get_coursor(int *gx,int *gy)
{
  *gx = x;
  *gy = y;
}

/*
 * console_getchar - Read a character from keyboard
 * Returns the ASCII code of the character
 */
char
console_getchar(void)
{
  char c = 0;
  // キーボードバッファから取得（keyboard_driver.cのAPI利用）
  extern int kb_buffer_is_empty(void);
  extern int kb_buffer_get(void);
  while (kb_buffer_is_empty()) {
    // ポーリング: 入力待ち
  }
  c = kb_buffer_get();
  return c;
}

/*
 * console_init - Alias for init_console
 */
void
console_init(void)
{
	init_console();
}

