#include "console.h"

// putcharの実装（画面出力用）
int putchar(int c) {
  // 画面に1文字出力（console.cの内部状態x,yを利用）
  extern void write_vram(int x, int y, int ch, int attr);
  static int px = 0, py = 0;
  if (c == '\n') {
    px = 0; py++;
    if (py > MAX_HEIGHT) py = 0;
  } else if (c == '\t') {
    int tmp = ((px + 8) & ~7);
    while (px < tmp && px <= MAX_WIDTH) {
      write_vram(px, py, ' ', 0xE1);
      px++;
    }
  } else {
    write_vram(px, py, c, 0xE1);
    px++;
    if (px > MAX_WIDTH) { px = 0; py++; }
    if (py > MAX_HEIGHT) py = 0;
  }
  return c;
}
// --- クロスビルド用ダミー定義 ---
#ifndef E_OK
#define E_OK 0
#endif
#ifndef E_DEV
#define E_DEV -3
#endif
#ifndef DMA_READ
#define DMA_READ 0
#endif
#ifndef DMA_MASK
#define DMA_MASK 0
#endif
#ifndef ROUNDUP
#define ROUNDUP(x,align) (((x + (align - 1)) / align) * align)
#endif
extern void busywait(int x);
extern void wait_int(int *x);
extern void setup_dma(void *a, int b, int c, int d);
extern void lock(void);
extern void unlock(void);

#ifndef LIBC_DUMMY_DECLS
#define LIBC_DUMMY_DECLS
int putchar(int c);
void write_cr(void);
void write_tab(void);
#endif
/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/

/* $Id: lib.c,v 1.2 2011/12/30 00:57:06 liu1 Exp $
 *
 * $Log: lib.c,v $
 * Revision 1.2  2011/12/30 00:57:06  liu1
 * コンパイルエラーの修正。
 *
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.9  1999-03-31 07:17:30  monaka
 * Added cast for bcopy().
 *
 * Revision 1.8  1999/03/15 01:35:26  monaka
 * Minor modify. Function printf was renamed to boot_printf. Some cast was added. and so on.
 *
 * Revision 1.7  1998/11/20 08:02:33  monaka
 * *** empty log message ***
 *
 * Revision 1.6  1998/06/11 15:42:33  night
 * bzero �ΥХ��ν�����
 * (�ΰ�򥹥����פ���ݥ��󥿤򥤥󥯥���Ȥ��Ƥ��ʤ��ä�)
 *
 * Revision 1.5  1996/11/13 16:41:02  night
 * strcat, strncat �ΥХ�������
 * ���ԡ����ΰ��������Ƥ򥳥ԡ����Ƥ��ʤ���
 *
 * Revision 1.4  1996/11/13  16:28:14  night
 * strcat, strncat �� TPTR �η������ BPTR ���ѹ�������
 *
 * Revision 1.3  1996/05/17  14:21:18  night
 * ����������֤���ꤹ���������������֤򼨤��ͤ� 2 �ܤˤ��Ƥ�����������������
 *
 * Revision 1.2  1996/05/11  15:50:47  night
 * ����������֤�������ˡ�� IBM-PC �Ѥ��ѹ���
 *
 * Revision 1.1  1996/05/11  10:45:02  night
 * 2nd boot (IBM-PC �� B-FREE OS) �Υ�������
 *
 *
 */


static char	rcsid[] = "$Id: lib.c,v 1.2 2011/12/30 00:57:06 liu1 Exp $";

#include "types.h"
#include "location.h"
#include "memory.h"
#include "console.h"
#include "macros.h"
#include "misc.h"
#include "vram.h"
#include "asm.h"
#include "lib.h"

static void	write_string (char *s, int width);
static void	write_digit (int n, int base, int width);
static void	write_digit2 (ULONG n, int base, int width);

#define  _AUPBND                (sizeof (int) - 1)
#define  _ADNBND                (sizeof (int) - 1)

#define _bnd(X, bnd)            (((sizeof (X)) + (bnd)) & (~(bnd)))
#define va_arg(ap, T)           (*(T *)(((ap) += (_bnd (T, _AUPBND))) - (_bnd (T,_ADNBND))))
#define INC(ap,T)           	((T *)(((ap) += (_bnd (T, _AUPBND))) - (_bnd (T,_ADNBND))))
/* (((t *)x) += 1) */

