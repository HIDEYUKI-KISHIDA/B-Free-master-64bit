#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
#define NULL ((void*)0)
#define __attribute__(x)
#define __asm__(x)
#define __volatile__
#define __restrict
#define __inline__
#define __extension__
#define __builtin_va_list int
#define __builtin_va_start(a,b)
#define __builtin_va_end(a)
#define __builtin_va_arg(a,b) (0)
#define __builtin_offsetof(type, member) ((size_t)&(((type *)0)->member))
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *d, const void *s, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern int printf(const char *fmt, ...);
extern int puts(const char *s);
extern int putchar(int c);
extern int getchar(void);
extern int sprintf(char *str, const char *fmt, ...);
extern int snprintf(char *str, size_t size, const char *fmt, ...);
extern int sscanf(const char *str, const char *fmt, ...);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strcpy(char *d, const char *s);
extern char *strncpy(char *d, const char *s, size_t n);
extern size_t strlen(const char *s);
extern void abort(void);
extern void exit(int code);
extern int atexit(void (*f)(void));
extern int setjmp(jmp_buf env);
extern void longjmp(jmp_buf env, int val);
#endif
/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* 2nd boot �ץ������ΥС������ */
static char rcsid[] = "#(@)$Version$";
