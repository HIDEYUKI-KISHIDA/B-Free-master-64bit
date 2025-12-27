/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#) $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/kernlib/port_manager.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/kernlib/port_manager.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

/*
 * $Log: port_manager.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.1  1999-04-18 17:48:33  monaka
 * Port-manager and libkernel.a is moved to ITRON. I guess it is reasonable. At least they should not be in BTRON/.
 *
 * Revision 1.5  1999/03/30 13:38:52  monaka
 * Minor fixes.
 *
 * Revision 1.4  1999/03/15 00:23:48  monaka
 * Some casts for rcv_mbf added.
 *
 * Revision 1.3  1997/10/11 16:21:11  night
 * �ݡ���̾�Υ��ԡ�������
 *
 * Revision 1.2  1997/09/23 13:52:20  night
 * �ǥХå�ʸ���ɲá�
 * find_port() �������� (name) �η����ѹ���
 *
 * Revision 1.1  1996/07/22  23:52:05  night
 * �ǽ����Ͽ
 *
 * Revision 1.4  1995/12/05 15:10:22  night
 * unregist_port () ���ɲá�
 *
 * Revision 1.3  1995/09/21  15:51:42  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.2  1995/06/28  14:14:25  night
 * print ʸ�� #ifdef DEBUG �� #endif �ǰϤä���
 *
 * Revision 1.1  1995/02/26  14:18:12  night
 * �ǽ����Ͽ
 *
 *
 */
/*
 * �ݡ��ȥޥ͡�����Ȥ��̿����ñ�˹Ԥ�����Υ饤�֥��ؿ���
 *
 * regist_port (PORT_NAME name, ID port);   ��å������Хåե� ID ����Ͽ
 * unregist_port (PORT_NAME name);	    ��å������Хåե� ID ������
 * find_port (PORT_NAME name, ID &port);    ��å������Хåե� ID �θ���
 * alloc_port (void);                       �ݡ��Ȥ�����
 */

#include <itron.h>
#include <errno.h>
#include <types.h>
#include "../servers/port-manager.h"
#include "libkernel.h"

/*
 * �ݡ��ȥޥ͡�����˥�å������Хåե��������Ͽ���롣
 * ���δؿ�����ǰ��Ū�˼����ѤΥ�å������ݡ��Ȥ�������롣
 *
 */
PORT_MANAGER_ERROR
regist_port (PORT_NAME *name, ID port)
{
  struct recv_port_message_t	recv_msg;
  struct port_manager_msg_t  	send_msg;
  ID			     	recv_port;
  W			     	rsize;


  /*
   * �ݡ��ȥޥ͡����㤫������������Ĥ��ѤΥ�å������Хåե�������
   */
  recv_port = get_port (sizeof (recv_msg), sizeof (recv_msg));
  if (recv_port == 0)
    {
#ifdef DEBUG
      dbg_printf ("regist_port: cannot get port.\n");
#endif /* DEBUG */
      return (E_PORT_SYSTEM);
    }

  /*
   * �ݡ��ȥޥ͡�����������׵��å������������
   */
  send_msg.hdr.type  = REGIST_PORT;
  send_msg.hdr.size  = sizeof (send_msg);
  send_msg.hdr.rport = recv_port;
  strncpy (send_msg.body.regist.name, *name, sizeof (send_msg.body.regist.name));
  send_msg.body.regist.port = port;

#ifdef DEBUG
  dbg_printf ("regist_port: name = <%s>\n", &(send_msg.body.regist.name));
#endif /* DEBUG */

  /*
   * �ݡ��ȥޥ͡�������Ф�����Ͽ�׵��å��������������롣
   */
  if (snd_mbf (PORT_MANAGER_PORT, sizeof (send_msg), &send_msg) != E_OK)
    {
      del_mbf (recv_port);
      return (E_PORT_SYSTEM);
    }

#ifdef DEBUG
  dbg_printf ("port_manager:snd_msg: ok.\n");
#endif /* DEBUG */

  /*
   * �ݡ��ȥޥ͡����㤫���������å������μ����Ĥ�
   */
  if (rcv_mbf (&recv_msg, (VP)&rsize, recv_port) != E_OK)
    {
      del_mbf (recv_port);
      return (E_PORT_SYSTEM);
    }

#ifdef DEBUG
  dbg_printf ("port_manager:rcv_msg: ok.\n");
#endif /* DEBUG */


  /*
   * �Ȥ�����ä����������ѤΥ�å������Хåե��������롣
   */
  del_mbf (recv_port);

#ifdef DEBUG
  dbg_printf ("port_manager: errno = %d\n", recv_msg.error);
#endif /* DEBUG */
  return (recv_msg.error);
}

