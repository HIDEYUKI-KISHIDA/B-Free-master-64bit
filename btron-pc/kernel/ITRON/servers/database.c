/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#) $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/servers/database.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */

static char rcs[] = "@(#) $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/servers/database.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/*
 * $Log: database.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.1  1999-04-18 17:48:36  monaka
 * Port-manager and libkernel.a is moved to ITRON. I guess it is reasonable. At least they should not be in BTRON/.
 *
 * Revision 1.1  1996/07/23 00:03:04  night
 * IBM PC �Ѥκǽ����Ͽ
 *
 * Revision 1.4  1995/12/05 15:16:28  night
 * ������������ݡ��ȥơ��֥�� 0 ���� 1 �ޤǤ���Ĵ�٤Ƥ��ʤ��ä���
 * ����� MAX_PORT_ENTRY(= 100)�ޤǸ�������褦�ˤ�����
 *
 * Revision 1.3  1995/09/21  15:51:48  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.2  1995/06/26  15:18:56  night
 * �����Ĥ��� printf �� DEBUG �ޥ����ǰϤ����
 *
 * Revision 1.1  1995/03/18  14:12:47  night
 * �ǽ����Ͽ
 *
 *
 */

/*
 * ��å������Хåե���ǡ����١����������뤿��Υ⥸�塼�롣
 * ̾���ȥ�å������Хåե� ID �Ȥ��ӤĤ��롣
 *
 * ���Υ⥸�塼��ϡ��������Ф��Ƽ��δؿ���������롣
 *
 *   init_regist_table (void)
 *   regist_database (PORT_NAME *name, ID port, ID task)
 *   unregist_database (PORT_NAME *name, ID port, ID task)
 *   find_database (PORT_NAME *name, ID *port)
 *
 *
 * �ǡ����δ�����ˡ�Ȥ��Ƥϡ�ñ��ʥơ��֥������Ǵ������롣
 * ���ʤ���������Ƥ���ǽ�Υ���ȥ����Ͽ����õ���Ȥ��ˤϡ�
 * �ǽ�Υ���ȥ꤫����֤�õ���Ƥ�����
 *
 */


/*
 * ɬ�פʥ��󥯥롼�ɥե�������ɤ߹��ߡ�
 */
#include <itron.h>
#include <errno.h>
#include <types.h>
#include "port-manager.h"

/* Local prototypes for kernlib helpers we rely on. */
extern void bzero (void *bufp, W size);
extern void strncpy (B *dest, B *src, W size);
extern W strncmp (B *s1, B *s2, W size);


/*
 * �ǡ�������Ͽ���륨��ȥꡣ
 * ̾���ȥ�å������Хåե��� ID ��������Ͽ������������ ID �� 
 * 3 �Ĥξ������롣
 * ���ι�¤�Τϡ����Υե�������椷���Ȥ�ʤ���
 *
 */
struct data_entry_t
{
  PORT_NAME	name;		/* ��å������Хåե��˷�ӤĤ���̾����   */
				/* ��Ͽ����Ȥ��˻��ꤹ�롣               */

  ID		port;		/* ��å������Хåե� ID                  */
				/* �������Ǥ��ͤ� 0 �ΤȤ��ˤϡ����Υ�    */
  				/* ��ȥ�ϡ��ȤäƤ��ʤ����Ȥ�ɽ����     */

  ID		task;		/* ��å������Хåե�����Ͽ���������� */
};


/*
 * ����ơ��֥�
 */
static struct data_entry_t	table[MAX_PORT_ENTRY];


/*
 * �ơ��֥뤫��̾���ˤ�äƥ���ȥ�򸡺�����ؿ���
 * unregist_database() �� find_database() �ǻ��Ѥ��롣
 */
static W	find_entry (PORT_NAME name);


/*
 * �ǡ����١����ν����
 * ����ơ��֥� table �ѿ������Ǥ������ꤹ�롣
 * (0 ������)
 */
void
init_regist_table (void)
{
  bzero (table, sizeof (table));
}


/*
 * �ǡ�������Ͽ����
 * table �ѿ��ζ����Ƥ��륨��ȥ�򸫤Ĥ�����å������Хåե���
 * ��Ͽ���롣
 *
 */
PORT_MANAGER_ERROR
regist_database (PORT_NAME name, ID port, ID task)
{
  W	counter;		/* �����Ƥ��륨��ȥ��ơ��֥뤫�� */
				/* ���Ĥ���Ȥ��˻��Ѥ��륫����   */

  /*
   * �ơ��֥�Ʊ��̾���Υ���ȥ꤬�ʤ����򸡺����롣
   * �⤷��Ʊ��̾�����ĥ���ȥ꤬���ä����ˤϡ�
   * E_PORT_DUPLICATION �Υ��顼���֤���
   */
  if (find_entry (name) >= 0)
    {
      return (E_PORT_DUPLICATION);	/* Ʊ��̾�����ĥ���ȥ꤬�� */
					/* �Ǥˤ��ä����顼���֤���   */
    }

  /*
   * �ơ��֥����Ƭ��������Ƥ��륨��ȥ��õ����
   */
  for (counter = 0; counter < MAX_PORT_ENTRY; counter++)
    {
      /*
       * �⤷������ȥ�� ID ���Ǥ� 0 �ʤ�С����Υ���ȥ��
       * �Ȥ��Ƥ��ʤ���
       */
      if (table[counter].port == 0)
	{
	  /*
	   * �����Ƥ��륨��ȥ�򸫤Ĥ�����
	   * ��å������Хåե�����Ͽ���롣
	   */
	  strncpy (table[counter].name, name, (PORT_NAME_LEN + 1));
	  table[counter].port = port;
	  table[counter].task = task;
#ifdef DEBUG
	  dbg_printf ("regist: %s, %s\n", &(table[counter].name), name);
#endif /* DEBUG */
	  return (E_PORT_OK);
	}
    }

  /*
   * �����Ƥ��륨��ȥ꤬ȯ���Ǥ��ʤ��ä���
   * E_PORT_FULL �Υ��顼���֤���
   */
  return (E_PORT_FULL);
}


