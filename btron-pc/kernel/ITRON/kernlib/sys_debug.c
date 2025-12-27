/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* �ǥХå��ѤΥ����ƥॳ����
 *
 */

#include "../../ITRON/h/types.h"
#include "../../ITRON/h/itron.h"
#include "../../ITRON/h/syscall.h"
#include "../../ITRON/h/errno.h"

ER
dbg_puts (B *msg)
{
  return call_syscall (SYS_DBG_PUTS, msg);
}


static ER	vprintf (B *fmt, B *ap);


void
putchar (B ch)
{
  B buf[2];

  buf[0] = ch & 0x000000ff;
  buf[1] = '\0';
  dbg_puts (buf);
}


static void
print_digit (UW d, UW base)
{
  static B digit_table[] = "0123456789ABCDEF";

  if (d < base)
    {
      putchar (digit_table[d]);
    }
  else
    {
      print_digit (d / base, base);
      putchar (digit_table[d % base]);
    }
}


static void
print_string (B *string)
{
  dbg_puts (string);
}

#ifdef notdef
#define INC(p,x)	(((W)p) = (((W)p) + sizeof (x *)))
#endif


W
dbg_printf (B *fmt,...)
{
  B *arg0;

  arg0 = (B *)&fmt;
  arg0 += sizeof (B *);
  return vprintf (fmt, arg0);
}

static ER
vprintf (B *fmt, B *ap)
{
  for (; *fmt != '\0'; fmt++)
    {
      if (*fmt == '%')
	{
	  switch (*++fmt)
	    {
	    case 's':
	      print_string (*(B **)ap);
	      ap += sizeof (B *);
	      break;

	    case 'd':
	      {
		W val = *(W *)ap;
		ap += sizeof (W);
		if (val < 0)
		  {
		    val = -val;
		    putchar ('-');
		  }
		print_digit ((UW)val, 10);
		break;
	      }

	    case 'x':
	      {
		UW val = *(UW *)ap;
		ap += sizeof (UW);
		print_digit (val, 16);
		break;
	      }

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

  return E_OK;
}

