#include "initramfs.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CPIO_MAGIC "070701"
#define CPIO_TRAILER "TRAILER!!!"
#define CPIO_MAX_FILES 64

struct cpio_newc_header {
	char c_magic[6];
	char c_ino[8];
	char c_mode[8];
	char c_uid[8];
	char c_gid[8];
	char c_nlink[8];
	char c_mtime[8];
	char c_filesize[8];
	char c_devmajor[8];
	char c_devminor[8];
	char c_rdevmajor[8];
	char c_rdevminor[8];
	char c_namesize[8];
	char c_check[8];
};

struct initramfs_entry {
	char        name[128];
	const void *data;
	size_t      size;
};

static struct initramfs_entry entries[CPIO_MAX_FILES];
static int entry_count;

static uint32_t cpio_hex(const char *s, size_t n)
{
	uint32_t v = 0;
	size_t i;

	for (i = 0; i < n; i++) {
		char c = s[i];
		uint32_t d;

		if (c >= '0' && c <= '9')
			d = (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			d = (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			d = (uint32_t)(c - 'A' + 10);
		else
			d = 0;
		v = (v << 4) | d;
	}
	return v;
}

static size_t align4(size_t n)
{
	return (n + 3U) & ~3U;
}

int bfree_initramfs_parse(const void *base, size_t len)
{
	const uint8_t *p = base;
	const uint8_t *end = p + len;

	entry_count = 0;
	while (p + sizeof(struct cpio_newc_header) <= end) {
		const struct cpio_newc_header *hdr =
			(const struct cpio_newc_header *)p;
		uint32_t namesz;
		uint32_t filesz;
		const char *name;
		const void *data;
		size_t hdr_len = sizeof(*hdr);

		if (memcmp(hdr->c_magic, CPIO_MAGIC, 6) != 0)
			return -1;

		namesz = cpio_hex(hdr->c_namesize, 8);
		filesz = cpio_hex(hdr->c_filesize, 8);
		p += hdr_len;
		if (p + namesz > end)
			return -1;

		name = (const char *)p;
		p += align4(namesz);
		if (p + filesz > end)
			return -1;

		data = p;
		p += align4(filesz);

		if (strcmp(name, CPIO_TRAILER) == 0)
			break;

		if (entry_count < CPIO_MAX_FILES) {
			struct initramfs_entry *e = &entries[entry_count++];

			memset(e, 0, sizeof(*e));
			strncpy(e->name, name, sizeof(e->name) - 1);
			e->data = data;
			e->size = filesz;
		}
	}

	return 0;
}

int bfree_initramfs_lookup(const char *name, const void **data, size_t *size)
{
	int i;

	for (i = 0; i < entry_count; i++) {
		if (strcmp(entries[i].name, name) == 0) {
			if (data != NULL)
				*data = entries[i].data;
			if (size != NULL)
				*size = entries[i].size;
			return 0;
		}
	}
	return -1;
}

int bfree_initramfs_file_count(void)
{
	return entry_count;
}
