/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#)$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/i386/virtual_memory.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */
static char rcsid[] = "@(#)$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/i386/virtual_memory.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

/*
 * $Log: virtual_memory.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.26  2000-01-26 08:31:49  naniwa
 * to prevent memory leak
 *
 * Revision 1.25  1999/11/10 10:30:10  naniwa
 * to support execve, etc
 *
 * Revision 1.24  1999/07/26 08:54:29  naniwa
 * fixed dup_vmap_table (), etc
 *
 * Revision 1.23  1999/07/24 04:34:04  naniwa
 * fixed release_vmap()
 *
 * Revision 1.22  1999/07/23 14:35:40  naniwa
 * implemented vunmap, vunm_reg, etc
 *
 * Revision 1.21  1999/07/09 08:20:33  naniwa
 * bug fix of vget_reg/vput_reg
 *
 * Revision 1.20  1999/04/13 04:15:12  monaka
 * MAJOR FIXcvs commit -m 'MAJOR FIX!!! There are so many changes, modifys, fixes. Sorry but I can't remember all of those. For example, all the manager and driver programmer have got power to access all ITRON systemcall. (My works is just making access route to ITRON. I don't know what happens in the nuclus.'! There are so many changes, modifys, fixes. Sorry but I can't remember all of those. For example, all the manager and driver programmer have got power to access all ITRON systemcall. (My works is just making access route to ITRON. I don't know what happens in the nuclus.
 *
 * Revision 1.19  1999/04/12 15:29:18  monaka
 * pointers to void are renamed to VP.
 *
 * Revision 1.18  1999/04/12 14:47:40  monaka
 * vnum_reg() is added but it returns E_NOSPT.
 *
 * Revision 1.17  1999/04/12 13:29:37  monaka
 * printf() is renamed to printk().
 *
 * Revision 1.16  1999/03/16 13:07:28  monaka
 * Modifies for source cleaning. Most of these are for avoid gcc's -Wall message.
 *
 * Revision 1.15  1999/03/15 00:25:42  monaka
 * a cast for pageent has added.
 *
 * Revision 1.14  1999/03/02 16:00:23  night
 * ��Ȫ@KMC (yusuke@kmc.kyoto-u.ac.jp) �Υ��ɥХ����ˤ�뽤����
 * (vget_reg() ��Ʊ�ͤν���)
 * --------------------
 * ���� if �ˤ�äƷ��ꤵ��Ƥ��� delta_end �Ϥ��Υڡ�����Ǥ�
 * ž���κǸ�Υ��ɥ쥹�Τ褦�Ǥ�����ž���κǸ�Υڡ����λ��ˤ�
 * (p + PAGE_SIZE == align_end) ����Ω����Ϥ��Ǥ������ΤȤ���
 * ��Ȥξ�����delta_end��PAGE_SIZE�ˤʤꡢɬ�װʾ�˥��ԡ������
 * ���ޤ������å����˲�����뤳�Ȥ�ͭ��ޤ�����
 * --------------------
 *
 * Revision 1.13  1999/03/02 15:27:30  night
 * ��Ȫ@KMC (yusuke@kmc.kyoto-u.ac.jp) �Υ��ɥХ����ˤ�뽤����
 * --------------------
 * ���� if �ˤ�äƷ��ꤵ��Ƥ��� delta_end �Ϥ��Υڡ�����Ǥ�
 * ž���κǸ�Υ��ɥ쥹�Τ褦�Ǥ�����ž���κǸ�Υڡ����λ��ˤ�
 * (p + PAGE_SIZE == align_end) ����Ω����Ϥ��Ǥ������ΤȤ���
 * ��Ȥξ�����delta_end��PAGE_SIZE�ˤʤꡢɬ�װʾ�˥��ԡ������
 * ���ޤ������å����˲�����뤳�Ȥ�ͭ��ޤ�����
 * --------------------
 *
 * Revision 1.12  1998/02/25 12:43:51  night
 * vmap() �˥�������������ꤹ��������ɲá�
 * ����ӡ������˽��äơ����������������ꤹ��������ɲá�
 *
 * Revision 1.11  1998/02/24 14:12:20  night
 * vget_reg()/vput_reg() �ǥ��ԡ������ΰ����Ƭ���ɥ쥹��
 * �ڡ��������˰��פ��Ƥ���Ȥ��ˡ����ԡ�����ʤ��Ȥ���
 * �Х�����������
 *
 * vmap_reg() �� vtor() ���оݤȤʤ륿�����ξ��֤� DORMANT
 * ���֤λ��˥��顼�Ȥߤʤ��Ƥ����Τ����������֤ȼ��������
 * �褦�ˤ�����
 *
 * Revision 1.10  1998/02/23 14:45:51  night
 * ����������������Ȥ��ˡ����٤Ƥβ��۶��֤�ƥ����� (������������������
 * ����) ����Ѿ�����Τ��ᡢ�����ƥ���� (0x80000000 �ʹ�) �Τߤ�Ѿ�
 * ����褦���ѹ�������
 *
 * Revision 1.9  1998/02/16 14:19:34  night
 * vcre_reg() �ΰ��������å����ѹ���
 * �����ϡ��������ȥ��ɥ쥹�� 0 �ʲ��λ��ˤϡ��ѥ�᡼�����顼��
 * ���Ƥ�����������ȡ��������ȥ��ɥ쥹�� 0 ����ꤷ�����˥��顼
 * �ˤʤäƤ��ޤ����������ȥ��ɥ쥹�Υ����å��򳰤�����
 *
 * Revision 1.8  1997/09/09 13:51:35  night
 * �ǥХå��Ѥ� printf ������
 *
 * Revision 1.7  1997/08/31 14:12:40  night
 * lowlib �ط��ν������ɲá�
 *
 * Revision 1.6  1997/07/06 11:56:06  night
 * �������ǡ�Unsigned Long �Ǥ��뤳�Ȥ򼨤� UL ���ɲä�����
 *
 * Revision 1.5  1997/05/14 14:09:36  night
 * vget_reg() �� vput_reg() �������ư���褦����������
 *
 * Revision 1.4  1997/05/12 14:36:21  night
 * �ǥХå��Ѥ�ʸ���ɲá�
 *
 * Revision 1.3  1997/03/25 13:31:05  night
 * �ؿ��Υץ��ȥ�����������ɲä���Ӱ�����������ν���
 *
 * Revision 1.2  1996/11/07  12:43:15  night
 * vtor() ���ɲä���� vget_reg() �� vput_reg() ����Ȥ����������
 *
 * Revision 1.1  1996/07/22  13:39:21  night
 * IBM PC �� ITRON �κǽ����Ͽ
 *
 * Revision 1.12  1996/01/06 16:08:03  night
 * �ƤӽФ��ؿ�̾���ְ�äƤ����Τ�������ľ������
 *
 * Revision 1.11  1995/12/13 15:02:23  night
 * vmap_reg() �ؿ�����Ȥ����������
 *
 * Revision 1.10  1995/09/21  15:51:29  night
 * �������ե��������Ƭ�� Copyright notice ������ɲá�
 *
 * Revision 1.9  1995/09/17  17:00:12  night
 * ;ʬ�� printf () �� #ifdef notdef ... #endif �ǰϤ����
 *
 * Revision 1.8  1995/09/14  04:33:09  night
 * ���ɥ쥹�ޥ����Ѥ�������ͤ��ѹ���
 *
 * Revision 1.7  1995/05/31  22:58:00  night
 * �����Ĥ��� #ifdef DEBUG ... #endif ���ɲá�
 * (;ʬ�ʥǥХå��� printf ʸ���Υ����)
 *
 * Revision 1.6  1995/03/18  14:50:11  night
 * vcre_reg() �ؿ��򥳥�ѥ���Ǥ���褦�˽�����
 *
 * Revision 1.5  1995/02/26  14:07:40  night
 * RCS �ޥ��� $ Header $ �� $ Log $ ���ɲá�
 *
 *
 */

