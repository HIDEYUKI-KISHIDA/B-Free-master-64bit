/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* lowlib.c --- lowlib ��Ϣ�δؿ�
 *
 *
 */


/* $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/common/lowlib.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $ */

static unsigned char	rcsid[] = "$Id: lowlib.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";


#include "../h/types.h"
/*#include "../../../boot/2nd/boot.h"*/
#include "../h/errno.h"
#include "../h/task.h"
#include "../h/func.h"
#include "../h/lowlib.h"



struct lowlib_info	lowlib_table[MAX_MODULE];
W nlowlib = 0;


/*
 *	�����ǻ��ꤷ�� lowlib �⥸�塼��ν����
 */
ER
init_lowlib (struct module_info *modp)
{
  ER	(*init)(struct lowlib_info *);
  ER	errno;

  if (modp->type != lowlib)
    {
      return (E_PAR);
    }


  printk ("LOWLIB: call init function in module.\n");

  init = (ER (*)(struct lowlib_info *))modp->entry;
  errno = (init)(&lowlib_table[nlowlib]);
  if (errno == E_OK)
    {
      bcopy (modp->name, lowlib_table[nlowlib].name, MAX_MODULE_NAME);
      lowlib_table[nlowlib].modp = modp;
      printk ("LOWLIB: [%d] install %s module\n", nlowlib, modp->name);
      nlowlib++;
    }
  else
    {
      printk ("loading error (errno = %d)\n", errno);
    }

  return (errno);
}



/*
 *	���ꤷ���������� LOWLIB �򤯤äĤ���
 */
ER
load_lowlib (VP *argp)
{
  struct a
    {
      ID task;
      B *name;
    } *args = (struct a*)argp;

  W			i;
  T_TCB			*tskp;
#ifdef MONAKA
  VP			ppage;
  struct lowlib_data	*plowlib;
#endif
  ER			errno;
  struct lowlib_data ld;
  UW paddr;

  if (args->task < 0)
    {
      return (E_ID);
    }
  if (args->name == NULL)
    {
      return (E_PAR);
    }

  tskp = get_tskp (args->task);
  if (tskp == NULL)
    {
      return (E_ID);
    }


  for (i = 0; i < nlowlib; i++)
    {
      if (strncmp (args->name, lowlib_table[i].name, MAX_MODULE_NAME) == 0)
	{
	  printk("Found module %s == %s\n", args->name, lowlib_table[i].name);
	  /* ��������⥸�塼���ȯ��������*/

	  /* �������˳����߽����ؿ�����Ͽ���� */
	  if (lowlib_table[i].intr_func)
	    {
	      if (tskp->n_interrupt >= MAX_MODULE)
		{
		  return (E_SYS);
		}
	      
	      tskp->interrupt[tskp->n_interrupt].intr_no = lowlib_table[i].intr;
	      tskp->interrupt[tskp->n_interrupt].intr_func = lowlib_table[i].intr_func;
	      tskp->n_interrupt++;
	    }
	  printk("Registed interrupt functions to the task.\n");

	  /* lowlib �����Ѥ��륿������˰ۤʤ����(������ ID �ʤ�) ��
	   * �ΰ�����ꤹ�롣
	   * ���Ѥ��벾�ۥ����ΰ� (LOWLIB_DATA) ��ʪ������˥ޥåפ��롣
	   * ������������ Region ������⤹��ɬ�פ����뤫�⡣��
	   */

    if (tskp->n_interrupt == 1)
      {
        paddr = vtor(args->task, (UW) LOWLIB_DATA);
        if (paddr == 0)
    {
      errno = vmap_reg (args->task, LOWLIB_DATA, sizeof (struct lowlib_data), ACC_KERNEL);
      if (errno)
        {
          return (errno);
        }
    }
        else
    {
      printk("WARNING: LOWLIB_DATA has already been mapped\n");
    }
        bzero(&ld, sizeof(struct lowlib_data));
        errno = vput_reg(args->task, LOWLIB_DATA,
           sizeof(struct lowlib_data), &ld);
        if (errno)
    {
      return (errno);
    }
      }
#ifdef MONAKA
	  plowlib = (struct lowlib_data *)ppage;
	  bzero (plowlib, sizeof (struct lowlib_data));
#endif
	  return (E_OK);
	}
    }

     
  return (E_PAR);
}  

/*
 *	Removeing LOWLIB on specified task.
 */
ER
unload_lowlib (VP *argp)
{
  struct a
    {
      ID task;
      B *mod_name;
    } *args = (struct a *)argp;

  return (E_NOSPT);
}


/*
 *	LOWLIB �ξ�����֤�
 */
ER
stat_lowlib (VP *argp)
{
  struct a
    {
      B *name;
      W *nlowlib;		/* lowlib ����Ͽ�� (name == */
				/* NULL �ΤȤ�) 	    */
      struct lowlib_info *infop;
    } *args = (struct a *)argp;
  W i;

  if ((args->name == NULL) && (args->infop == NULL))
    {
      printk ("sts_low: nlowlib = %d\n", nlowlib);
      *(args->nlowlib) = nlowlib;
      return (E_OK);
    }

  if (args->infop == NULL)
    {
      return (E_PAR);
    }

  if (args->name == NULL)
    {
      if ((*(args->nlowlib) < 0) || (*(args->nlowlib) >= nlowlib))
	{
	  return (E_PAR);
	}

      bcopy (&lowlib_table[*(args->nlowlib)], args->infop, sizeof (struct lowlib_info));
      return (E_OK);
    }

  for (i = 0; i < nlowlib; i++)
    {
      if (strncmp (args->name, lowlib_table[i].name, MAX_MODULE_NAME) == 0)
	{
	  /* ��������⥸�塼���ȯ��������*/

	  bcopy (&lowlib_table[i], args->infop, sizeof (struct lowlib_info));
	  return (E_OK);
	}
    }

  return (E_ID);
}

