/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* echo.c --- echo �ǥХ����ɥ饤�� 
 *
 * ���ΥǥХ����ɥ饤�Фϡ�I/O �����ƥ�ΥǥХå��Ѥ˺���������
 * 
 */

#include "itron.h"
#include "errno.h"
#include "task.h"
#include "misc.h"
#include "func.h"
#include "interrupt.h"
#include "../io/io.h"

static ID	deviceid;	/* �ǥХ����ֹ�(�ǥХ����ơ��֥�Υ���ȥ��ֹ�) */
				/* ���ֹ�Ȥ��ƻ��Ѥ��롣*/
static ID	taskid;		/* �ңӣ������åɥ饤�ФΥ������ɣ� */


extern ER	init_echo (void);
static void echo_server (void);

/* �ǥХå��Ѥ� printk 
 */
#define DPRINTK(x)	{ printk x; }


/*****************************************************************************
 * init_echo 
 *
 * ������
 *	�ʤ�
 *
 * �֤��͡�
 *	���顼�ֹ�
 *	E_OK	���ｪλ
 *
 * ��ǽ��
 *
 *
 */
ER
init_echo (void)
{
  ER		err;
  T_CTSK	pktsk;

	err = def_dev (DEVNAME_ECHO, CHAR, ANY_DEVICE, &deviceid);
  if (err != E_OK)
    {
      printk ("cannot initialize for echo device. err = %d\n", err);
      return (err);
    }

  DPRINTK(("init_echo: setup\n"));

  pktsk.tskatr = TA_HLNG;
  pktsk.startaddr = echo_server;
  pktsk.itskpri = 1;
  pktsk.stksz = PAGE_SIZE * 2;
  pktsk.addrmap = NULL;
  err = new_task (&pktsk, &taskid, TRUE);
  if (err != E_OK)
    {
      printk ("cannot create task for echo. err = %d\n", err);
      return (err);
    }
  printk ("echo: TASK ID = %d\n", taskid);
  return (E_OK);
}

static void
echo_server (void)
{
  T_IO_REQUEST	rcv_packet;
  T_IO_RESPONSE	res_packet;
  ER		err;
  
  printk ("ECHO: server start.\n");
  for (;;)
    {
      err = get_ioreq (deviceid, &rcv_packet);
      if (err == E_OK)
	{
	  printk ("ECHO: Receive request; command = %d\n", rcv_packet.command);
	  /* ���ޥ�ɲ������¹Ԥ��� */
	  switch (rcv_packet.command)
	    {
	      /* IO_NULL, IO_OPEN, IO_CLOSE �ˤĤ��Ƥϲ��⤷�ʤ� */
	    case IO_NULL:
	    case IO_OPEN:
	    case IO_CLOSE:
	      res_packet.stat = E_OK;
	      break;
	      
	    case IO_READ:
	      res_packet.stat = E_OK;
	      break;

	    case IO_WRITE:
	      {
		W	i;

		printk ("ECHO: write size = %d\n", rcv_packet.s.write_pack.size);
		for (i = 0; i < rcv_packet.s.write_pack.size; i++)
		  {
		    putchar (((B *)(rcv_packet.s.write_pack.bufp))[i]);
		  }
	      }
	      res_packet.stat = E_OK;
	      break;

	    case IO_STAT:
	      res_packet.stat = E_OK;
	      break;

	    case IO_CONTROL:
	      res_packet.stat = E_OK;
	      break;

	    default:
	      printk ("ECHO: paramater error\n");
	      res_packet.stat = E_PAR;
	      break;
	    }
	}
      
      put_res (deviceid, &rcv_packet, &res_packet);
    }
}