/* virtual_memory.c
 *	
 *
 */

#include "i386.h"
#include "itron.h"
#include "misc.h"
#include "func.h"
#include "task.h"
#include "errno.h"
#include "region.h"

static I386_PAGE_ENTRY *alloc_pagetable (void);


/* dup_vmap_table --- ���ꤵ�줿���ۥ���Υޥåԥ󥰥ơ��֥��
 *		      ���ԡ����롣
 *		      �ޥåץơ��֥뼫�ΤϿ������������롣
 *
 */
ADDR_MAP 
dup_vmap_table (ADDR_MAP dest)
{
  ADDR_MAP	newp;
  int		i;
  I386_PAGE_ENTRY	*p;

  dest = (ADDR_MAP)(((UW)dest) | 0x80000000);
  newp = (ADDR_MAP)(palloc (1));	/* �ڡ����ǥ��쥯�ȥ�Υ��������� */
  bzero ((VP )newp, PAGE_SIZE);


/* 1998/Feb/23 */
  for (i = ADDR_MAP_SIZE / 2; i < ADDR_MAP_SIZE; i++)
    {
      newp[i] = dest[i];		/* �ڡ����ǥ��쥯�ȥ�򣱥���ȥꤺ�ĥ��ԡ� */
      if (newp[i].present)		/* ����ȥ꤬�ޥåԥ󥰤���Ƥ���ʤ�С�   */
	{				/* ���ԡ����롣                             */
	  p = (I386_PAGE_ENTRY *)(palloc (1));

#ifdef DEBUG
	  printk ("dup_vmap_table: (VP)RTOV(dest[i].frame_addr << PAGE_SHIFT) = 0x%x,"
		    "(VP)p = 0x%x, PAGE_SIZE = %d\n",
		    (VP)RTOV(dest[i].frame_addr << PAGE_SHIFT),
		    p,
		    PAGE_SIZE);
#endif
	  {
	    int		j;
	    char	*q, *r;

	    q = (VP)RTOV(dest[i].frame_addr << PAGE_SHIFT);
	    r = (char *)p;
	    for (j = 0; j < 4096; j++)
	      {
#ifdef notdef
		printk ("copy source = 0x%x, dest = 0x%x\n", q, r);
#endif
		*r = *q;
		r++;
		q++;
	      }
	  }

#ifdef notdef
	  bcopy ((unsigned int)((VP)RTOV(dest[i].frame_addr << PAGE_SHIFT)),
		 (unsigned int)(VP)p,
		 PAGE_SIZE);
#endif

#ifdef DEBUG
	  printk ("dup_vmap_table: [%d]copy 0x%x -> 0x%x\n", 
		  i,
		  (VP)RTOV(dest[i].frame_addr << PAGE_SHIFT), 
		  (VP)p);
#endif
	  newp[i].frame_addr = ((UW)p & 0x7fffffff) >> PAGE_SHIFT;
	}
    }
  return (newp);
}

