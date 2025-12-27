/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#) $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/servers/port-manager.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */

static char rcs[] = "@(#) $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/servers/port-manager.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

/*
 * $Log: port-manager.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.1  1999-04-18 17:48:36  monaka
 * Port-manager and libkernel.a is moved to ITRON. I guess it is reasonable. At least they should not be in BTRON/.
 *
 * Revision 1.3  1999/04/13 04:14:58  monaka
 * MAJOR FIXcvs commit -m 'MAJOR FIX!!! There are so many changes, modifys, fixes. Sorry but I can't remember all of those. For example, all the manager and driver programmer have got power to access all ITRON systemcall. (My works is just making access route to ITRON. I don't know what happens in the nuclus.'! There are so many changes, modifys, fixes. Sorry but I can't remember all of those. For example, all the manager and driver programmer have got power to access all ITRON systemcall. (My works is just making access route to ITRON. I don't know what happens in the nuclus.
 *
 * Revision 1.2  1999/02/17 14:56:35  night
 * ��å������Хåե�°���ν�����������ɲ�
 *
 * +  msg_pk.mbfatr = TA_TFIFO;
 *
 * Revision 1.1  1996/07/23 00:03:04  night
 * IBM PC �Ѥκǽ����Ͽ
 *
 * Revision 1.3  1995/09/21  15:51:48  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.2  1995/06/26  15:19:15  night
 * �����Ĥ��� printf �� DEBUG �ޥ����ǰϤ����
 *
 * Revision 1.1  1995/03/18  14:12:45  night
 * �ǽ����Ͽ
 *
 *
 */

/*
 * �ݡ��ȥޥ͡�����
 *
 * <���Υ����Фϡ��ץ������Ķ� (BTRON or POSIX) �˴ط��ʤ�ư���>
 *
 *
 * ���ճˤ����ץꥱ���������̿����뤿��˻��Ѥ����å������Хåե�
 * �� ID ��������롣
 * ITRON (�濴��) �ϥ�å������Хåե��ˤ���̿����Ǥ��롣��������
 * ���ΤޤޤǤϡ����ճˤ��ɤΥ�å������Хåե���ȤäƤ��뤫�狼���
 * ����
 * �ݡ��ȥޥ͡�����ϡ��������Х��̾���ȥ�å������Хåե��� ID ����
 * ��������롣�������ϡ��ݡ��ȥޥ͡�������Ф�����Ͽ���뤳�Ȥˤ�äơ�
 * ¾�Υ����������å������Хåե��򥢥������Ǥ���褦�ˤ��롣
 *
 * ���⤷�ʤ��Ƥ�ݡ��ȥޥ͡�������Ф��ƥ��������Ǥ���褦�ˡ��ݡ���
 * �ޥ͡����㼫�Ȥϡ�����Υ�å������Хåե� ID (11) ��Ȥ���
 * 
 * �ݡ��ȥޥ͡�����ϡ�(���ճˤ�Ʊ�ͤ�) �����ФȤ���ư��� (�桼���⡼
 * ��(CPU �ø���٥� 3)��ư���)
 *
 * �ݡ��ȥޥ͡�����Υ�å������ϼ��η������Ѱդ���Ƥ��롣
 *
 * regist_port_t	{ PORT_NAME name; ID port; ID task; }
 * unregist_port_t	{ PORT_NAME name; ID task; }
 * find_port_t		{ PORT_NAME name; }
 *
 * ����ˡ����˼����إå����ä�ä���å��������ݡ��ȥޥ͡����������
 * ��뤳�Ȥˤʤ롣
 *
 * msg_header { W msg_type; W size };
 *
 *
 * �ݡ��ȥޥ͡�������Ф����׵���ά�����뤿��ˡ��饤�֥�� 
 * (libport.a) ���Ѱդ��Ƥ��롣���Υ饤�֥��ϡ��ݡ��ȥޥ͡��������
 * �����å���������������Ԥ��ؿ���¾�˥�å������򰷤������������
 * �ؿ������äƤ��롣
 * �ǡ����Υ饤�֥��ˤĤ��Ƥϡ��ݡ��ȥޥ͡�����˴ؤ���ؿ��Ȥ��Ƥϡ�
 * ���Τ�Τ����롣
 *
 * regist_port (PORT_NAME name, ID port);   ��å������Хåե� ID ����Ͽ
 * unregist_port (PORT_NAME name);	    ��å������Хåե� ID ������
 * find_port (PORT_NAME name, ID &port);    ��å������Хåե� ID �θ���
 * 
 */


