/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* sys_task.c -- �������ط��Υ����ƥॳ����
 *
 * $Id: sys_task.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $
 */

#include "../../ITRON/h/types.h"
#include "../../ITRON/h/itron.h"
#include "../../ITRON/h/syscall.h"
#include "../../ITRON/h/errno.h"


/* cre_tsk  --- ������������
 *
 * ������
 *
 * �֤��͡�
 *
 */
ER
cre_tsk (ID tskid, T_CTSK *pk_ctsk)
{
  return call_syscall (SYS_CRE_TSK, tskid, pk_ctsk);
}


ER
vcre_tsk (T_CTSK *pk_ctsk, ID *rid)
{
  W	i;
  ER	err;

  for (i = MIN_USERTASKID; i <= MAX_USERTASKID; i++)
    {
      err = cre_tsk (i, pk_ctsk);
      if (err == E_OK)
	{
	  *rid = i;
	  return (E_OK);
	}
    }
  return (E_NOMEM);
}


/* ext_tsk  --- ����������λ
*/
void
ext_tsk (void)
{
  call_syscall (SYS_EXT_TSK);
}

/* exd_tsk  --- ����������λ�Ⱥ��
*/
void
exd_tsk (void)
{
  call_syscall (SYS_EXD_TSK);
}

/* can_wup  --- �������ε����׵��̵����
*/
ER
can_wup (INT *p_wupcnt, ID taskid)
{
  return call_syscall (SYS_CAN_WUP, p_wupcnt, taskid);
}

/* chg_pri  --- �ץ饤����ƥ����ѹ�
*/
ER
chg_pri (ID tskid, PRI tskpri)
{
  return call_syscall (SYS_CHG_PRI, tskid, tskpri);
}

/* dis_dsp  --- �ǥ����ѥå��ػ�
*/
ER
dis_dsp (void)
{
  return call_syscall (SYS_DIS_DSP);
}

/* ena_dsp  --- �ǥ����ѥå�����
*/
ER
ena_dsp (void)
{
  return call_syscall (SYS_ENA_DSP);
}

/* frsm_tsk --- �����Ԥ����֤Υ����������Ԥ����֤���(¿�Ť��Ԥ�������)
*/
ER
frsm_tsk (ID taskid)
{
  return call_syscall (SYS_FRSM_TSK, taskid);
}

/* rel_wai --- �Ԥ����֤β��
 */
ER
rel_wai (ID taskid)
{
  return call_syscall (SYS_REL_WAI, taskid);
}

/* get_tid  --- ���������Υ����� ID ����
*/
ER
get_tid (ID *rid)
{
  return call_syscall (SYS_GET_TID, rid);
}

/* ref_tsk  --- ���������֤λ���
*/
ER
ref_tsk (T_RTSK *stat, ID taskid)
{
  return call_syscall (SYS_REF_TSK, stat, taskid);
}

/* rot_rdq  --- Ʊ��ץ饤����ƥ��ǤΥ������ν�����ѹ�����
*/
ER
rot_rdq (PRI tskpri)
{
  return call_syscall (SYS_ROT_RDQ, tskpri);
}

/* rsm_tsk  --- �����Ԥ����֤Υ����������Ԥ����֤���
*/
ER
rsm_tsk (ID taskid)
{
  return call_syscall (SYS_RSM_TSK, taskid);
}

/* slp_tsk  --- �����������Ԥ����֤ˤ���
*/
ER
slp_tsk (void)
{
  return call_syscall (SYS_SLP_TSK);
}

/* sta_tsk  --- �������ε�ư
*/
ER
sta_tsk (ID taskid, INT stacd)
{
  return call_syscall (SYS_STA_TSK, taskid, stacd);
}

/* sus_tsk  --- ���ꤷ�������������Ԥ����֤˰ܹ�
*/
ER
sus_tsk (ID taskid)
{
  return call_syscall (SYS_SUS_TSK, taskid);
}

/* ter_tsk  --- ¾������������λ
*/
ER
ter_tsk (ID tskid)
{
  return call_syscall (SYS_TER_TSK, tskid);
}

/* wup_tsk  --- ���ꤵ�줿�������򵯾����롣
*/
ER
wup_tsk (ID taskid)
{
  return call_syscall (SYS_WUP_TSK, taskid);
}

/* del_tsk --- ¾���������
 */
ER
del_tsk (ID tskid)
{
  return call_syscall (SYS_DEL_TSK, tskid);
}