/*
 * �ǡ����١���������ꤷ��̾�����ĥ���ȥ�������롣
 * �⤷���б����륨��ȥ꤬���Ĥ����ʤ��ä����ˤϡ�E_PORT_NOTFOUND 
 * �Υ��顼���֤���
 *
 * ���� name �ǻ��ꤷ������ȥ�򸫤Ĥ�������ȥ�����Ƥ� 0 �����롣
 * 
 * ���δؿ��ϡ���������å������Хåե��� ID ����� port ���֤���
 *
 */
PORT_MANAGER_ERROR
unregist_database (PORT_NAME name, ID *port, ID task)
{
  W	index;		/* �ǡ����١������鸫�Ĥ�������ȥ� */

  index = find_entry (name);
  if (index < 0)
    {
      return (E_PORT_NOTFOUND);	/* ����ȥ��ֹ椬 -1 ���ä�;       */
				/* ���� name �˳������륨��ȥ꤬  */
				/* �ʤ��ä��Τǡ�E_PORT_NOTFOUND �� */
      				/* ���顼���֤���                  */
    }

  /*
   * ��å������Хåե� ID ����� port ���������롣
   */
  *port = table[index].port;

  /*
   * ��å������Хåե�����Ͽ�����������������׵��Ԥä���������
   * �ۤʤäƤ������ˤϡ�E_PORT_INVALID �Υ��顼�Ȥ��롣
   */
  if (table[index].task	 != task)
    {
      return (E_PORT_INVALID);
    }

  /*
   * find_entry() �ˤ�äơ�ȯ����������ȥ�� 0 �����롣
   */
  bzero (&table[index], sizeof (struct data_entry_t));

  return (E_PORT_OK);  
}


/*
 * ���� name �ǻ��ꤷ��̾�����ĥ���ȥ�򸫤Ĥ��ƥ�å������Хåե� 
 * ID ���֤���
 * �⤷���ơ��֥����˳�������̾�����ĥ���ȥ꤬�ʤ��ä����ˤϡ�
 * E_PORT_NOTFOUND �Υ��顼���֤���
 *
 */
PORT_MANAGER_ERROR
find_database (PORT_NAME name, ID *port)
{
  W	index;	/* �ǡ����١����ơ��֥뤫�鸫�Ĥ�������ȥ��ɽ��  */
		/* table �ѿ��Υ���ǥå�����			   */

  /*
   * �б����륨��ȥ꤬�ʤ�����õ����
   * find_entry() ��ȤäƤ��롣
   */
  index = find_entry (name);
  if (index == -1)
    {
      /*
       * find_entry() ���֤��ͤ� -1 ���ä���
       * �б����륨��ȥ꤬�ʤ��Τǡ�E_PORT_NOTFOUND �Υ��顼���֤���
       */
      return (E_PORT_NOTFOUND);
    }

  /*
   * ���� port ��õ���Ф�������ȥ�����äƤ����å������Хåե��� id 
   * �����ꤹ�롣 
   */
  *port = table[index].port;

  return (E_PORT_OK);		/* ���ｪλ��E_PORT_OK ���֤��� */
}


/*
 * ����ơ��֥뤫�饨��ȥ�򸫤Ĥ��뤿��β������ؿ���
 * �֤��ͤȤ��ƾ���ơ��֥�Υ���ǥå����ֹ���֤���
 * �ơ��֥뤫����� name ���б�����̾�����ĥ���ȥ꤬ȯ���Ǥ��ʤ���
 * �����ˤϡ�-1 ���֤���
 */
static W
find_entry (PORT_NAME name)
{
  W	index;			/* ����ơ��֥�򥵡�������Ȥ��ˡ��� */
				/* �Ѥ��륫���󥿡�                   */

  /*
   * ��������̾�����ĥ���ȥ꤬�ʤ����ɤ��������ơ��֥� (table) ��
   * ��Ƭ���鸡�����롣
   */
  for (index = 0; index < MAX_PORT_ENTRY; index++)
    {
#if defined(DEBUG)
      dbg_printf ("find_entry: <%s>, <%s>\n", 
		  table[index].name, name);
#endif /* DEBUG */

      if (strncmp (name, table[index].name, PORT_NAME_LEN) == 0)
	{
	  /*
	   * ���� name ��Ʊ��̾�����ĥ���ȥ��ȯ��������
	   * ����ơ��֥�� ����ǥå����ͤ��֤���
	   */
	  return (index);
	}
    }

  /*
   * ����ơ��֥�ˤϡ��������륨��ȥ꤬�ʤ��ä���
   * -1 ���֤���
   */
  return (-1);
}
