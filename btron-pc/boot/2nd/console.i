# 0 "console.c"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "console.c"
# 17 "console.c"
extern void busywait(int x);
extern void wait_int(int *x);
extern void setup_dma(void *a, int b, int c, int d);




extern void lock(void);
extern void unlock(void);
# 89 "console.c"
static char rcsid[] = "$Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/console.c,v 1.1 2011/12/27 17:13:35 liu1 Exp $";

# 1 "lib.h" 1



# 1 "types.h" 1
# 50 "types.h"
typedef char SBYTE;
typedef unsigned char BYTE;
typedef short WORD16;
typedef unsigned short UWORD16;
typedef long WORD32;
typedef unsigned long UWORD32;
typedef long long WORD64;
typedef unsigned long long UWORD64;
typedef long WORD;
typedef unsigned long UWORD;
typedef long LONG;
typedef unsigned long ULONG;
typedef long long LONG64;
typedef unsigned long long ULONG64;
typedef float FLOAT;
typedef double DOUBLE;
typedef void VOID;

typedef enum { FALSE=0, TRUE=1 } BOOL;
typedef unsigned char SCODE;
typedef unsigned char TCODE;

typedef char *SBPTR;
typedef unsigned char *BPTR;
typedef short *WPTR;
typedef unsigned short *UWPTR;
typedef long *LPTR;
typedef unsigned long *ULPTR;
typedef long long *L64PTR;
typedef unsigned long long *UL64PTR;

typedef TCODE *TPTR;
typedef SCODE *SPTR;
typedef int (*FUNCP)();
# 5 "lib.h" 2

extern void boot_printf (char *fmt, ...);
extern WORD strlen (BPTR s);
extern WORD strnlen (BPTR s, WORD len);
extern WORD strcpy (BPTR s1, BPTR s2);
extern WORD strncpy (BPTR s1, BPTR s2, WORD n);
extern WORD strncpy_with_key (BPTR s1, BPTR s2, WORD n, int key);
extern BPTR strcat (BPTR s1, BPTR s2);
extern BPTR strncat (BPTR s1, BPTR s2, WORD n);
extern ULONG strcmp (BPTR s1, BPTR s2);
extern ULONG strncmp (BPTR s1, BPTR s2, WORD n);
extern char *strchr (char *s, int ch);
extern char *strnchr (char *s, int ch, int n);

extern int toupper (int ch);

extern void write_vram (int x, int y, int ch, int attr);
extern void set_cursor_pos (int x, int y);
extern void scroll_up ();

extern int atoi (char *s);
extern int string_to_number (char *s, int base);

extern void bcopy (const char *src, char *dest, int length);
extern void bzero (char *src, int length);
# 92 "console.c" 2
# 1 "errno.h" 1
# 101 "errno.h"
typedef UWORD32 ERRNO;
# 93 "console.c" 2
# 1 "location.h" 1
# 94 "console.c" 2
# 1 "memory.h" 1
# 22 "memory.h"
extern void *last_addr;
extern UWORD32 real_mem, ext_mem, base_mem;

void init_memory (void);
# 95 "console.c" 2
# 1 "console.h" 1






void console_clear(void);

static inline void console_putc(char c) {}
static inline void console_puts(const char *s) {}
# 96 "console.c" 2
# 1 "macros.h" 1
# 97 "console.c" 2

static int x;
static int y;

void write_cr (void);
void write_tab (void);




int
init_console (void)
{
  int i;

  console_clear ();
  for (i = 0; i < 25; i++)
    {
      scroll_up ();
    }

  x = y = 0;
  set_cursor_pos (x, y);

  return (0);
}

void
console_clear (void)
{
  int x, y;

  for (y = 0; y <= 25; y++)
    for (x = 0; x <= 80; x++)
      {
 write_vram (x, y, ' ', 0xE1);
      }
}



void
write_cr ()
{
  x = 0;
  if (y >= 25)
    {
      scroll_up ();
    }
  else
    {
      y++;
    }
  set_cursor_pos (x, y);
}




void
write_tab ()
{
  int tmp;
  if (x < 80)
    {
      tmp = ((((x + 1) + ((8) - 1)) / (8)) * (8)) - 1;
      while (x < tmp)
 {
   write_vram (x, y, ' ', 0);
   x++;
 }
    }
}





extern int putchar(int ch);

void set_coursor(int sx, int sy)
{
  x = sx;
  y = sy;
  set_cursor_pos (x,y);
}
void get_coursor(int *gx,int *gy)
{
  *gx = x;
  *gy = y;
}





char
console_getchar(void)
{
 char c;




 c = 0;




 return c;
}




void
console_init(void)
{
 init_console();
}
