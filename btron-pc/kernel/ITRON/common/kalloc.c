/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* kalloc.c

  �����ͥ�����ꥢ�������ȴؿ���
 
  Interface:
 
 	kalloc()	���ꤷ��������ʬ�����Υ����ΰ�򥢥������Ȥ���
			��Ƭ���ɥ쥹���֤���

 	kfree()		kalloc() �ǥ��������Ȥ����ΰ��������롣
 
 */

#include "itron.h"
#include "errno.h"
#include "misc.h"
#include "func.h"


#define CLICK_SIZE	(10 * PAGE_SIZE)

struct kmem_entry
{
  struct kmem_entry	*next;
  W			size;
  B			body[0];
};

static struct kmem_entry	freelist;
static struct kmem_entry	*pivot;

#ifdef MEMORY_TEST
#define palloc	malloc

main ()
{
  int	size;

  init_kalloc ();
  print_memory_list ();
  kalloc (10);
  print_memory_list ();
}
#endif /* MEMORY_TEST */


void
print_memory_list (void)
{
  struct kmem_entry	*p;

  printk ("ADDR: 0x%x    SIZE: %d    NEXT: 0x%x\n",
	  &freelist, freelist.size, freelist.next);
  for (p = freelist.next; p != &freelist; p = p->next)
    {
      printk ("ADDR: 0x%x    SIZE: %d    NEXT: 0x%x\n",
	      p, p->size, p->next);
    }
}


/* getcore --- ������kalloc��������������ΰ���ɲä��롣
 */
static ER
getcore(W size)
{
  struct kmem_entry *newp;

  newp = (struct kmem_entry *)palloc (ROUNDUP (size, CLICK_SIZE) / PAGE_SIZE);
  if (newp == NULL)
    {
      return (E_NOMEM);
    }

/* �������������ΰ��ͳ�ΰ���ɲä���
 */
  kfree (newp->body, ROUNDUP (size, CLICK_SIZE) - sizeof(struct kmem_entry));
  return (E_OK);
}


/* init_kalloc --- kalloc �Τ���Υե꡼�ꥹ�Ȥν����
 *
 */
void
init_kalloc (void)
{
#if 0
  T_CMPL flag;
#endif

  pivot = &freelist;
  freelist.size = 0;
  freelist.next = &freelist;
}


/* kalloc --- �����ΰ�򿷤����ɲä��롣
 *
 */
VP
kalloc (W size)
{
  struct kmem_entry	*front, *before;

retry:
  /* search freelist */
  for (before = pivot, front = pivot->next;
       front != pivot;
       front = front->next)
    {
      if (front->size == size)
	{
	  /* �����������٤ξ��; �ե꡼�ꥹ�Ȥ��饨��ȥ�������� */
	  before->next = front->next;
	  break;
	}
      else if (front->size >= (size + sizeof (struct kmem_entry)))
	{
	  /* ���������礭�����; front �λؤ�����ȥ꤫���Ⱦʬ���� */
	  front->size -= size + sizeof (struct kmem_entry);
    front = (struct kmem_entry *)(((UW) front->body) + front->size);
    front->size = size;
	  break;
	}
      before = front;
    }

  /* ����ȥ꤬���Ĥ���ʤ��ä�; �ڡ����򥢥������Ȥ��롣
   */
  if (front == pivot)
    {
      if (getcore (size) != E_OK)
	return (NULL);
      goto retry;
    }
  pivot = before;
  return (front->body);
}

/* kfree --- ���ꤷ�����ꥢ��ե꡼����ꥹ�Ȥ��ɲä��롣
 *
 */
void
kfree (VP ap, W size)
{
  struct kmem_entry *p, *before;
  struct kmem_entry *area;

  area = (struct kmem_entry *)(((UW) ap) - sizeof(struct kmem_entry)) ;
  area->size = size;

  /* �ե꡼�ꥹ�Ȥκǽ餫��Ǹ�ޤǤ�õ�����롣
   */
  before = &freelist;
  p = freelist.next;

  if (before == p) {
    before->next = area;
    area->next = before;
    return;
  }

#ifdef notdef
  while ((p < area) && (p != &freelist))
    {
      before = p;
      p = before->next;
    }

  if ((B *)before + before->size == (B *)area)
    {
      area = before;
      before->size += size;
    }
  else
    {
      area->size = size;
      before->next = area;
    }

  if ((B *)area + size == (B *)p)
    {
      /* �ΰ�����Τ�Τ����礹�� */

      area->next = p->next;
      area->size += p->size;
      if (pivot == p)
	{
	  pivot = area;
	}
    }
  else
    {
      area->next = p;
    }
#else
  if (area > pivot) before = pivot;
  p = before;
  while(1) {
    /* �ե꡼�ꥹ�Ȥ���Ƭ����é�ꡢ�ꥹ�Ȥ���������ݥ���Ȥ���� */
    /* �ե꡼�ꥹ�Ȥϥ��ɥ쥹��ˤʤäƤ��ꡢp �Υ��ɥ쥹���ɲä� */
    /* ���ΰ�κǸ�����礭���ʤä��顢���������������롣*/
    if ((((UW)p->body) + p->size <= (UW)area) &&
	((p->next < p) || ((((UW)area->body) + area->size) <= (UW)p->next))) {
      if ((((UW)p->body) + p->size) == (UW)area) {
	/* ���ߤΥ���ȥ���٤��äƤ���
	 */
	p->size += (area->size + sizeof (struct kmem_entry));
	if ((((UW)p->body) + p->size) == (UW)p->next) {
	  p->size += (p->next->size + sizeof (struct kmem_entry));
	  p->next = p->next->next;
	}
	return;
      }
      else if ((((UW)area->body) + area->size) == (UW)(p->next)) {
	  /* ���Υ���ȥ���٤��äƤ��� */
	  area->size += (p->next->size + sizeof (struct kmem_entry));
	  area->next = p->next->next;
	  p->next = area;
	  return;
      }
      else {
	/* ����Υ���ȥ�ȤϤɤ���Ȥ��٤��äƤ��ʤ� -
	 * ñ�˥ꥹ�ȤˤĤʤ���
	 */
	area->next = p->next;
	p->next = area;
	return;
      }
    }
    p = p->next; 
    if (p == before) break;
  }	/* for */
  printk("kfree: missing. %x:%d \n",
	 area, area->size+sizeof(struct kmem_entry));
#endif
}
