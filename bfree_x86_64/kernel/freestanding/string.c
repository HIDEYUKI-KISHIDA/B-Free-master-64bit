void *memset(void *s, int c, unsigned long n)
{
	unsigned char *p = s;
	unsigned long i;

	for (i = 0; i < n; i++)
		p[i] = (unsigned char)c;
	return s;
}

void *memcpy(void *dst, const void *src, unsigned long n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	unsigned long i;

	for (i = 0; i < n; i++)
		d[i] = s[i];
	return dst;
}

int memcmp(const void *a, const void *b, unsigned long n)
{
	const unsigned char *p = a;
	const unsigned char *q = b;
	unsigned long i;

	for (i = 0; i < n; i++) {
		if (p[i] != q[i])
			return (int)p[i] - (int)q[i];
	}
	return 0;
}

int strcmp(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

char *strncpy(char *dst, const char *src, unsigned long n)
{
	unsigned long i;

	for (i = 0; i < n && src[i] != '\0'; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = '\0';
	return dst;
}
