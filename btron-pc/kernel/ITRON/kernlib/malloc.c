/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* @(#) $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/kernlib/malloc.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */

static char rcs[] = "@(#) $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/kernlib/malloc.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


/* 
 * $Log: malloc.c,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.6  2000-02-13 09:07:21  naniwa
 * minor fix
 *
 * Revision 1.5  2000/02/06 11:06:22  naniwa
 * try to fix log comment
 *
 * Revision 1.4  2000/02/06 09:12:34  naniwa
 * minor fix
 *
 * Revision 1.3  2000/01/26 08:33:11  naniwa
 * minor fix
 *
 * Revision 1.2  1999/07/26 08:55:37  naniwa
 * fixed malloc()
 *
 * Revision 1.1  1999/04/18 17:48:33  monaka
 * Port-manager and libkernel.a is moved to ITRON. I guess it is reasonable. At least they should not be in BTRON/.
 *
 * Revision 1.5  1998/02/25 12:33:20  night
 * vmap_reg () �ΰ����ο����ҤȤ����������Ȥˤ���ѹ���
 *
 * Revision 1.4  1997/10/24 14:03:53  night
 * init_malloc �˰����ɲá�
 *
 * Revision 1.3  1997/09/02 14:40:54  night
 * printf ��ƤӽФ��ʤ��褦�ˤ�����
 * (�ץ������ˤ�äƤϡ�printf �򥵥ݡ��Ȥ��ʤ�����)
 *
 * Revision 1.2  1997/08/31 14:09:42  night
 * malloc �ط��ν����������ư���褦�ˤ�����
 *
 * Revision 1.1  1997/07/06 13:09:43  night
 * �ǽ����Ͽ
 *
 * Revision 1.5  1997/07/02 13:10:32  night
 * malloc �ν�����
 *
 * Revision 1.4  1997/06/29 15:44:32  night
 * �����Ȥ��ѹ�
 *
 * �����������ɤ� SJIS ���� EUC ���ѹ�������
 * ��RCS �� Log �ޥ�������Υ����Ȥ��ɲä���(�ޤ�����Ƥ��ʤ��ä��ե�����Τ�)��
 *
 * Revision 1.3  1997/06/29 15:33:56  night
 * �����Ȥν��� (rcs �� Log �ޥ������ɲ�)
 *
 *
 */

#include <h/types.h>
#include <h/itron.h>
#include <h/config.h>
#include <h/errno.h>
#include <h/misc.h>
#include "libkernel.h"


#define MEMORY_CLICK		(PAGE_SIZE * 10)
#define MALLOC_SIZE		(1024 * 1024 * 10)	/* 10MB */


extern B	_end;
UW		last_page;

struct alloc_entry_t
{
  struct alloc_entry_t		*next;
  struct alloc_entry_t		*before;
  UW				size;		/* alloc_data �Υ����� */
						/* ������ */
  
  B				alloc_data[0];
};


static struct alloc_entry_t		alloc_list;
static struct alloc_entry_t		*free_list;
static struct alloc_entry_t		*pivot;

static VP			get_system_memory (UW size);


/* malloc �����ν����
 *
 */
ER
init_malloc (UW free_memory_erea)
{
  ID			mytid;
  struct alloc_entry_t	*p;
  ER			err;


  if (free_memory_erea)
    {
      last_page = free_memory_erea;
    }
  else
    {
      last_page = (UW)&_end;
    }
  last_page = ROUNDUP (last_page, PAGE_SIZE);

  err = get_tid (&mytid);
  if (err != E_OK)
    {
      return (err);
    }

  err = vcre_reg (mytid, (VP) last_page, MALLOC_SIZE, MALLOC_SIZE,
		  VPROT_READ | VPROT_READ | VPROT_USER, NULL);
  if (err)
    {
      return (err);
    }

  alloc_list.size = 0;
  p = (struct alloc_entry_t *)get_system_memory (MEMORY_CLICK);
  if (p == NULL)
    {
      return (E_NOMEM);
    }

  p->size = MEMORY_CLICK - sizeof (struct alloc_entry_t);

#ifdef notdef
  dbg_printf ("MEMORY_CLICK(%d) - sizeof (struct alloc_entry_t) (%d) = %d\n",
	  MEMORY_CLICK, sizeof (struct alloc_entry_t), 
	  MEMORY_CLICK - sizeof (struct alloc_entry_t));
#endif

  p->next = &alloc_list;
  p->before = &alloc_list;
  alloc_list.next = p;
  alloc_list.before = p;
  free_list = &alloc_list;
  pivot = &alloc_list;

#ifdef notdef
  dbg_printf ("init_malloc: alloc_list = 0x%x, alloc_list.next = 0x%x, alloc_list.before = 0x%x\n", &alloc_list, alloc_list.next, alloc_list.before);
  dbg_printf ("init_malloc: alloc_list.next->next = 0x%x, alloc_list.next->before = 0x%x\n",
	  alloc_list.next->next, alloc_list.next->before);
  dbg_printf ("init_malloc: p = 0x%x, p->next = 0x%x, p->before = 0x%x\n", 
	  p, p->next, p->before);
#endif
  return (E_OK);
}


VP
malloc (UW size)
{
  struct alloc_entry_t	*p;
  struct alloc_entry_t	*q;
  struct alloc_entry_t	*newmem;
  int			alloc_size;
  extern void		free (VP addr);



  if (size <= 0)
    {
      return (NULL);
    }

retry:
#ifdef notdef
  for (p = pivot->next; p != pivot; p = p->next)
#else
  p = pivot;
  do 
#endif
    {
#ifdef notdef
      dbg_printf ("malloc: search freememory: 0x%x, size = %d\n", p, p->size);
#endif
      if (p->size == size)
	{
	  pivot = p->next;
	  p->before->next = p->next;
	  p->next->before = p->before;
#ifdef notdef
	  dbg_printf ("malloc: return 0x%x\n", p);
#endif
	  return (p->alloc_data);
	}
      else if (p->size > (size + sizeof (struct alloc_entry_t)))
	{
	  pivot = p->next;
	  p->size -= (size + sizeof (struct alloc_entry_t));
	  newmem = (struct alloc_entry_t *)
	    ((UW)p + (p->size + sizeof (struct alloc_entry_t)));
	  newmem->size = size;
	  newmem->before = newmem->next = 0;
#ifdef notdef
	  dbg_printf ("malloc: return 0x%x(p = 0x%x), p->size = %d, p->size + sizeof (struct alloc_entry_t) = %d\n",
		  &(p->alloc_data[p->size + sizeof (struct alloc_entry_t)]),
		  p, 
		  p->size,
		  p->size + sizeof (struct alloc_entry_t));
#endif
	  return (&(p->alloc_data[p->size + sizeof (struct alloc_entry_t)]));
	}
#ifdef notdef
    }
#else
    p = p->next;
    } while (p != pivot);
#endif

  alloc_size = (MEMORY_CLICK < ROUNDUP (size + sizeof (struct alloc_entry_t), PAGE_SIZE)) 
               ? ROUNDUP (size + sizeof (struct alloc_entry_t), PAGE_SIZE)
	       : MEMORY_CLICK;
  newmem = (struct alloc_entry_t *)get_system_memory (alloc_size);
  if (newmem)
    {
      newmem->next = newmem->before = NULL;
      newmem->size = alloc_size - sizeof (struct alloc_entry_t);
      free (newmem->alloc_data);
      goto retry;
    }
  return (NULL);
}