#include <itron.h>
#include <errno.h>
#include <types.h>
#include "port-manager.h"


/*
 * �������Х��ѿ������
 */
ID	request_port;


/*
 * �ݡ��ȥޥ͡�����ǻȤ��ؿ��Υץ��ȥ����������
 *
 */
extern void	_main (void);
extern void	regist_port (struct port_manager_msg_t *msgp);
extern void	unregist_port (struct port_manager_msg_t *msgp);
extern void	find_port (struct port_manager_msg_t *msgp);
extern W	dbg_printf (B *fmt,...);
extern ER	dbg_puts (B *msg);


/*
 * ���Υե��������Ǥ������Ѥ��ʤ������ƥ��å��ؿ������
 */
static void	recv_port_manager (ID rport, PORT_MANAGER_ERROR errno, ID port);


/*
 *	�ݡ��ȥޥ͡������ main ����
 *	��å�������������������Ԥ������θ��å���������� - ��������
 *	�롼�פ����롣
 */
void
_main (void)
{
  T_CMBF				msg_pk;
  ER					error;
  struct port_manager_msg_t		msg_buf;
  INT					size;


  /*
   * �ץ������ν����
   * ��å������Хåե����롣
   * ��å������Хåե� ID �ϡ�PORT_MANAGER_PORT �ޥ����ǻ��ꤷ��
   * ��Τ���Ѥ��롣
   *
   *
   * <����γ�ĥ�ؤΥ����ǥ�>
   *
   * ����ϡ���å������Хåե��������˥��������������Ǥ���褦�ˤ��롣
   * ��������������ꤹ�뤳�Ȥˤ�äơ���å������Хåե���������������
   * ���ʳ��ϡ���å��������ɤ߼��ʤ��ʤɤ�������ǽ�Ȥ��롣
   *
   */
  msg_pk.bufsz = sizeof (struct port_manager_msg_t);
  msg_pk.maxmsz = sizeof (struct port_manager_msg_t) * MAX_MSG_ENTRY;
  msg_pk.mbfatr = TA_TFIFO;
  error = cre_mbf (PORT_MANAGER_PORT, &msg_pk);
  if (error != E_OK)
    {
      exd_tsk ();
      /* NOT REACHED */
    }

  /* 
   * ��Ͽ�ơ��֥�ν����
   */
  init_regist_table ();

  dbg_puts ("port manager start.\n");
  /*
   *	��å����������Ƚ���
   *	�ݡ��ȥޥ͡�����ϡ����󥰥륿������ư��롣
   *	���Τ��ᡢ��å������μ�����������ν�λ�ޤǤϡ�¾���׵�ϼ���
   *	�Ĥ��ʤ���
   */
  for (;;)
    {
      /* 
       * ��å�������������롣
       * ��������Ȥ��˻��Ѥ����å������Хåե��ϡ�PORT_MANAGER_PORT
       * �ޥ����ǻ��ꤷ����Τ���Ѥ��롣
       */
      error = rcv_mbf (&msg_buf, &size, PORT_MANAGER_PORT);
      if (error == E_OK)
	{
	  /*
	   * ��å�����������������Ȥ�ɽ�����롣
	   * ����ϡ��ǥХå��Τ�������줿��
	   */
#ifdef DEBUG
	  dbg_puts ("port-manager: message read.\n");
#endif /* DEBUG */

	  /*
	   * ��å������ν���
	   * ����������å������Υإå��μ��फ��Ŭ�ڤʽ�����Ԥ���
	   */
	  switch (msg_buf.hdr.type)
	    {
	    case REGIST_PORT:	
	      /*
	       * ��å������Хåե� ID ����Ͽ
	       */
	      dbg_printf ("port-manager: (regist) <%s>\n", msg_buf.body.regist.name);
	      regist_port (&msg_buf);
	      break;

	    case UNREGIST_PORT:
	      /*
	       * ��å������Хåե� ID ������
	       */
	      unregist_port (&msg_buf);
	      break;

	    case FIND_PORT:
	      /*
	       * ��å������Хåե� ID �θ���
	       */
	      find_port (&msg_buf);
	      break;
	    }
	}
      /* NOT REACHED */
    }
}

/*
 * ��å������Хåե� ID ����Ͽ������Ԥ���
 *
 */
