/*
 * Minimal libc + host-I/O stubs for freestanding kernel.elf (M10).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BFREE_HEAP_SIZE (256 * 1024)

static unsigned char bfree_heap[BFREE_HEAP_SIZE];
static size_t bfree_heap_used;
int errno;

static void *heap_alloc(size_t size, int zero)
{
	size_t aligned = (size + 15U) & ~15U;
	void *p;

	if (bfree_heap_used + aligned > BFREE_HEAP_SIZE)
		return NULL;
	p = &bfree_heap[bfree_heap_used];
	bfree_heap_used += aligned;
	if (zero)
		memset(p, 0, aligned);
	return p;
}

void *malloc(size_t size)
{
	return heap_alloc(size, 0);
}

void *calloc(size_t nmemb, size_t size)
{
	size_t total;

	if (nmemb != 0 && size > BFREE_HEAP_SIZE / nmemb)
		return NULL;
	total = nmemb * size;
	return heap_alloc(total, 1);
}

void *realloc(void *ptr, size_t size)
{
	void *n;

	if (ptr == NULL)
		return malloc(size);
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	n = malloc(size);
	if (n == NULL)
		return NULL;
	memcpy(n, ptr, size);
	return n;
}

void free(void *ptr)
{
	(void)ptr;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0')
		n++;
	return n;
}

static void emit_char(char **dst, size_t *rem, char c)
{
	if (*rem == 0)
		return;
	**dst = c;
	(*dst)++;
	(*rem)--;
}

static void emit_u64(char **dst, size_t *rem, unsigned long long v, int base,
		     int uppercase)
{
	char tmp[32];
	int i = 0;

	if (v == 0) {
		emit_char(dst, rem, '0');
		return;
	}
	while (v > 0) {
		unsigned digit = (unsigned)(v % (unsigned)base);
		tmp[i++] = (char)(digit < 10 ? '0' + digit :
				   (uppercase ? 'A' : 'a') + digit - 10);
		v /= (unsigned)base;
	}
	while (i > 0)
		emit_char(dst, rem, tmp[--i]);
}

int snprintf(char *str, size_t size, const char *format, ...)
{
	va_list ap;
	size_t rem = size > 0 ? size - 1 : 0;
	char *out = str;
	const char *p;

	if (str == NULL || size == 0)
		return 0;

	va_start(ap, format);
	for (p = format; *p != '\0'; p++) {
		if (*p != '%') {
			emit_char(&out, &rem, *p);
			continue;
		}
		p++;
		switch (*p) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (s == NULL)
				s = "(null)";
			while (*s != '\0')
				emit_char(&out, &rem, *s++);
			break;
		}
		case 'd': {
			long v = va_arg(ap, long);
			unsigned long long u;
			if (v < 0) {
				emit_char(&out, &rem, '-');
				u = (unsigned long long)(-(v + 1)) + 1ULL;
			} else {
				u = (unsigned long long)v;
			}
			emit_u64(&out, &rem, u, 10, 0);
			break;
		}
		case 'u': {
			unsigned int v = va_arg(ap, unsigned int);
			emit_u64(&out, &rem, v, 10, 0);
			break;
		}
		case 'x': {
			unsigned int v = va_arg(ap, unsigned int);
			emit_u64(&out, &rem, v, 16, 0);
			break;
		}
		case '%':
			emit_char(&out, &rem, '%');
			break;
		default:
			emit_char(&out, &rem, '%');
			emit_char(&out, &rem, *p);
			break;
		}
	}
	va_end(ap);

	*out = '\0';
	return (int)(out - str);
}

int open(const char *path, int flags, ...)
{
	(void)path;
	(void)flags;
	errno = ENOENT;
	return -1;
}

ssize_t read(int fd, void *buf, size_t count)
{
	(void)fd;
	(void)buf;
	(void)count;
	return -1;
}

ssize_t write(int fd, const void *buf, size_t count)
{
	(void)fd;
	(void)buf;
	(void)count;
	return -1;
}

int close(int fd)
{
	(void)fd;
	return -1;
}

off_t lseek(int fd, off_t offset, int whence)
{
	(void)fd;
	(void)offset;
	(void)whence;
	return -1;
}