VP
calloc (UW size, UW nentry)
{
  VP	rp;

  rp = malloc (size * nentry);
  if (rp)
    {
      bzero (rp, size * nentry);
    }

  return (rp);
}


void
free (VP addr)
{
  struct alloc_entry_t	*start_point;
  struct alloc_entry_t	*p;
  struct alloc_entry_t  *newentry;

  newentry = (struct alloc_entry_t *)((B *)addr - sizeof (struct alloc_entry_t));
  if ((newentry > (struct alloc_entry_t *)last_page) || (newentry < &alloc_list))
    {
      /* free ���륨��ȥ�Υ��ɥ쥹���������� */
      return;
    }

  if (&alloc_list == alloc_list.next)
    {
      alloc_list.next = newentry;
      alloc_list.before = newentry;
      newentry->next = newentry->before = &alloc_list;
      return;
    }

  start_point = (struct alloc_entry_t *)(((UW)pivot > (UW)addr) ? &alloc_list : pivot);

  for (p = start_point; ; p = p->next)
    {

#ifdef notdef
      dbg_printf ("malloc: free: newentry = 0x%x, (p = 0x%x, p->size = %d, p->size + sizeof (struct alloc_entry_t) = %d, p->end = 0x%x, p->next = 0x%x)\n", 
	      newentry,
	      p, p->size, p->size + sizeof (struct alloc_entry_t), (UW)p + p->size + sizeof (struct alloc_entry_t), p->next);
#endif


      /* �ե꡼�ꥹ�Ȥ���Ƭ����é�ꡢ�ꥹ�Ȥ���������ݥ���Ȥ���� */
      /* �ե꡼�ꥹ�Ȥϥ��ɥ쥹��ˤʤäƤ��ꡢp �Υ��ɥ쥹���ɲä� */
      /* ���ΰ�κǸ�����礭���ʤä��顢���������������롣*/
      if (((((UW)p) + p->size + sizeof (struct alloc_entry_t)) <= (UW)newentry) &&
	  ((p->next < p) || ((((UW)newentry) + newentry->size + sizeof (struct alloc_entry_t)) <= (UW)p->next)) )
	{

	  if ((((UW)p) + p->size + sizeof (struct alloc_entry_t)) == (UW)newentry)
	    {
#ifdef notdef
	      dbg_printf ("p->size = %d, newentry->size = %d, p->size + newentry->size + sizeof (...) = %d(0x%x)\n",
		      p->size, newentry->size, p->size + newentry->size + sizeof (struct alloc_entry_t), 
		      p->size + newentry->size + sizeof (struct alloc_entry_t));
#endif

	      /* ���ߤΥ���ȥ���٤��äƤ���
	       */
	      p->size += (newentry->size + sizeof (struct alloc_entry_t));
	      if (((UW)p + p->size + sizeof (struct alloc_entry_t)) == (UW)p->next)
		{
		  p->size += (p->next->size + sizeof (struct alloc_entry_t));
		  p->next->next->before = p;
		  p->next = p->next->next;
		}
	      return;
	    }
	  else if ((((UW)newentry) + newentry->size + sizeof (struct alloc_entry_t)) == (UW)(p->next))
	    {

	      /* ���Υ���ȥ���٤��äƤ���
	       */
	      newentry->size += (p->next->size + sizeof (struct alloc_entry_t));
	      newentry->before = p;
	      newentry->next = p->next->next;
	      p->next = newentry;
	      return;
	    }
	  else
	    {

	      /* ����Υ���ȥ�ȤϤɤ���Ȥ��٤��äƤ��ʤ� -
	       * ñ�˥ꥹ�ȤˤĤʤ���
	       */
	      newentry->before = p;
	      newentry->next = p->next;
	      p->next = newentry;
	      newentry->next->before = newentry;
	      return;
	    }
	}
      else if ((((UW)p + p->size + sizeof (struct alloc_entry_t)) < (UW)newentry) &&
	       (p > p->next))
	{

	  /* �ꥹ�Ȥκǹ�̤Υ��ɥ쥹�ޤ��Ϥ���
	   * �ꥹ�ȤκǸ���ɲä���
	   */
	  newentry->before = p;
	  newentry->next = p->next;
	  p->next = newentry;
	  newentry->next->before = newentry;
	  return;
	}

      if (p == start_point->before)
	{
	  break;
	}

    }	/* for */


#if 1
  /* ����������ʬ���ʤ��ä�
   */
  dbg_printf ("malloc:free: missing. cannot found insert entry in free list.\n");
#endif
}





static VP
get_system_memory (UW size)
{
  ID	mytid;
  VP	rp;
  ER	errno;

  if (get_tid (&mytid) != E_OK)
    {
      return (NULL);
    }

#ifdef notdef
  dbg_printf ("get_system_memory: id = %d, start = 0x%x, size = %d\n", mytid, last_page, size);
#endif
  errno = vmap_reg (mytid, (VP) last_page, size, ACC_KERNEL);
  if (errno)
    {
#ifdef notdef
      dbg_printf ("get_system_memory: can't vmap_reg %d\n", errno);
#endif
      return (NULL);
    }

  rp = (VP)last_page;
  last_page += size;
  return (rp);
}



void
print_freelist (void)
{
  struct alloc_entry_t	*p;
  int			i;

  for (i = 0, p = pivot; ; p = p->next, i++)
    {
#if 1
      dbg_printf ("[%d]: 0x%x, next = 0x%x, before = 0x%x, size = %d (0x%x)\n",
	      i, p, p->next, p->before, p->size, p->size);
#endif
      if (p->next == pivot)
	break;
    }
}