/***********************************************************************
 * release_vmap --- ���ꤷ�����ɥ쥹�ޥåץơ��֥�򤹤٤Ʋ������롣
 *
 */
extern ER
release_vmap (ADDR_MAP dest)
{
  I386_PAGE_ENTRY	*p;
  W			i, j;
  UW			ppage;

  dest = (ADDR_MAP)((UW)dest | 0x80000000);
  for (i = 0; i < ADDR_MAP_SIZE; i++)
    {
      if (dest[i].present) {
	p = (I386_PAGE_ENTRY *)(dest[i].frame_addr * PAGE_SIZE);
  if ((UW)p <= 0x7fffffff)
    {
      p = (I386_PAGE_ENTRY *)((((UW)p)) | 0x80000000);
    }
	if (i < ADDR_MAP_SIZE/2) {
	  for (j = 0; j < PAGE_SIZE / sizeof (I386_PAGE_ENTRY); j++)
	    {
	      if (p[j].present) {
		ppage = (p[j].frame_addr * PAGE_SIZE) & 0x7fffffff;
		pfree((VP) ppage, 1);
	      }
	    }
	}
  p = (I386_PAGE_ENTRY *)(((UW)p) & 0x7fffffff);
	pfree ((VP) p, 1);
      }
    }
  dest = (ADDR_MAP)((UW)dest & 0x7fffffff);
  pfree ((VP) dest, 1);

  return E_OK;
}


/*************************************************************************
 * vmap --- ���ۥ���Υޥåԥ�
 *
 * ������	task	�ޥåԥ󥰤��оݤȤʤ륿����
 *		vpage	���ۥ��ꥢ�ɥ쥹
 *		ppage	ʪ�����ꥢ�ɥ쥹
 *		accmode	���������� (0 = kernel, 1 = user)
 *
 * ���͡�	TRUE	����
 *		FALSE	����
 *
 * ������	�����ǻ��ꤵ�줿���ۥ����ʪ������˳�����Ƥ�
 *
 */
