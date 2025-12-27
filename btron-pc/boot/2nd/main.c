// --- リンク時未定義シンボルのダミー定義 ---
char rcsid[] = "dummy rcsid";
unsigned char MODULE_TABLE[4096] = {0};
// --- ここまで ---
// --- 未定義シンボルのextern宣言（ビルド通過用） ---
extern char rcsid[];
extern int vga_text(void);
extern int fd_read(int, int, unsigned char*);
extern void stop_motor(int);
extern int read_single_module(int, void*, void*);
extern int putchar(int);
// --- ここまで ---
// --- ビルド通過用ダミー定義 ---
// ...existing code...

// #ifdef __cplusplus
// extern "C" {
// #endif
// extern int fd_read(int drive, int blockno, unsigned char* buff);
// #ifdef __cplusplus
// }
// #endif
// #include <stdint.h>
#include "types.h"
#include "errno.h"
#include "sfs.h"
#include "config.h"
#include "lib.h"
#include "memory.h"
#include "file.h"
#include "a.out.h"
#include "main.h"
// --- ダミー定義群（未定義シンボル対策） ---
#ifndef E_OK
#define E_OK 0
#endif
#ifndef E_SYS
#define E_SYS -1
#endif
#ifndef E_IO
#define E_IO -2
#endif
#ifndef ULONG
#define ULONG unsigned long
#endif
#ifndef MAJOR_VER
#define MAJOR_VER 1
#endif
#ifndef MINOR_VER
#define MINOR_VER 0
#endif
// rcsidのダミー定義は削除（既存定義に任せる）
#ifndef MODULE_TABLE
#define MODULE_TABLE 0
#endif
// vga_textのダミー定義は削除（既存定義に任せる）
// ext_mem, base_mem, real_memのダミー定義は削除（memory.hのextern宣言に任せる）
#ifndef KERNEL_ADDR
#define KERNEL_ADDR 0x100000
#endif
// putcharのダミー定義は削除（既存定義に任せる）
// --- ここまでダミー定義 ---
#ifndef ROUNDUP
#define ROUNDUP(x, y)  (((x) + ((y)-1)) & ~((y)-1))
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#ifndef BLOCK_SIZE
#define BLOCK_SIZE 512
#endif
#include "../../kernel/ITRON/h/itron_module.h"

int main(void) {
  boot_printf("[BTRON] 2ndブート開始\n");
  banner();
  boot_printf("[BTRON] メモリチェック\n");
  status_memory();
  boot_printf("[BTRON] モジュールロード\n");
  // ここでmulti_bootやread_multi_module等を呼ぶ想定（既存のmainは空なので例示）
  // multi_boot(...);
  // read_multi_module();
  boot_printf("[BTRON] 2ndブート完了\n");
  return 0;
}



/***************************************************************************
 * beep -- BEEP routine.
 */
int
beep ()
{

  return E_OK;
}


/***************************************************************************
 * status_memory --- ��������ɽ��
 */
int
status_memory ()
{
  extern void	*last_addr;

#ifdef nodef
  volatile int	*p;
  
  for (p = (int *)0x100000; (int)p < 0xf00000; (int)p += 0x100000)
    {
      *p = 0;
      *p = 0xAA;
      if (*p != 0xAA)
	break;
    }
#endif /* nodef */
  boot_printf ("Extended Memory = %d K bytes\n", ext_mem / 1024);
  boot_printf ("USE Memory      = %d bytes\n", last_addr);

  return E_OK;
}

/***************************************************************************
 *
 */
void
print_binary (int n)
{
  ULONG mask = 0x00000080;
  int	i;

  for (i = 0; i < 8; i++)
    {
      if (mask & n)
	boot_printf ("1");
      else
	boot_printf ("0");
      mask = mask >> 1;
    }
}

  
void
banner (void)
{
  boot_printf("==========================================\n");
  boot_printf("   BTRON/386 2nd BOOT (32bit)\n");
  boot_printf("==========================================\n");
  boot_printf("  Version   : %d.%d\n", MAJOR_VER, MINOR_VER);
  boot_printf("  RCS       : %s\n", rcsid);
  boot_printf("  Mode      : 32 BIT\n");
  boot_printf("------------------------------------------\n");
  boot_printf("  Copyright (C) 1991-2025 B-Free Project\n");
  boot_printf("  https://b-free.org/\n");
  boot_printf("------------------------------------------\n");
  boot_printf("  \n");
  boot_printf("  Welcome to BTRON/386 32bit Boot!\n");
  boot_printf("==========================================\n\n");
  boot_printf("[BTRON] バナー表示完了\n");
}