void
boot_printf (char *fmt, ...)
{
  int	i;
  void	**sp = (void **)&fmt;
  int	width;

  INC(sp, char*);
  for (i = 0; fmt[i] != '\0'; i++)
    {
      if (fmt[i] == '%')
	{
	  width = MAXINT;
	  i++;
	  if (ISDIGIT (fmt[i]))
	    {
	      width = 0;
	      while (ISDIGIT (fmt[i]))
		{
		  width = (fmt[i] - '0') + (width * 10);
		  i++;
		}
	    }
	  switch (fmt[i])
	    {
	    case 's':
	      write_string ((char *)*sp, width);
	      INC (sp, char*);
	      break;
	    case 'd':
	      write_digit ((int)*sp, 10, width);
	      INC (sp, int);
	      break;
	    case 'x':
	      write_digit ((int)*sp, 16, width);
	      INC (sp, int);
	      break;
	    case 'c':
	      putchar ((int)*sp);
	      INC (sp, int);
	      break;
	    }
	}
      else
	{
	  switch (fmt[i])
	    {
	    default:
	      putchar (fmt[i]);
	      break;
	    case '\n':
	      write_cr ();
	      break;
	    case '\t':
	      write_tab ();
	      break;
	    }
	}
    }
}

/***********************************************************************
 *
 */
static void
write_string (char *s, int width)
{
  int	i;
  
  for (i = 0; (*s != '\0') && (i < width); i++)
    {
      switch (*s)
	{
	default:
	  putchar (*s);
	  break;
	case '\n':
	  write_cr ();
	  break;
	case '\t':
	  write_tab ();
	  break;
	}
      s++;
    }
}

/***********************************************************************
 *
 */
static void
write_digit (int n, int base, int width)
{
  if (n == 0)
    {
      putchar ('0');
    }
  else if ((n < 0) && (base == 10))
    {
      putchar ('-');
      write_digit2 (-n, base, width);
    }
  else
    {
      write_digit2 (n, base, width);
    }
}

static void
write_digit2 (ULONG n, int base, int width)
{
  int	ch;

  if (--width < 0) return;

  if (n > 0)
    {
      write_digit2 (n / base, base, width);
      ch = n % base;
      if ((ch >= 0) && (ch <= 9))
	ch += '0';
      else
	ch = (ch - 10) + 'a';
      putchar (ch);
    }
}



/* ʸ����ط��Υե����� */

/*************************************************************************
 * strlen
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
WORD
strlen (BPTR s)
{
  int	i;

  for (i = 0; *s != '\0'; i++, s++)
    ;
  return (i);
}

/*************************************************************************
 * strnlen ---
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
WORD
strnlen (BPTR s, WORD len)
{
  WORD	i;

  for (i = 0; (*s != '\0') && (i < len); i++, s++)
    ;
  return (i);
}

/*************************************************************************
 * strcpy --- 
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
WORD
strcpy (BPTR s1, BPTR s2)
{
  WORD i;
  
  for (i = 0; *s2; i++)
    {
      *s1++ = *s2++;
    }
  *s1 = *s2;
  return (i);
}

/*************************************************************************
 * strncpy --- 
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
WORD
strncpy (BPTR s1, BPTR s2, WORD n)
{
  WORD i;
  
  for (i = 0; *s2; i++)
    {
      if (i > n)
	{
/*	  *s1 = '\0';*/
	  return (i);
	}
      *s1++ = *s2++;
    }
/*  *s1 = *s2;*/
  return (i);
}

/*************************************************************************
 * strncpy_with_key --- 
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
WORD
strncpy_with_key (BPTR s1, BPTR s2, WORD n, int key)
{
  WORD i;
  
  for (i = 0; *s2; i++)
    {
      if ((i >= n) || (*s2 == key))
	{
	  *s1 = '\0';
	  return (i);
	}
      *s1++ = *s2++;
    }
  *s1 = *s2;
  return (i);
}

/***********************************************************************
 * strcat -- ���Ĥ�ʸ�����Ϣ��
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
BPTR
strcat (BPTR s1, BPTR s2)
{
  TPTR ret_val = s1;
  
  while (*s1)
    s1++;

  while (*s2)
    {
      *s1++ = *s2++;
    }
  *s1++ = *s2++;
  return ret_val;
}

/***********************************************************************
 * strncat -- ���Ĥ�ʸ�����Ϣ��
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
BPTR
strncat (BPTR s1, BPTR s2, WORD n)
{
  TPTR	ret_val = s1;
  WORD	i;
  
  while (*s1)
    s1++;

  for (i = 0; *s2; i++)
    {
      if (i >= n)
	break;
      *s1++ = *s2++;
    }
  *s1++ = *s2++;
  return ret_val;
}

/***********************************************************************
 * strcmp --- ���Ĥ�ʸ��������
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
ULONG
strcmp (BPTR s1, BPTR s2)
{
  while (*s1 && *s2)
    {
      if (*s1 != *s2)
        {
          return (*s2 - *s1);
        }
      s1++;
      s2++;
    }
  return (*s1 - *s2);
}

/***********************************************************************
 * strchr -- ʸ�������˻��ꤷ��ʸ�����ޤޤ�Ƥ��ʤ�����Ĵ�٤롣
 *
 *
 */