BOOL
vmap (T_TCB *task, UW vpage, UW ppage, W accmode)
{
  I386_DIRECTORY_ENTRY	*dirent;
  I386_PAGE_ENTRY	*pageent;
  UW			dirindex;
  UW			pageindex;

#ifdef DEBUG
  printk ("[%d] vmap: 0x%x -> 0x%x\n", task->tskid, ppage, vpage);
#endif /* DEBUG */
/*  task->context.cr3 &= 0x7fffffff; */
  dirent = (I386_DIRECTORY_ENTRY *)(task->context.cr3);
  dirent = (I386_DIRECTORY_ENTRY *)((((UW)dirent)) | 0x80000000);
  dirindex = vpage & 0xffc00000;
  dirindex = dirindex >> DIR_SHIFT;
  pageindex = (vpage & 0x003ff000) >> PAGE_SHIFT;

/*
  dirindex = vpage / (PAGE_SIZE * PAGE_SIZE);
*/
/*
  pageindex = (vpage % (PAGE_SIZE * PAGE_SIZE * PAGE_SIZE));
*/

#ifdef DEBUG
  printk ("dirindex = %d, pageindex = %d\n", dirindex, pageindex);
#endif /* DEBUG */
  if (dirent[dirindex].present != 1)
    {
      /* �ڡ����ǥ��쥯�ȥ�Υ���ȥ�϶����ä���
       * �������ڡ����ǥ��쥯�ȥ�Υ���ȥ�����롣
       */
      pageent = (I386_PAGE_ENTRY *)alloc_pagetable ();
      if (pageent == NULL)
	{
	  return (FALSE);
	}
#ifdef DEBUG
      printk ("dir alloc(newp). frame = 0x%x ", ((UW)pageent & 0x0fffffff) >> PAGE_SHIFT);
#endif /* DEBUG */
      /*      dirent[dirindex].frame_addr = ((UW)pageent & 0x0fffffff) >> PAGE_SHIFT;*/
      dirent[dirindex].frame_addr = ((UW)pageent & 0x7fffffff) >> PAGE_SHIFT;
      dirent[dirindex].present = 1;
      dirent[dirindex].read_write = 1;
      dirent[dirindex].u_and_s = 1;
      dirent[dirindex].zero2 = 0;
      dirent[dirindex].access = 0;
      dirent[dirindex].dirty = 0;
      dirent[dirindex].user = accmode;
      dirent[dirindex].zero1 = 0;
    }
  else
    {
      pageent = (I386_PAGE_ENTRY *)(dirent[dirindex].frame_addr * PAGE_SIZE);
#ifdef DEBUG
      printk ("dir alloc(old). frame = 0x%x ", dirent[dirindex].frame_addr);
#endif /* DEBUG */
    }

  if ((UW)pageent <= 0x7fffffff)
    {
      pageent = (I386_PAGE_ENTRY *)((((UW)pageent)) | 0x80000000);
    }

  if (pageent[pageindex].present == 1) {
    /* ���˥ڡ����� map ����Ƥ��� */
    printk("vmap: vpage %x has already mapped\n", vpage);
    /*    return(FALSE);*/
  }
  pageent[pageindex].frame_addr = (ppage / PAGE_SIZE) & 0x7fffffff;
  pageent[pageindex].present = 1;
  pageent[pageindex].read_write = 1;
  pageent[pageindex].u_and_s = accmode;
  pageent[pageindex].zero2 = 0;
  pageent[pageindex].access = 0;
  pageent[pageindex].dirty = 0;
  pageent[pageindex].zero1 = 0;
  pageent[pageindex].user = 0;
/*
  pageent[pageindex + 1].present = 1;
  pageent[pageindex + 1].read_write = 1;
  pageent[pageindex + 1].u_and_s = 1;

  pageent[pageindex + 31].present = 1;
  pageent[pageindex + 31].read_write = 1;
  pageent[pageindex + 31].u_and_s = 1;

  pageent[pageindex + 32].present = 1;
  pageent[pageindex + 32].read_write = 1;
  pageent[pageindex + 32].u_and_s = 1;

  pageent[pageindex + 33].present = 1;
  pageent[pageindex + 33].read_write = 1;
  pageent[pageindex + 33].u_and_s = 1;
*/
#ifdef DEBUG
  printk ("pageindex = %d, frame = 0x%x\n", pageindex, pageent[pageindex].frame_addr);
#endif /* DEBUG */
  return (TRUE);
}

/* ���ۥ���Υ���ޥå�
 *
 * ����:	virtual	���ۥ��ꥢ�ɥ쥹
 *
 */
extern ER
vunmap (T_TCB *task, UW vpage)
{
  I386_DIRECTORY_ENTRY	*dirent;
  I386_PAGE_ENTRY	*pageent;
  UW			dirindex;
  UW			pageindex;
  UW			ppage;
  ER			errno;

  dirent = (I386_DIRECTORY_ENTRY *)(task->context.cr3);
  dirent = (I386_DIRECTORY_ENTRY *)((((UW)dirent)) | 0x80000000);
  dirindex = vpage & 0xffc00000;
  dirindex = dirindex >> DIR_SHIFT;
  pageindex = (vpage & 0x003ff000) >> PAGE_SHIFT;

/*
  dirindex = vpage / (PAGE_SIZE * PAGE_SIZE);
*/
/*
  pageindex = (vpage % (PAGE_SIZE * PAGE_SIZE * PAGE_SIZE));
*/

#ifdef DEBUG
  printk ("dirindex = %d, pageindex = %d\n", dirindex, pageindex);
#endif /* DEBUG */
  if (dirent[dirindex].present != 1)
    {
      /* �ڡ����ǥ��쥯�ȥ�Υ���ȥ�϶����ä���
       */
      return (FALSE);
    }
  else
    {
      pageent = (I386_PAGE_ENTRY *)(dirent[dirindex].frame_addr * PAGE_SIZE);
    }

  if ((UW)pageent <= 0x7fffffff)
    {
      pageent = (I386_PAGE_ENTRY *)((((UW)pageent)) | 0x80000000);
    }

  ppage = (pageent[pageindex].frame_addr * PAGE_SIZE) & 0x7fffffff;
  errno = pfree((VP) ppage, 1);
  if (errno) return(FALSE);
  pageent[pageindex].present = 0;
  return (TRUE);
}



/*************************************************************************
 * alloc_dirent --- �ڡ����ǥ��쥯�ȥ�ơ��֥�κ���
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
static I386_PAGE_ENTRY *
alloc_pagetable (void)
{
  I386_PAGE_ENTRY	*newp;
  W			i;

  newp = (I386_PAGE_ENTRY *)palloc (1);
  if (newp == NULL)
    {
      return (NULL);
    }
  bzero (newp, PAGE_SIZE);
  for (i = 0; i < PAGE_SIZE / sizeof (I386_PAGE_ENTRY); i++)
    {
      newp[i].present = 0;
      newp[i].read_write = 1;
      newp[i].u_and_s = 1;
      newp[i].zero2 = 0;
      newp[i].access = 0;
      newp[i].dirty = 0;
      newp[i].user = 0;
      newp[i].zero1 = 0;
      newp[i].frame_addr = 0;
    }
  return (newp);
}


/* vtor - ���ۥ��ꥢ�ɥ쥹��ʪ�����ɥ쥹���Ѵ�����
 *
 */
