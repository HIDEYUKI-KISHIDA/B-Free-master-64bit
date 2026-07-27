#ifndef BFREE_FREESTANDING_UNISTD_H
#define BFREE_FREESTANDING_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

int open(const char *path, int flags, ...);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);

#endif