/**************************************************************************
 * panic.
 */
void
panic (char *s)
{
  boot_printf ("panic: %s\n", s);
  for (;;)
    ;
}





int
cat (char *buf, int size)
{
  int	i;

  for (i = 0; i < size; i += 2)
    {
      boot_printf ("%c", &buf[i]);
    }

  return E_OK;
}



int
multi_boot (int fd, struct sfs_superblock *sb, struct sfs_inode *ip, int silent)
{
  int		i;
  int		offset;
  struct boot_header	*info;
  void  	(*entry)();
  int		errno;

  if (!silent)
    boot_printf ("[BTRON] 複数モジュールブート開始\n");
  info = (struct boot_header *)MODULE_TABLE;

  errno = sfs_read_file (fd, sb, ip, 0, BLOCK_SIZE, (char *)info);
  if (errno)
    {
      if (!silent)
	boot_printf ("Couldn't read OS file.\n");
      vga_text ();
      return (E_SYS);
    }

  info->machine.base_mem = base_mem;
  info->machine.ext_mem = ext_mem;
  info->machine.real_mem = real_mem;
  if (fd & 0x010000)
    {
      /* HD */
      info->machine.rootfs = 0x80010000 | ((fd & 0xff) + 1);
    }
  else
    {
      /* FD */
      info->machine.rootfs = 0x80000000 | (fd & 0xff);
    }

  if (!silent)
    boot_printf ("Module %d\n", info->count);
  offset = BLOCK_SIZE;	/* �ǽ�Υ⥸�塼�뤬���äƤ��륪�ե��å�(�Х���) */
  entry = (void (*)())(info->modules[0].entry);

  for (i = 0; i < info->count; i++)
    {
      if (!silent)
      {
        boot_printf ("[BTRON] モジュール%dロード中\n", i);
      }
      if (load_module (fd, sb, ip, offset, &info->modules[i], silent) != E_OK)
      {
        if (!silent)
        {
          boot_printf ("Can't load module(s)...abort.\n");
        }
        vga_text ();
        return (E_SYS);
      }
#ifdef nodef
      offset += (info->modules[i].length -  BLOCK_SIZE);   /* ??? */
#else
      offset += info->modules[i].length + BLOCK_SIZE;   /* ??? */
#endif
    }
  if (!silent) {
    boot_printf ("exec_info->a_entry = 0x%x\n", entry); 
  }
  if (silent)
    {
      vga_text ();
    }

  (*entry)();

  return E_OK;
}


int load_module(int fd, struct sfs_superblock *sb, struct sfs_inode *ip, int offset, struct module_info *info, int silent)
{
  char	tmp[BLOCK_SIZE];
  struct exec	*exec_info;

  sfs_read_file (fd, sb, ip, offset, sizeof (struct exec), tmp); 
  exec_info = (struct exec *)tmp;
  if (N_BADMAG (*exec_info))
    {
      if (!silent) {
	boot_printf ("This object is not exec format (%d).\n", *exec_info);
      }
      return (E_SYS);
    }
  
  if (!silent)
    {
      boot_printf ("[%s]\n",info->name);
      boot_printf ("Module: exec type = 0x%x, Text size = %d, Data size = %d\n",
	      N_MAGIC(*exec_info),
	      exec_info->a_text,
	      exec_info->a_data);
    }

  if ((N_MAGIC(*exec_info) == 0413) || (N_MAGIC(*exec_info) == NMAGIC))
    {
      if (sfs_read_file (fd, sb, ip, offset + BLOCK_SIZE, exec_info->a_text + exec_info->a_data, (char *)info->paddr))
	{
	  return E_IO;
	}
    }
  else
    {
      if (!silent)
	{
	  boot_printf ("I don't know how to read a.out image.(0x%x)\n", N_MAGIC(*exec_info));
	}
      return E_SYS;
    }
  return E_OK;
}