UW
vtor (ID tskid, UW addr)
{
  T_TCB	*taskp;
  I386_DIRECTORY_ENTRY	*dirent;
  I386_PAGE_ENTRY	*pageent;
  UW			dirindex;
  UW			pageindex;

  taskp = (T_TCB *)get_tskp (tskid);
#ifdef notdef
  if ((taskp->tskstat == TTS_NON) || (taskp->tskstat == TTS_DMT))
#else
  if (taskp->tskstat == TTS_NON)
#endif
    {
      return (NULL);
    }

  dirent = (I386_DIRECTORY_ENTRY *)taskp->context.cr3;
  dirent = (I386_DIRECTORY_ENTRY *)RTOV ((UW)dirent);
  dirindex = (addr & 0xffc00000UL) >> DIR_SHIFT;
  pageindex = (addr & 0x003ff000UL) >> PAGE_SHIFT;
  if (dirent[dirindex].present != 1)
    {
      return (NULL);
    }

  pageent = (I386_PAGE_ENTRY *)(dirent[dirindex].frame_addr * PAGE_SIZE);
  pageent = (I386_PAGE_ENTRY *)RTOV ((UW)pageent);
  if (pageent[pageindex].present != 1)
    {
      return (NULL);
    }

  return (pageent[pageindex].frame_addr << PAGE_SHIFT);
}



/*
 * �꡼�����κ���
 *
 * �ƥ������ϡ��꡼������������äƤ��롣
 * ������ǻȤäƤ��ʤ�����ȥ�����ӡ������ǻ��ꤷ�����������롣
 *
 * ���δؿ�����Ǥϡ�ʪ�������ޥåԥ󥰤���褦�ʽ����Ϥ��ʤ���
 * ñ�˿������꡼������ҤȤĳ�����Ƥ�����Ǥ��롣
 * �⤷���꡼���������������Ȥ���ʪ������������Ƥ����Ȥ��ˤϡ�
 * vcre_reg ��¹Ԥ������Ȥ� vmap_reg ��¹Ԥ���ɬ�פ����롣
 *
 */
ER
vcre_reg (ID	id, 	/* task ID */
	  VP	start,	/* �꡼�����γ��ϥ��ɥ쥹 */
	  W	min,	/* �꡼�����κǾ�(���)������ */
	  W	max,	/* �꡼�����κ��祵���� */
	  UW	perm,	/* �꡼�����Υѡ��ߥå���� */
	  FP	handle)	/* �꡼�������ǥڡ����ե�����Ȥ�ȯ�������� */
			/* ���ν����λ��� */
{
  T_TCB		*taskp;
  T_REGION	*regp;
  W		counter;

  /*
   * �����Υ����å���Ԥ���
   * �⤷�������ͤ������������ˤϡ�E_PAR �Υ��顼�ֹ���֤���
   */
/*
  if (start <= 0)	return (E_PAR);
*/
  if (min <= 0)		return (E_PAR);
  if (max <= 0)		return (E_PAR);
  if (min > max)	return (E_PAR);

  /*
   * ������ ID ���鳺�����륿�����Υ���ƥ����Ȥؤ�
   * �ݥ��󥿤���Ф���
   */
  taskp = get_tskp (id);
  if (taskp == NULL)
    {
      /*
       * �����ǻ��ꤷ�� ID ���ĥ�������¸�ߤ��Ƥ��ʤ���
       * E_OBJ ���֤���
       */
      return (E_OBJ);
    }

  /*
   * ���Ф����������Υ���ƥ����Ⱦ��󤫤�꡼��������Υ���ȥ��
   * ���Ф��� 
   */
  for (counter = 0; counter < MAX_REGION; counter++)
    {
      if (taskp->regions[counter].permission == 0)
	{
	  break;
	}
    }
  if (counter == MAX_REGION)
    {
      /*
       * �����Ƥ���꡼����󤬤ʤ��ä���
       * E_NOMEM �Υ��顼���֤���
       */
      return (E_NOMEM);
    }
  regp = &(taskp->regions[counter]);	/* regp �˶����Ƥ���꡼����� */
					/* ����ȥ��ݥ��󥿤�����롣*/

  /*
   * �꡼������������ꡣ
   * �꡼����󥨥�ȥ�ؤϡ��������ͤ򤽤Τޤ����줺�˰ʲ��Τ褦�ʽ�
   * ����Ԥ���
   *	start		�ڡ������������ڤ�ΤƤ�
   *	min_size	�ڡ������������ڤ�夲��
   *	max_size	�ڡ������������ڤ�夲��
   *	permission	���Τޤ�
   *	handle		���Τޤ�
   */
  regp->start_addr = (VP)CUTDOWN (start, PAGE_SIZE);
  regp->min_size = ROUNDUP (min, PAGE_SIZE);
  regp->max_size = ROUNDUP (max, PAGE_SIZE);
  regp->permission = perm;
  regp->handle = handle;
  
  /*
   * ����������˽�λ������
   */
  return (E_OK);
}

