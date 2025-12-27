/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* gdc7220.c --- ����ե��å��ǥ����ץ쥤����ȥ����� PD7220 �Υɥ饤��
 *
 *
 * 
 */


#include "itron.h"
#include "errno.h"
#include "task.h"
#include "misc.h"
#include "func.h"
#include "interrupt.h"
#include "../h/graphics.h"
#include "../io/io.h"

#define PLANE0		0x800A8000
#define PLANE1		0x800B0000
#define PLANE2		0x800B8000
#define PLANE3		0x800E0000



/* ���Υե�����ǻ��Ѥ���ޥ��� */
#define CHK_ERR		if (err) return (err);



/* GDC �˴ط����Ƥ���IO�ݡ��Ȥ���� */
#define GDC_COMMAND	0xa2		/* �饤�ȥ��ޥ��	*/
#define GDC_OUT		0xa0		/* �饤�ȥѥ�᡼��	*/
#define GDC_IN		0xa2		/* �꡼�ɥǡ���		*/
#define GDC_STATUS	0xa0		/* �꡼�ɥ��ơ�����	*/
#define GDC_DISLAY_SEL	0xa4		/* ɽ����������		*/
#define GDC_DRAW_SEL	0xa6		/* �����������		*/
#define GDC_PAL_D	0xa8		/* �饤�ȥѥ�åȥ쥸���� D */
#define GDC_PAL_C	0xaa		/* �饤�ȥѥ�åȥ쥸���� C */
#define GDC_PAL_B	0xac		/* �饤�ȥѥ�åȥ쥸���� B */
#define GDC_PAL_A	0xae		/* �饤�ȥѥ�åȥ쥸���� A */

/* GDC ���ޥ�� */
#define GDC_RESET	0x0000		/* �ꥻ�åȥ��ޥ��	    */

/* sync ���ޥ�� */
#define MODE_DISP	0x0f		/* ɽ������		*/
#define MODE_UNDISP	0x0e		/* ɽ�����		*/


/* ��������ؿ� */
static ER	gdc_reset(void);			/* GDC�Υꥻ�åȤ�Ԥ�			*/
static ER	gdc_display(void);			/* ɽ������				*/
static ER	gdc_undisplay(void);			/* ɽ�����				*/
static ER	gdc_line (W x1, W y1, W x2, W y2);	/* ���̾��ľ�������			*/
static ER	gdc_circle (W x, W y, W r);		/* ���̾�˱ߤ�����			*/
static ER	gdc_dot (W x, W y);			/* ���̾����������			*/
static ER	gdc_write (W num, ...);			/* GDC�˥��ޥ�ɤ����Ф��� 		*/
static ER	point2addr (W x, W y);			/* POINT2ADDR: x,y ��ɸ���� VRAM	*/ 
							/* ��Υ��ɥ쥹��׻����� 		*/

/* SYNC ���ޥ�ɡ����̤γƼ����� */
static ER	gdc_sync (B mode, UH column, UH vs, UH hs, UH hfp, UH hbp, UH vfp, UH line, UH vbp);

static void	gdc_server (void);

static ID	deviceid;	/* �ǥХ����ֹ�(�ǥХ����ơ��֥�Υ���ȥ��ֹ�) */
				/* ���ֹ�Ȥ��ƻ��Ѥ��롣*/
static ID	taskid;		/* �Уģã��������ɥ饤�ФΥ������ɣ� */



/*****************************************************************************
 * init_gdc7220 --- PD7220 �ν����
 *
 * ������
 *	�ʤ�
 *
 * �֤��͡�
 *	���顼�ֹ�
 *	E_OK	���ｪλ
 *
 * ��ǽ��
 *	����ե��å� GDC �ν������Ԥ���
 *	�������ꤹ�롧
 *	  ��������	640x400
 *	  ���顼����	16 Color
 *
 *
 * �������ס�
 *	1. RESET���ޥ�ɤ�ȯ��
 *	2. ư��⡼�ɤ����� 
 *	3. ɽ���γ���
 *
 *	4. �ɥ饤����Ͽ�μ¹�
 *
 */
