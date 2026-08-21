/*
 * musl src/internal/libc.h — guest desktop compile helper.
 * Only the fields guest_link_compat.cpp touches are required at link time;
 * layout must match the musl libc.a linked into desktop.elf.
 */
#ifndef MUSL_LIBC_LIBC_H
#define MUSL_LIBC_LIBC_H

#include <stddef.h>
#include <stdio.h>
#include <limits.h>

struct __locale_map;

struct __locale_struct {
    const struct __locale_map *cat[6];
};

struct tls_module {
    struct tls_module *next;
    void *image;
    size_t len, size, align, offset;
};

struct __libc {
    char can_do_threads;
    char threaded;
    char secure;
    volatile signed char need_locks;
    int threads_minus_1;
    size_t *auxv;
    struct tls_module *tls_head;
    size_t tls_size, tls_align, tls_cnt;
    size_t page_size;
    struct __locale_struct global_locale;
};

#ifndef PAGE_SIZE
#define PAGE_SIZE __libc.page_size
#endif

extern struct __libc __libc;
#define libc __libc

#endif /* MUSL_LIBC_LIBC_H */