/*
 * �꡼�������˴�
 *
 * ���� start �ǻ��ꤷ�����ɥ쥹�ΰ���������꡼�����������롣 
 * ��������꡼�����˴ޤޤ���ΰ���Υǡ������˴����롣
 *
 * start ���ͤǻ��ꤷ�����ɥ쥹�ϡ��꡼��������Ƭ���ɥ쥹�Ǥ���ɬ��
 * �Ϥʤ����꡼�������Υ��ɥ쥹�ʤ�С��ɤΥ꡼��������ꤷ������
 * �����ƥॳ�������Ƚ�Ǥ��롣
 *
 */
ER
vdel_reg (ID id, VP start) 
     /* id     �������꡼�������ĥ�����
      * start  �������꡼��������Ƭ���ɥ쥹
      */
{
  T_TCB		*taskp;
  W		counter;

  taskp = get_tskp (id);
  if (taskp == NULL)
    {
      /*
       * �����ǻ��ꤷ�� ID ���ĥ�������¸�ߤ��Ƥ��ʤ���
       * E_OBJ ���֤���
       */
      return (E_OBJ);
    }
  
  for (counter = 0; counter < MAX_REGION; counter++)
    {
      if (taskp->regions[counter].start_addr == start)
	{
	  taskp->regions[counter].permission = 0;
	  break;
	}
    }
  if (counter == MAX_REGION) 
    return (E_PAR);
  else 
    return (E_OK);
}

/*
 * �꡼�������β��ۥڡ�����ʪ����������դ��롣
 *
 * �����ǻ��ꤷ�����ɥ쥹�ΰ��ʪ����������դ��롣
 *
 * ʣ���Υڡ������������륵���������ꤵ�줿��硢���ƤΥڡ������ޥå�
 * ��ǽ�ΤȤ��Τ�ʪ����������դ��롣����¾�ξ��ϳ���դ��ʤ���
 *
 * �ޥåפ���ʪ������Υ��ɥ쥹�ϻ���Ǥ��ʤ����濴�ˤ����ۥ����
 * ����դ���ʪ�������Ŭ���˳�꿶�롣
 *
 *
 * �֤���
 *
 * �ʲ��Υ��顼�ֹ椬�֤롣
 *	E_OK     �꡼�����Υޥåפ�����  
 *	E_NOMEM  (ʪ��)���꤬��­���Ƥ���
 *	E_NOSPT  �ܥ����ƥॳ����ϡ�̤���ݡ��ȵ�ǽ�Ǥ��롣
 *	E_PAR	 ��������������
 *
 */
ER
vmap_reg (ID id, VP start, UW size, W accmode)
     /* 
      * id        ������ ID
      * start     �ޥåפ��벾�ۥ����ΰ����Ƭ���ɥ쥹
      * size      �ޥåפ��벾�ۥ����ΰ���礭��(�Х���ñ��)
      * accmode	  �ޥåפ��벾�ۥ����ΰ�Υ��������������
      *		  (ACC_KERNEL = 0, ACC_USER = 1)
      */
{
  T_TCB	*taskp;
  UW	counter, i;
  VP	pmem;
  ER	res;

  taskp = (T_TCB *)get_tskp (id);
#ifdef notdef
  if ((taskp->tskstat == TTS_NON) || (taskp->tskstat == TTS_DMT))
#else
  if (taskp->tskstat == TTS_NON)
#endif
    {
#ifdef DEBUG
      printk ("vmap_reg : taskp->tskstat = %d\n", taskp->tskstat);	 /* */
#endif
      return (E_PAR);
    }

  size = ROUNDUP (size, PAGE_SIZE)/PAGE_SIZE;
  start = (VP)((((UW)start) / PAGE_SIZE) * PAGE_SIZE);
  if (pmemfree() < size) return(E_NOMEM);
  res = E_OK;
  for (counter = 0; counter < size; counter++)
    {
      pmem = palloc (1);
      if (pmem == NULL) {
	res = E_NOMEM;
	break;
      }
      if (vmap (taskp, (((UW)start) + (counter * PAGE_SIZE)),
		(UW)pmem & 0x7fffffff, accmode) == FALSE)
	{
	  pfree((VP) ((UW) pmem & 0x7fffffff), 1);
	  res = E_SYS;
	  break;
	}
    }
  if (res != E_OK) {
    for (i = 0; i < counter; ++i) {
      vunmap(taskp, ((UW)start) + (i * PAGE_SIZE));
    }
  }
  return (res);
}

/*
 *
 */