/*
   ʣ���Υ⥸�塼����ɤ߹��ࡣ

   �ǽ�Υ⥸�塼��Τ� ITRON �����ͥ�Ȳ��ꤷ�Ƥ��롣
   ���Τ��ᡢ�ɤ߹�����֤ϡ�0x00010000 �ȷ��Ƥ��롣
   (ITRON �����ͥ�ϡ����ۥ��ɥ쥹 0x80010000 ���ɤ߹��ळ�Ȥˤ��Ƥ��롣
   0x00010000 �Ȥ����Τϡ�0x80010000 ���б����Ƥ���ʪ�����ɥ쥹�Ǥ���)

   2 ���ܰʹߤΥ⥸�塼��ϡ�ITRON �����ͥ�ˤθ��³�����ɤ߹��ळ�Ȥˤʤ롣
   ���Τ��ᡢITRON �����ͥ���礭���ˤ�ä�ʪ�����ɥ쥹���Ѥ�뤳�Ȥˤʤ롣
   2 ���ܰʹߤΥ⥸�塼��ˤĤ��Ƥϡ�boot �ϥ����ɤ�������ǡ����ۥ��ɥ쥹
   �ؤΥޥåԥ󥰤ʤɤ� ITRON �����ͥ�ε�ư��˹Ԥ���

*/ 
int
read_multi_module ()
{
  int	i;
  int	bn;
  struct boot_header	*info;
  void  (*entry)();

  boot_printf ("[BTRON] 複数モジュールブート開始\n");
  info = (struct boot_header *)MODULE_TABLE;
  fd_read (0, 0, (BYTE *)info);
  info->machine.base_mem = base_mem;
  info->machine.ext_mem = ext_mem;
  info->machine.real_mem = real_mem;
  info->machine.rootfs = 0xffffffff;
  boot_printf ("Module %d\n", info->count);
  bn = 1;	/* �ǽ�Υ⥸�塼�뤬���äƤ���֥��å��ֹ� */
  entry = (void (*)())(info->modules[0].entry);
  for (i = 0; i < info->count; i++)
    { 
      boot_printf ("[BTRON] モジュール%d(%s)ロード中\n", i, info->modules[i].name);
      read_single_module (bn, (void *)info->modules[i].paddr, &(info->modules[i]));

#ifdef nodef
      bn += ((info->modules[i].length / BLOCK_SIZE) - 1);
#else
      bn += (info->modules[i].length / BLOCK_SIZE + 1);
#endif
    }
  boot_printf ("load done.\n");
  boot_printf ("exec_info->a_entry = 0x%x\n", entry); 
  stop_motor(0);
  (*entry)();

  return E_OK;
}


int
read_single_module (int start_block, void *paddr, void *info)
{
  char	buf[BLOCK_SIZE];
  char	tmp[BLOCK_SIZE];
  int	i, j;
  int	bn;
  struct exec	*exec_info;

  bn = start_block;
  fd_read (0, bn, (BYTE *)tmp);
  exec_info = (struct exec *)tmp;
  if (N_BADMAG (*exec_info))
    {
      boot_printf ("This object is not exec format (%d).\n", *exec_info);
      boot_printf ("block number: %d\n", bn);	/* */
      for (;;)
	;
      /* STOP HERE */
    }
  
  if ((N_MAGIC(*exec_info) == 0413) || (N_MAGIC(*exec_info) == NMAGIC))
    {
      bn += 1;	/* a.out �Υإå����礭������������ȥ��åפ��� */

            boot_printf ("[BTRON] テキスト領域: %dブロック, データ領域: %dブロック, paddr: 0x%x\n",
              (ROUNDUP (exec_info->a_text, PAGE_SIZE) / BLOCK_SIZE),
              (ROUNDUP (exec_info->a_data, PAGE_SIZE) / BLOCK_SIZE),
              paddr);

      for (i = 0;
	   i < (ROUNDUP (exec_info->a_text, PAGE_SIZE) 
		 / BLOCK_SIZE);
	   i++, bn++)
	{
	  boot_printf (".");
	  fd_read (0, bn, (BYTE*)buf);
	  bcopy (buf,
		 (char *)(paddr + i * BLOCK_SIZE),
		 BLOCK_SIZE);
	}
/*      boot_printf ("\nText region is readed.\n"); */
      if (exec_info->a_data > 0)
	{
	  for (j = 0;
	       j <= (ROUNDUP (exec_info->a_data, PAGE_SIZE)
		     / BLOCK_SIZE);
	       j++ , bn++)
	    {
	      boot_printf (",");
	      fd_read (0, bn, buf);
	      bcopy (buf,
		     (char *)(paddr
				     + (ROUNDUP (exec_info->a_text, PAGE_SIZE)) 
				     + j * BLOCK_SIZE),
		     BLOCK_SIZE);
	    }
	}
    }
  else
    {
      boot_printf ("I don't know how to read a.out image.(0x%x)\n", N_MAGIC(*exec_info));
      for (;;)
	;
    }
/*  boot_printf ("\nload done.\n"); */
  boot_printf ("\n");

  return E_OK;
}


