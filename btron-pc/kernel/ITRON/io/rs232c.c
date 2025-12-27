/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* rs232c.c --- RS232C �ɥ饤�� (PC9801 ��)
 *
 * $Revision: 1.1 $
 *
 *
 * �������󥿥ե�����
 *
 * init_rs232c		RS232C �ɥ饤�Фν����
 *
 *
 * static �ؿ�
 *
 * rs232c_server	RS232C �ɥ饤�ФؤΥꥯ�����Ȥμ����դ����
 * rs232c_intr		RS232C �ϡ��ɥ���������γ�ꤳ�ߥϥ�ɥ�
 * rs232c_read		RS232C �ϡ��ɥ���������Υǡ����ɤ߹���
 * rs232c_write		RS232C �ϡ��ɥ������ؤΥǡ����񤭹���
 * rs232c_stat		RS232C �ɥ饤�Фξ���
 * rs232c_control	�ɤ߽񤭰ʳ���RS232C�ɥ饤�Ф�����
 *
 */

#include "itron.h"
#include "errno.h"
#include "task.h"
#include "misc.h"
#include "func.h"
#include "interrupt.h"
#include "../io/io.h"
#include "../io/rs232c.h"


struct data_link
{
  struct data_link	*next;
  B			ch;
};


struct rs232c
{
  W	port;
  W	speed;
  W	bitlen;
  W	stoplen;
};

static ID	deviceid;	/* �ǥХ����ֹ�(�ǥХ����ơ��֥�Υ���ȥ��ֹ�) */
				/* ���ֹ�Ȥ��ƻ��Ѥ��롣*/
static ID	taskid;		/* �ңӣ������åɥ饤�ФΥ������ɣ� */

/* ���Υե�����ǻ��Ѥ���static�ؿ�
 */
static void	rs232c_server (void);
static void	rs232c_intr (void);
static ER	rs232c_read (struct io_read_packet *);
static ER	rs232c_write (struct io_write_packet *);
static ER	rs232c_stat (struct io_stat_packet *);
static ER	rs232c_control (struct io_control_packet *);

/* �ǥХå��ѿ����ͤ� TRUE �λ��ˤϥǥХå����֤Ȥʤ�
 */
static BOOL	rs232c_debug = TRUE;

/* �ǥХå��Ѥ� printk 
 */
#define DPRINTK(x)	if (rs232c_debug) { printk x; }


/*****************************************************************************
 * init_rs232c --- �ңӣ������åɥ饤��
 *
 * ������
 *	�ʤ�
 *
 * �֤��͡�
 *	���顼�ֹ�
 *	E_OK	���ｪλ
 *
 * ��ǽ��
 *	RS232C �ν������Ԥ���
 *
 */
ER
init_rs232c (void)
{
  ER		err;
  T_CTSK	pktsk;

  err = def_dev (DEVNAME_RS232C, CHAR, ANY_DEVICE, &deviceid);
  if (err != E_OK)
    {
      printk ("cannot initialize for RS232C device. err = %d\n", err);
      return (err);
    }

  DPRINTK(("init_rs232c: setup\n"))

  pktsk.tskatr = TA_HLNG;
  pktsk.startaddr = rs232c_server;
  pktsk.itskpri = 1;
  pktsk.stksz = PAGE_SIZE * 2;
  pktsk.addrmap = NULL;
  err = new_task (&pktsk, &taskid, TRUE);
  if (err != E_OK)
    {
      printk ("cannot create task for RS232c. err = %d\n", err);
      return (err);
    }
  printk ("RS232C: TASK ID = %d\n", taskid);
  return (E_OK);
}


/*****************************************************************************
 * rs232c_server --- �ɥ饤�Фؤ��׵��å������μ����ؿ�
 *
 * ����:
 *	�ʤ�
 *
 * �֤��͡�
 *	���顼�ֹ�
 *
 */
static void
rs232c_server (void)
{
  T_IO_REQUEST	rcv_packet;
  T_IO_RESPONSE	res_packet;
  ER		err;
  
  printk ("RS232C: server start.\n");
  for (;;)
    {
      err = get_ioreq (deviceid, &rcv_packet);
      if (err == E_OK)
	{
	  printk ("RS232C: RReceive request %d\n", rcv_packet.command);
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
	      res_packet.stat = rs232c_read (&(rcv_packet.s.read_pack));
	      break;

	    case IO_WRITE:
	      res_packet.stat = rs232c_write (&(rcv_packet.s.write_pack));
	      break;

	    case IO_STAT:
	      res_packet.stat = rs232c_stat (&(rcv_packet.s.stat_pack));
	      break;

	    case IO_CONTROL:
	      res_packet.stat = rs232c_control (&(rcv_packet.s.control_pack));
	      break;

	    default:
	      res_packet.stat = E_PAR;
	      break;
	    }
	}
      
      put_res (deviceid, &rcv_packet, &res_packet);
    }
}


/********************************************************************************
 * rs232c_intr --- RS232C �γ����ߥϥ�ɥ�
 *
 * ������
 *	�ʤ�
 *
 * �֤��͡�
 *	�ʤ�
 *
 * ��ǽ��
 *
 */
static void
rs232c_intr (void)
{
  DPRINTK(("rs232c_intr\n"))
}


/**********************************************************************************
 * 
 *
 * ������
 *
 * �֤��͡�
 *
 *
 * ��ǽ��
 *
 */
static ER
rs232c_read (struct io_read_packet *packet)
{
  DPRINTK(("rs232c_read\n"))
}

/**********************************************************************************
 * 
 *
 * ������
 *
 * �֤��͡�
 *
 *
 * ��ǽ��
 *
 */
static ER	
rs232c_write (struct io_write_packet *packet)
{
  DPRINTK (("rs232c_write\n"));
}

/**********************************************************************************
 * 
 *
 * ������
 *
 * �֤��͡�
 *
 *
 * ��ǽ��
 *
 */
static ER
rs232c_stat (struct io_stat_packet *packet)
{
  DPRINTK (("rs232c_stat\n"));
}

/**********************************************************************************
 * 
 *
 * ������
 *
 * �֤��͡�
 *
 *
 * ��ǽ��
 *
 */
static ER
rs232c_control (struct io_control_packet *packet)
{
  DPRINTK (("rs232c_control\n"));
}
