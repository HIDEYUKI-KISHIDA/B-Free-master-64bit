/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/BTRON/servers/gname/misc.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */

static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/BTRON/servers/gname/misc.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: misc.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.1  1999-12-29 17:24:53  monaka
 * Still mock-up code...
 *
 * Revision 1.1  1998/12/19 07:50:25  monaka
 * Pre release version.
 *
 */


#include "gname.h"
#include "gname_internal.h"





static ID	log_port;
static ID	dev_recv;

/* 仮のデバイス検出ロジック。32bit移植完了までは常に成功扱いとする。 */
ER
probe (struct device *dev)
{
  (void) dev;
  return E_OK;
}


static ER	vprintf (B *fmt, B *arg0);




/* init_log - �������ϵ�������������
 *
 *
 */
void
init_log (void)
{
  if (find_port (CONSOLE_DRIVER, &log_port) != E_PORT_OK)
    {
      dbg_printf ("POSIX: Cannot open console device.\n");
      slp_tsk ();
      /* DO NOT REACHED */
    }

  dev_recv = get_port (sizeof (DDEV_RES), sizeof (DDEV_RES));
  if (dev_recv <= 0)
    {
      dbg_printf ("POSIX: Cannot allocate port\n");
      slp_tsk ();
      /* DO NOT REACHED */
    }
}


void
print_digit (UW d, UW base)
{
  static B digit_table[] = "0123456789ABCDEF";

  if (d < base)
    {
      putc ((W)(digit_table[d]), log_port);
    }
  else
    {
      print_digit (d / base, base);
      putc ((W)(digit_table[d % base]), log_port);
    }
}


W
printf (B *fmt,...)
{
  B *arg0;
  ER err;

  arg0 = (B *)&fmt;
  arg0 += sizeof (B *);
  err = vprintf (fmt, arg0);
  return (err);
}

static ER
vprintf (B *fmt, B *ap)
{
  for (; *fmt != '\0'; fmt++)
    {
      if ((*fmt) == '%')
	{
	  ++fmt;
	  switch (*fmt)
	    {
	    case 's':
        put_string (*(B **)ap, log_port);
        ap += sizeof (B *);
	      break;

	    case 'd':
        {
    W val = *(W *)ap;
    ap += sizeof (W);
    if (val < 0)
    {
      val = -val;
      putc ('-', log_port);
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
	      putc ('%', log_port);
	      break;
	    }
	}
      else
	{
	  putc (*fmt, log_port);
	}
    }

  return (E_OK);
}



W
put_string (B *line, ID port)
{
  W i;

  for (i = 0; line[i] != '\0'; i++)
    {
      putc (line[i], port);
    }
  return (i);
}


W 
putc (int ch, ID port)
{
  DDEV_REQ		req;		/* �׵�ѥ��å� */
  DDEV_RES		res;		/* �����ѥ��å� */
  W			rsize;
  ER			error;
  W			i;
  
  
  req.header.mbfid = dev_recv;
  req.header.msgtyp = DEV_WRI;
  req.body.wri_req.dd = 0xAA;
  req.body.wri_req.size = 1;
  req.body.wri_req.dt[0] = (char)(ch & 0xff);
  error = snd_mbf (port, sizeof (req), &req);
  if (error != E_OK)
    {
      dbg_printf ("cannot send packet. %d\n", error);
      return (0);
    }
  rsize = sizeof (res);
  error = rcv_mbf (&res, (INT *)&rsize, dev_recv);
  if (res.body.wri_res.errcd != E_OK)
    {
      dbg_printf ("%d\n", res.body.wri_res.errcd);
      return (0);
    }      
  return (1);
}



/* _assert - ASSERT �ޥ����ˤ�äƸƤӽФ����ؿ�
 *
 * ��å���������Ϥ����ץ�������λ���롣
 *
 */ 
void
_assert (B *msg)
{
  printf ("ASSERT: ");
  printf ("%s\n", msg);
  for (;;)
    {
      slp_tsk ();
    }
}



void
busywait (W count)
{
  W	i;
  W	dummy;

  while (count-- > 0)
    {
      for (i = 0; i < 10000; i++)
	{
	  dummy++;
	}
    }
}
