/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* printk.c --- printk �ط��δؿ�
 *
 */

#include <stddef.h>
#include "itron.h"
#include "func.h"



static inline VP advance_arg (VP ptr, size_t bytes)
{
	return (VP)(((B *)ptr) + bytes);
}


static void	print_string (B *);
static void	print_digit (UW, UW);
static void	print_digit0 (UW, UW);

/*
 *
 */
W
printk (B *fmt,...)
{
  VP arg0;

	arg0 = (VP)&fmt;
	arg0 = advance_arg (arg0, sizeof (B *));
	return (vprintk (fmt, arg0));
}


/*
 *
 */
W
vprintk (B *fmt, VP arg0)
{
	B *cursor = (B *)arg0;

	for (; *fmt != '\0'; fmt++)
    {
      if (*fmt == '%')
	{
	  switch (*++fmt)
	    {
	    case 's':
	      {
		B *str = *(B **)cursor;
		cursor = (B *)advance_arg (cursor, sizeof (B *));
		print_string (str);
	      }
	      break;

	    case 'd':
	      {
		W val = *(W *)cursor;
		cursor = (B *)advance_arg (cursor, sizeof (W));
		if (val < 0)
		  {
		    val = -val;
		    putchar ('-');
		  }
		print_digit ((UW)val, 10);
	      }
	      break;

	    case 'x':
	      {
		UW val = *(UW *)cursor;
		cursor = (B *)advance_arg (cursor, sizeof (UW));
		print_digit (val, 16);
	      }
	      break;

	    default:
	      putchar ('%');
	      break;
	    }
	}
      else
	{
	  putchar (*fmt);
	}
    }
  return 0; /* dummy */
}

static void
print_string (B *s)
{
  while (*s != '\0')
    {
      putchar (*s);
      s++;
    }
}

static void
print_digit (UW d, UW base)
{
  print_digit0 (d, base);
}

static void
print_digit0 (UW d, UW base)
{
  static B digit_table[] = "0123456789ABCDEF";

  if (d < base)
    {
      putchar (digit_table[d]);
    }
  else
    {
      print_digit0 (d / base, base);
      putchar (digit_table[d % base]);
    }
}