char *
strchr (char *s, int ch)
{
  while (*s != '\0')
    {
      if (ch == *s)
	{
	  return (s);
	}
      s++;
    }
  return (NULL);
}

/***********************************************************************
 * strnchr -- ʸ�������˻��ꤷ��ʸ�����ޤޤ�Ƥ��ʤ�����Ĵ�٤롣
 *
 *
 */
char *
strnchr (char *s, int ch, int n)
{
  int	i;

  for (i = 0; *s != '\0'; i++)
    {
      if (i < n)
	break;
      if (ch == *s)
	{
	  return (s);
	}
      s++;
    }
  return (NULL);
}


/***********************************************************************
 * strncmp -- ���Ĥ�ʸ��������
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
ULONG
strncmp (BPTR s1, BPTR s2, WORD n)
{
  WORD	i;

  for (i = 0; (i < n) && *s1 && *s2; i++)
    {
      if (*s1 != *s2)
        return (*s2 - *s1);
      s1++;
      s2++;
    }
  if (i == n)
    return 0;
  return (*s1 - *s2);
}

int
toupper (int ch)
{
  if ((ch >= 'a') && (ch <= 'z'))
    {
      ch = ch + ('A' - 'a');
    }
  return (ch);
}


/* VRAM �ط������ */

/***********************************************************************
 * 
 */
void
write_vram (int x, int y, int ch, int attr)
{
  short	*addr;
  
  addr = (short *)TEXT_VRAM_ADDR;
  addr[CURSOR_POS(x, y)] = ((attr & 0xff) << 8) | (ch & 0xff);
}

/**************************************************************************
 * set_cursor_pos --- 
 */
void
set_cursor_pos (int x, int y)
{
  int	addr;

  lock ();
  addr = CURSOR_POS (x, y);
/*  addr *= 2; */

  outb (GDC_ADDR, 0x0e);
  outb (GDC_DATA, (addr >> 8) & 0xff);
  outb (GDC_ADDR, 0x0f);
  outb (GDC_DATA, addr & 0xff);
  unlock ();
}

void
scroll_up ()
{
  UWORD16 *addr;
  int i;
  
  addr = (UWORD16 *)TEXT_VRAM_ADDR;
  for (i = 0; i < TEXT_VRAM_SIZE - 80; i++)
    {
      addr[i] = addr[i + 80];
    }
  for (i = 0; i <= MAX_WIDTH; i++)
    {
      write_vram (i, MAX_HEIGHT, ' ', 0xe1);
    }
}


/* ʸ���� -> ���ͤؤ��Ѵ��ؿ� */

#define isdigit(c,base)	((c >= '0') && (c <= '9'))


/**************************************************************************
 *
 */
int
atoi (char *s)
{
  int result;
  result = string_to_number (s, 10);

  return result;
}

/***************************************************************************
 *
 */
int
string_to_number (char *s, int base)
{
  int	total = 0;
  

  while (*s != '\0')
    {
      if (!isdigit (*s, base))
	return (0);
      total = (total * base) + (*s - '0');
      s++;
    }
  return (total);
}


/* bcopy �ط��δؿ� */


/*************************************************************************
 * bcopy --- copy string.
 */
void
bcopy (const char *src, char *dest, int length)
{
  while (length-- > 0)
    {
      *dest++ = *src++;
    }
}

/************************************************************************
 * bzero
 */
void
bzero (char *src, int length)
{
  while (length-- > 0)
    {
      *src++ = 0;
    }
}