ER
vunm_reg (ID id, VP start, UW size)
{
  T_TCB	*taskp;
  UW	counter;

  taskp = (T_TCB *)get_tskp (id);
  if (taskp->tskstat == TTS_NON)
    {
      return (E_PAR);
    }
  size = ROUNDUP (size, PAGE_SIZE);
  start = (VP)((((UW)start) / PAGE_SIZE) * PAGE_SIZE);
  for (counter = 0; counter < (size / PAGE_SIZE); counter++)
    {
      if (vunmap (taskp, (((UW)start) + (counter * PAGE_SIZE))) == FALSE)
	{
	  return (E_SYS);
	}
    }
  return (E_OK);
}

/*
 * ���ꤷ���������Τ�ĥ꡼������ʣ�����롣
 *
 * ʣ�������꡼�����ϡ������̤Τ�ΤȤ��ư����롣
 * src, dst �Τɤ��餫�Υ��������꡼�������ΰ���ѹ����Ƥ⡢�⤦����
 * �Υ������ϱƶ�������ʤ���
 *
 */
ER
vdup_reg (ID src, ID dst, VP start)
     /* src    ʣ������꡼�������ĥ�����
      * dst    �꡼������ʣ����Υ�����
      * start  ʣ������꡼��������Ƭ���ɥ쥹
      */
{
  return (E_NOSPT);
}

/*
 * �꡼�����˴ޤޤ�뤹�٤Ƥβ��ۥ���ڡ����Υץ��ƥ��Ⱦ�������ꤹ�롣
 *
 * �ץ��ƥ��Ⱦ���Ȥ��Ƥϰʲ����ͤ�����Ǥ��롣
 *
 *	VPROT_READ    �ɤ߹��߲�ǽ
 *	VPROT_WRITE   �񤭹��߲�ǽ
 *	VPROT_EXEC    �¹Բ�ǽ
 *
 *
 * �֤���
 *
 * �ʲ��Υ��顼�ֹ椬�֤롣
 *	E_OK     �꡼�����Υޥåפ�����  
 *	E_NOMEM  (ʪ��)���꤬��­���Ƥ���
 *	E_NOSPT  �ܥ����ƥॳ����ϡ�̤���ݡ��ȵ�ǽ�Ǥ��롣
 *	E_OK     �꡼�����Υץ��ƥ��Ⱦ�������������  
 *	E_NOSPT  �ܥ����ƥॳ����ϡ�̤���ݡ��ȵ�ǽ�Ǥ��롣
 */
ER
vprt_reg (ID id, VP start, UW prot)
     /* id     �꡼�������ĥ�����
      * start  �꡼��������Ƭ���ۥ��ɥ쥹
      * prot   �ץ��ƥ��Ⱦ���
      */
{
  return (E_NOSPT);
}

/*
 * �������֤ǤΥ꡼�����ζ�ͭ
 *
 * ���� src �ǻ��ꤷ���������Τ�ĥ꡼��������� dst �ǻ��ꤷ������
 * ���˳�����Ƥ롣��ꤢ�Ƥ��꡼�����϶�ͭ����롣
 *
 * ��ͭ���줿�꡼�����˴ޤޤ�벾�ۥ��ɥ쥹�ΰ�ˤϡ��������֤�Ʊ��
 * ʪ�����ɥ쥹�������Ƥ롣���Τ��ᡢ�����Υ����������ۥ��ɥ쥹�ΰ�
 * ��ξ�����ѹ�������硢¾���Υ������ˤ�ȿ�Ǥ���롣
 *
 *
 * �֤���
 *
 * �ʲ��Υ��顼�ֹ椬�֤롣
 *
 *	E_OK     �꡼�����ζ�ͭ������  
 *	E_NOSPT  �ܥ����ƥॳ����ϡ�̤���ݡ��ȵ�ǽ�Ǥ��롣
 *
 */
ER
vshr_reg (ID src, ID dst, VP start)
     /*
      * src    ��ͭ���Υ꡼�������ĥ�����
      * dst    �����˥꡼������ͭ���륿����
      * start  �꡼��������Ƭ���ɥ쥹
      */
     
{
  return (E_NOSPT);
}

/*
 * �꡼����󤫤���ɤ߹���
 *
 * Ǥ�դΥ������β��ۥ����ΰ褫��ǡ������ɤ߹��ࡣ
 * �ڡ��������Ȥʤɤ˻��Ѥ��롣
 *
 *
 * �֤���
 *
 * �ʲ��Υ��顼�ֹ椬�֤롣
 *
 *	E_OK     ����  
 *	E_ID     �꡼�������ĥ�����
 *	E_NOSPT  �ܥ����ƥॳ����ϡ�̤���ݡ��ȵ�ǽ�Ǥ���
 *
 */