ER
init_pd7220 (void)
{
  ER	err;
  T_CTSK	pktsk;

/*  err = gdc_reset ();
  CHK_ERR;
  err = gdc_sync (MODE_DISP, 80, 0x08, 0x07, 0, 0, 0, 400, 0);
  CHK_ERR;
*/
  gdc_display ();

  err = def_dev (DEVNAME_PD7220, CHAR, ANY_DEVICE, &deviceid);
  if (err != E_OK)
    {
      printk ("cannot initialize for PD7220 device. err = %d\n", err);
      return (err);
    }
  pktsk.tskatr = TA_HLNG;
  pktsk.startaddr = gdc_server;
  pktsk.itskpri = 1;
  pktsk.stksz = PAGE_SIZE * 2;
  pktsk.addrmap = NULL;
  err = new_task (&pktsk, &taskid, TRUE);
  if (err != E_OK)
    {
      printk ("cannot create task for PD7220. err = %d\n", err);
      return (err);
    }
  printk ("PD7220: TASK ID = %d\n", taskid);
  return (E_OK);
}


static void
gdc_server (void)
{
  T_IO_REQUEST	rcv_packet;
  T_IO_RESPONSE	res_packet;
  ER		err;
  
  printk ("PD7220(GDC): server start.\n");
  for (;;)
    {
      err = get_ioreq (deviceid, &rcv_packet);
      if (err == E_OK)
	{
	  printk ("GDC: Receive request %d\n", rcv_packet.command);
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
	      res_packet.stat = E_NOSPT;
	      break;

	    case IO_WRITE:
	      res_packet.stat = E_NOSPT;
	      break;

	    case IO_STAT:
	      res_packet.stat = E_NOSPT;
	      break;

	    case IO_CONTROL:
	      res_packet.stat = gdc_control (&(rcv_packet.s));
	      break;

	    default:
	      res_packet.stat = E_PAR;
	      break;
	    }
	}
      
      put_res (deviceid, &rcv_packet, &res_packet);
    }
}

ER
gdc_control (struct io_control_packet *pack)
{
  ER	err;
  struct graphic_packet *arg;

  arg = pack->argp;
  switch (arg->command)
    {
    case Draw_Line:
      err = gdc_line (arg->b.line.x1, arg->b.line.x1, arg->b.line.x1, arg->b.line.x1);
      break;

    default:
      printk ("GDC: unknown request %d\n", arg->command);
      err = E_NOSPT;
      break;
    }
  return (err);
}

/* ------------------------------------------------------------------------------ */


/* GDC�Υꥻ�åȤ�Ԥ�	*/
static ER
gdc_reset(void)
{
  gdc_write (1, GDC_RESET);
}


/* ɽ������		*/
static ER
gdc_display(void)
{

}


/* ɽ�����		*/
static ER	
gdc_undisplay(void)
{

}


/* ���̾��ľ�������	*/
static ER
gdc_line (W x1, W y1, W x2, W y2)	
{
  printk ("gdc: line (%d, %d) - (%d, %d)\n", x1, y1, x2, y2);
}


/* ���̾�˱ߤ�����	*/
static ER
gdc_circle (W x, W y, W r)
{

}


/* ���̾����������	*/
static ER
gdc_dot (W x, W y)
{
  VB	*addr;
  B	buf; 

/* �ץ졼�� 0 */
  addr = (VB *)PLANE0;	/* VRAM ����Ƭ���ɥ쥹 */
  addr += (y * (640 / 8)) + (x / 8);
  buf = *addr;
  buf |= 1 << (x % 8);
  *addr = buf;

/* �ץ졼�� 1 */
  addr = (VB *)PLANE1;	/* VRAM ����Ƭ���ɥ쥹 */
  addr += (y * (640 / 8)) + (x / 8);
  buf = *addr;
  buf |= 1 << (x % 8);
  *addr = buf;

/* �ץ졼�� 2 */
  addr = (VB *)PLANE2;	/* VRAM ����Ƭ���ɥ쥹 */
  addr += (y * (640 / 8)) + (x / 8);
  buf = *addr;
  buf |= 1 << (x % 8);
  *addr = buf;

/* �ץ졼�� 3 */
  addr = (VB *)PLANE3;	/* VRAM ����Ƭ���ɥ쥹 */
  addr = (y * (640 / 8)) + (x / 8);
  buf = *addr;
  buf |= 1 << (x % 8);
  *addr = buf;
}


/* SYNC ���ޥ�ɡ����̤γƼ����� */
static ER
gdc_sync (B mode, UH column, UH vs, UH hs, UH hfp, UH hbp, UH vfp, UH line, UH vbp)
{
  printk ("gdc: SYNC\n");
  printk ("     Not work.\n");
}



/* GDC�˥��ޥ�ɤ����Ф��� */
static ER
gdc_write (W num, ...)
{

}