PORT_MANAGER_ERROR
unregist_port (PORT_NAME *name)
{
  struct recv_port_message_t	recv_msg;
  struct port_manager_msg_t  	send_msg;
  ID			     	recv_port;
  W			     	rsize;


  /*
   * �ݡ��ȥޥ͡����㤫������������Ĥ��ѤΥ�å������Хåե�������
   */
  recv_port = get_port (sizeof (recv_msg), sizeof (recv_msg));
  if (recv_port == 0)
    {
#ifdef DEBUG
      dbg_printf ("regist_port: cannot get port.\n");
#endif /* DEBUG */
      return (E_PORT_SYSTEM);
    }

  /*
   * �ݡ��ȥޥ͡�����������׵��å������������
   */
  send_msg.hdr.type  = UNREGIST_PORT;
  send_msg.hdr.size  = sizeof (send_msg);
  send_msg.hdr.rport = recv_port;
  strncpy (send_msg.body.regist.name, *name, sizeof (send_msg.body.regist.name));

#ifdef DEBUG
  dbg_printf ("regist_port: name = <%s>\n", &(send_msg.body.regist.name));
#endif /* DEBUG */

  /*
   * �ݡ��ȥޥ͡�������Ф�����Ͽ�׵��å��������������롣
   */
  if (snd_mbf (PORT_MANAGER_PORT, sizeof (send_msg), &send_msg) != E_OK)
    {
      del_mbf (recv_port);
      return (E_PORT_SYSTEM);
    }

#ifdef DEBUG
  dbg_printf ("port_manager:snd_msg: ok.\n");
#endif /* DEBUG */

  /*
   * �ݡ��ȥޥ͡����㤫���������å������μ����Ĥ�
   */
  if (rcv_mbf (&recv_msg, (VP)&rsize, recv_port) != E_OK)
    {
      del_mbf (recv_port);
      return (E_PORT_SYSTEM);
    }

#ifdef DEBUG
  dbg_printf ("port_manager:rcv_msg: ok.\n");
#endif /* DEBUG */


  /*
   * �Ȥ�����ä����������ѤΥ�å������Хåե��������롣
   */
  del_mbf (recv_port);

#ifdef DEBUG
  dbg_printf ("port_manager: errno = %d\n", recv_msg.error);
#endif /* DEBUG */
  return (recv_msg.error);
}


/*
 * �ݡ��ȥޥ͡����㤫���å������Хåե�����򸡺����롣
 * ���δؿ�����ǰ��Ū�˼����ѤΥ�å������ݡ��Ȥ�������롣
 *
 */
PORT_MANAGER_ERROR
find_port (B *name, ID *rport)
{
  struct recv_port_message_t	recv_msg;
  struct port_manager_msg_t  	send_msg;
  ID			     	recv_port;
  W			     	rsize;


#ifdef DEBUG
  dbg_printf ("find_port, name = <%s>\n", name);		/* */
#endif

  /*
   * �ݡ��ȥޥ͡����㤫������������Ĥ��ѤΥ�å������Хåե�������
   */
  recv_port = get_port (sizeof (recv_msg), sizeof (recv_msg));
  if (recv_port == 0)
    {
#ifdef DEBUG
      dbg_printf ("regist_port: cannot get port.\n");
#endif /* DEBUG */
      return (E_PORT_SYSTEM);
    }

#ifdef DEBUG
  dbg_printf ("strcpy (0x%x, %s)\n",
	      &(send_msg.body.find.name), name);
#endif /* DEBUG */
  /*
   * �ݡ��ȥޥ͡�����������׵��å������������
   */
  send_msg.hdr.type  = FIND_PORT;
  send_msg.hdr.size  = sizeof (send_msg);
  send_msg.hdr.rport = recv_port;
#ifdef notdef
  strcpy (&(send_msg.body.find.name), name);
#else
  strncpy (send_msg.body.find.name, name, sizeof (send_msg.body.find.name));
#endif

#ifdef DEBUG
  dbg_printf ("find_port: name = <%s>\n", &(send_msg.body.find.name));
#endif /* DEBUG */
  /*
   * �ݡ��ȥޥ͡�������Ф��Ƹ����׵��å��������������롣
   */
  if (snd_mbf (PORT_MANAGER_PORT, sizeof (send_msg), &send_msg) != E_OK)
    {
      del_mbf (recv_port);
      return (E_PORT_SYSTEM);
    }

#ifdef DEBUG
  dbg_printf ("port_manager:snd_msg: ok.\n");
#endif /* DEBUG */

  /*
   * �ݡ��ȥޥ͡����㤫���������å������μ����Ĥ�
   */
  if (rcv_mbf (&recv_msg, (INT *)&rsize, recv_port) != E_OK)
    {
#ifdef DEBUG
  dbg_printf ("port_manager:rcv_mbf: failed.\n");
#endif /* DEBUG */
      del_mbf (recv_port);
      return (E_PORT_SYSTEM);
    }

#ifdef DEBUG
  dbg_printf ("port_manager:rcv_msg: ok.\n");
#endif /* DEBUG */

  /*
   * �ݡ��ȥޥ͡����㤬���������ݡ����ֹ���֤���
   */
  *rport = recv_msg.port;

  /*
   * �Ȥ�����ä����������ѤΥ�å������Хåե��������롣
   */
  del_mbf (recv_port);

#ifdef DEBUG
  dbg_printf ("port_manager: errno = %d\n", recv_msg.error);
#endif /* DEBUG */
  return (recv_msg.error);
}

/*
 * ��å������Хåե����������롣
 * ��å������Хåե� ID �ϡ���ưŪ�˶����Ƥ����Τ���Ѥ��롣
 *
 */
ID
alloc_port (W size, W max_entry)
{
  ID		msg_port;
  T_CMBF	create_argument;

  /*
   * �׵�����Ĥ��Τ���Υ�å������Хåե���������롣
   * ��å������Хåե��� ID ���ä˷�ޤäƤ��ʤ��������Ƥ����å���
   * ���Хåե���Ŭ�������֡�
   */
  create_argument.bufsz  = size;
  create_argument.maxmsz = size * max_entry;
  create_argument.mbfatr = TA_TFIFO;
  for (msg_port = MIN_USERMBFID;
       msg_port <= MAX_USERMBFID;
       msg_port++)
    {
      if (cre_mbf (msg_port, &create_argument) == E_OK)
	{
	  /*
	   * ��å������Хåե��μ���������������
	   */
	  return (msg_port);
	}
    }

  /*
   * ��å������Хåե��������Ǥ��ʤ��ä���
   */
#ifdef DEBUG
  dbg_printf ("posix.process server: cannot allocate messege buffer\n");
#endif /* DEBUG */
  return (0);
}