void
regist_port (struct port_manager_msg_t *msgp)
{
  PORT_MANAGER_ERROR errno;

  /*
   * �ǡ����١�������Ͽ����
   */
  errno = regist_database (msgp->body.regist.name,
			   msgp->body.regist.port,
			   msgp->body.regist.task);

#ifdef DEBUG  
  dbg_printf ("port-server: regist_port: <%s>\n", msgp->body.regist.name);
#endif /* DEBUG */

  /*
   * �׵�����긵���Ф���������å����������롣
   * ����������˽�λ�������Ȥ������å���������äƤ���Τǡ�
   * ����������2�̤�¸�ߤ��롣
   */
  if (errno == E_PORT_OK)
    {
      /*
       * ����˽�������λ��������������å���������
       */
      recv_port_manager (msgp->hdr.rport,
			 errno, 
			 msgp->body.regist.port);
    }
  else
    {
      /*
       * ����˽����������ʤ��ä�����������å�����������
       * ������å������Τ�����å������Хåե� ID �ˤĤ��Ƥ�
       * �����Ǥ��ʤ��ä��Τǡ�0 ���֤���
       */
      recv_port_manager (msgp->hdr.rport, errno, 0);
    }
}



/*
 * ��å������Хåե� ID ����Ͽ���ý�����Ԥ���
 *
 */
void
unregist_port (struct port_manager_msg_t *msgp)
{
  PORT_MANAGER_ERROR	errno;
  ID		     	port;

  /*
   * �ǡ����١����������������롣
   */
  errno = unregist_database (msgp->body.unregist.name,
			     &port,
			     msgp->body.unregist.task);

  /*
   * ��Ͽ���ä��׵ᤷ����������������å��������֤���
   */
  if (errno != E_PORT_OK)
    {
      /*
       * ����˽�������λ��������������å���������
       */
      recv_port_manager (msgp->hdr.rport,
			 errno, 
			 port);
    }
  else
    {
      /*
       * �ǡ����١������饨��ȥ�����Ǥ��ʤ��ä���
       * ���顼��å��������������롣
       * ���顼�ֹ�ʳ������ƤϤ��٤� 0 �������֤���
       */
      recv_port_manager (msgp->hdr.rport, errno, 0);
    }
}

/*
 * ��å������Хåե� ID �θ���
 * 
 *
 */
void
find_port (struct port_manager_msg_t *msgp)
{
  PORT_MANAGER_ERROR	errno;
  ID		     	port;

  /*
   * �ǡ����١����������򸡺����롣
   */
  errno = find_database (msgp->body.find.name, &port);

  /*
   * �ǥХå�ʸ���ǡ����١�������θ������
   */
#ifdef DEBUG
  dbg_printf ("port-manager: find_port: errno = %d, port = %d\n", errno, port);
#endif /* DEBUG */

  /*
   * ��å������Хåե� ID ���׵ᤷ����������������å��������֤���
   */
  if (errno == E_PORT_OK)
    {
      /*
       * ����˽�������λ��������������å�����������
       * �ݡ����ֹ�Ȥ��ơ�find_database() ��ȯ��������å������Хåե� 
       * ID ���֤���
       */
      recv_port_manager (msgp->hdr.rport,
			 errno, 
			 port);
    }
  else
    {
      /*
       * �ǡ����١������饨��ȥ�����Ǥ��ʤ��ä���
       * ���顼��å��������������롣
       * ���顼�ֹ�ʳ������ƤϤ��٤� 0 �������֤���
       */
      recv_port_manager (msgp->hdr.rport, errno, 0);
    }
}


/*
 * �ݡ��ȥޥ͡�������Ф����׵�����ä����������Ф���������å��������֤���
 */
static void
recv_port_manager (ID rport, PORT_MANAGER_ERROR errno, ID port)
{
  /*
   * ������å����������Ƥ������ΰ衣
   * ��¤�� port_recv_port_message_t �ˤ�äƹ�¤���ꤹ�롣
   */
  struct recv_port_message_t	recv_msg;

  /*
   * ������å��������Ȥ�Ω��
   */
  recv_msg.error = errno;	/* ���顼�ֹ������ */
  recv_msg.port  = port;	/* ��å������Хåե� ID ������ */

  /*
   * ������å��������׵ḵ�����롣
   * ���ΤȤ�����snd_mbf() �����ƥॳ����Υ��顼��̵�뤷�Ƥ��롣
   */
  snd_mbf (rport, sizeof (recv_msg), &recv_msg);
}