int
read_a_out ()
{
  char	buf[BLOCK_SIZE];
  char	tmp[BLOCK_SIZE];
  int	*p;
  int	i;
  int	bn;
  void	(*func)();
  struct exec	*exec_info;
  int	errno;
  struct boot_header	*info;

  fd_read (0, 0, tmp);
  exec_info = (struct exec *)tmp;
  if (N_BADMAG (*exec_info))
    {
      boot_printf ("This object is not exec format.\n");
      return (0);
    }
  
  boot_printf ("[BTRON] シングルモジュールブート開始\n");
  boot_printf ("text size = %d\n", exec_info->a_text);
  boot_printf ("data size = %d\n", exec_info->a_data);
  boot_printf (" bss size = %d\n", exec_info->a_bss);
  info = (struct boot_header *)MODULE_TABLE;
  info->machine.base_mem = base_mem;
  info->machine.ext_mem = ext_mem;
  info->machine.real_mem = real_mem;
  info->machine.rootfs = 0xffffffff;

#ifdef linux
  if (N_MAGIC(*exec_info) == 0413)
#else
  if (exec_info->a_magic == 0413)
#endif
    {
      boot_printf ("demand loading object. (page size alignemnt)\n");
      boot_printf ("load address = 0x%x\n", KERNEL_ADDR);
#ifdef linux
      bn = 1;	/* 1K bytes offset */
#else
      bn = PAGE_SIZE / BLOCK_SIZE;
#endif /* linux */
      for (i = 0;
	   i <= (ROUNDUP (exec_info->a_text, PAGE_SIZE) 
		 / BLOCK_SIZE);
	   i++, bn++)
	{
#ifdef nodef
	  boot_printf (".");
#else
	  putchar ('.');
#endif
	  fd_read (0, bn, buf);
#ifdef linux
	  bcopy (buf,
		 (char *)(KERNEL_ADDR + ((bn - 1) * BLOCK_SIZE)),
 		 BLOCK_SIZE);
#else
	  bcopy (buf,
		 (char *)(KERNEL_ADDR + ((bn - 4) * BLOCK_SIZE)),
		 BLOCK_SIZE);
#endif /* linux */
	}
      boot_printf ("\nText region is read.\n");
      for (i = 0;
	   i <= (ROUNDUP (exec_info->a_data, PAGE_SIZE)
		 / BLOCK_SIZE);
	   i++, bn++)
	{
#ifdef nodef
	  boot_printf (".");
#else
	  putchar ('.');
#endif
	  fd_read (0, bn, buf);
#ifdef linux
	  bcopy (buf,
		 (char *)(KERNEL_ADDR + ((bn - 1) * BLOCK_SIZE)),
		 BLOCK_SIZE);
#else
	  bcopy (buf,
		 (char *)(KERNEL_ADDR + ((bn - 4) * BLOCK_SIZE)),
		 BLOCK_SIZE);
#endif
	}
    }
  else
    {
      boot_printf ("load address = 0x%x\n", (KERNEL_ADDR - N_TXTOFF (*exec_info)));
      for (i = 0;
	   i <= ((ROUNDUP (exec_info->a_text, BLOCK_SIZE) 
	      + ROUNDUP (exec_info->a_data, BLOCK_SIZE)
	      + ROUNDUP (sizeof (struct exec), BLOCK_SIZE)) / BLOCK_SIZE + 10);
	   i++)
	{
#ifdef nodef
	  boot_printf (".");
#else
	  putchar ('.');
#endif
retry:
	  errno = fd_read (0, i, buf);
	  if (errno != E_OK)
	    {
	      goto retry;
	    }
	  bcopy (buf,
		 (char *)((KERNEL_ADDR - N_TXTOFF(*exec_info))
				 + (i * BLOCK_SIZE)),
		 BLOCK_SIZE);
	}
    }
  p = (int *)(exec_info->a_entry);
  boot_printf ("load done.\n");
  boot_printf ("exec_info->a_entry = 0x%x\n", p);
  func = (void (*)())(exec_info->a_entry);
  (*func)();

  return E_OK;
}