ER
vget_reg (ID id, VP start, UW size, VP buf) 
     /*
      * id     �꡼��������ĥ�����
      * start  �ɤ߹����ΰ����Ƭ���ɥ쥹
      * size   �꡼����󤫤��ɤ߹��ॵ����
      * buf    �꡼����󤫤��ɤ߹�����ǡ���������Хåե�
      */
{
  UW	offset;
  UW	align_start;
  UW	align_end;
  UW	paddr;
  UW	copysize;
  UW	bufoffset;
  UW	p;
  UW	delta_start, delta_end;


  if (id < 0)		return (E_PAR);
  if (size <= 0)	return (E_PAR);
  if (buf == NULL)	return (E_PAR);

  align_start = CUTDOWN (start, PAGE_SIZE);
  align_end  = ROUNDUP (start + size, PAGE_SIZE);

  bufoffset = 0;

  for (p = align_start; p < align_end; p += PAGE_SIZE)
    {
      paddr = (UW)vtor (id, p);		/* ʪ�����ꥢ�ɥ쥹�μ��� */
      if (paddr == NULL)
	{
	  return (E_PAR);
	}

      paddr = (UW)RTOV (paddr);	/* V = R �ΰ�Υ��ɥ쥹���Ѵ� */

      if (p == align_start)
	{
	  offset = (UW)paddr + ((UW)start - align_start);
	  delta_start = (UW) start - align_start;
	}
      else
	{
	  offset = (UW)paddr;
	  delta_start = 0;
	}
      
      if ((p + PAGE_SIZE) >= align_end)
	{
	  delta_end = align_end - p;
	}
      else
	{
	  delta_end = PAGE_SIZE;
	}

      copysize = delta_end - delta_start;
      if (copysize > size) copysize = size;
      bcopy ((VP)offset, &((B *)buf)[bufoffset], copysize);
      bufoffset += copysize;
      size -= copysize;
    }

  return (E_OK);
}

/*
 * �꡼�����ؤν񤭹���
 *
 * Ǥ�դΥ������β��ۥ����ΰ�˥ǡ�����񤭹��ࡣ
 * �ڡ�������ʤɤ˻��ѤǤ��롣
 *
 *
 * �֤���
 *
 * �ʲ��Υ��顼�ֹ椬�֤롣
 *
 *	E_OK     �꡼�����ؤν񤭹��ߤ�����  
 *	E_ID     ���� id ���б�������������¸�ߤ��ʤ�
 *	E_NOSPT  �ܥ����ƥॳ����ϡ�̤���ݡ��ȵ�ǽ�Ǥ���
 *
 */
ER
vput_reg (ID id, VP start, UW size, VP buf)
     /*
      * id     �꡼��������ĥ�����
      * start  �񤭹����ΰ����Ƭ���ɥ쥹
      * size   �꡼�����˽񤭹��ॵ����
      * buf    �꡼�����˽񤭹���ǡ���
      */
{
  UW	offset;
  UW	align_start;
  UW	align_end;
  VP	paddr;
  UW	copysize;
  UW	bufoffset;
  UW	p;
  UW	delta_start, delta_end;


  if (id < 0)		return (E_PAR);
  if (size <= 0)	return (E_PAR);
  if (buf == NULL)	return (E_PAR);

  align_start = CUTDOWN (start, PAGE_SIZE);
  align_end  = ROUNDUP (start + size, PAGE_SIZE);

  bufoffset = 0;
  for (p = align_start; p < align_end; p += PAGE_SIZE)
    {
      paddr = (VP)vtor (id, p);		/* ʪ�����ꥢ�ɥ쥹�μ��� */
      if (paddr == NULL)
	{
	  return (E_PAR);
	}

      paddr = (VP)RTOV ((UW)paddr);	/* V = R �ΰ�Υ��ɥ쥹���Ѵ� */
      if (p == align_start)
	{
	  offset = (UW)paddr + ((UW)start - align_start);
	  delta_start = (UW) start - align_start;
	}
      else
	{
	  offset = (UW)paddr;
	  delta_start = 0;
	}
      
      if ((p + PAGE_SIZE) >= align_end)
	{
	  delta_end = align_end - p;
	}
      else
	{
	  delta_end = PAGE_SIZE;
	}

      copysize = delta_end - delta_start;
      if (copysize > size) copysize = size;
      bcopy (&((B *)buf)[bufoffset], (VP)offset, copysize);
      bufoffset += copysize;
      size -= copysize;
    }

  return (E_OK);
}

/*
 * �꡼�����ξ����������롣
 *
 * �꡼��������Ȥ��Ƥϼ��Τ�Τ��ͤ����롣
 *
 *	�꡼��������Ƭ���ۥ��ɥ쥹
 *	�꡼�����Υ�����
 *	�ץ��ƥ��Ⱦ���
 * 
 * �֤���
 *
 * �ʲ��Υ��顼�ֹ椬�֤롣
 *
 *	E_OK     �꡼�����ξ���μ���������  
 *	E_ID     ���� id �ǻ��ꤷ����������¸�ߤ��ʤ�
 *	E_NOSPT  �ܥ����ƥॳ����ϡ�̤���ݡ��ȵ�ǽ�Ǥ���
 *
 */
ER
vsts_reg (ID id, VP start, VP stat)
     /*
      * id     �꡼�������ĥ�����
      * start  �꡼��������Ƭ���ɥ쥹
      * stat   �꡼������������(�꡼��������ξܺ٤�̤����Ǥ���) 
      */
{
  return (E_NOSPT);
}

