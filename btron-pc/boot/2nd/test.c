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
void *memset(void *s, int c, size_t n) { return s; }
void *memcpy(void *d, const void *s, size_t n) { return d; }
int memcmp(const void *s1, const void *s2, size_t n) { return 0; }
int printf(const char *fmt, ...) { return 0; }
int puts(const char *s) { return 0; }
extern int putchar(int c);
int getchar(void) { return 0; }
int sprintf(char *str, const char *fmt, ...) { return 0; }
int snprintf(char *str, size_t size, const char *fmt, ...) { return 0; }
int sscanf(const char *str, const char *fmt, ...) { return 0; }
int strcmp(const char *s1, const char *s2) { return 0; }
int strncmp(const char *s1, const char *s2, size_t n) { return 0; }
char *strcpy(char *d, const char *s) { return d; }
char *strncpy(char *d, const char *s, size_t n) { return d; }
size_t strlen(const char *s) { return 0; }
void abort(void) { while(1); }
void exit(int code) { while(1); }
int atexit(void (*f)(void)) { return 0; }
int setjmp(jmp_buf env) { return 0; }
void longjmp(jmp_buf env, int val) { while(1); }
#endif
int main(){return 0;}
