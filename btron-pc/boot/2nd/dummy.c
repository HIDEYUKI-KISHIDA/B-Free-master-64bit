int __main(void) __attribute__((used));
int __main(void) { return 0; }
// アンダースコア付きシンボルのエイリアス
void boot_printf(void) {}
void fault(void) {
	printf("FAULT!\n");
	while (1) asm volatile ("hlt");
}
void interrupt(void) {}
// 割り込みハンドラはinterrupt.cで定義するため、ここではextern宣言のみ
extern void intr_keyboard(void);
void _boot_printf(void) __attribute__((alias("boot_printf")));
void _fault(void) __attribute__((alias("fault")));
void _interrupt(void) __attribute__((alias("interrupt")));
void _intr_keyboard(void) __attribute__((alias("intr_keyboard")));
// ダミー関数群（未定義シンボル対策）
#include <stdio.h>

// void boot_printf(const char *fmt, ...) {} // 削除: void(void)型のみ残す
void bcopy(const void *src, void *dst, int len) {}
void bzero(void *dst, int len) {}
int inb(int port) {
#if defined(__GNUC__)
	unsigned char ret;
	__asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
#else
	return 0;
#endif
}
void outb(int port, int val) {
#if defined(__GNUC__)
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
#endif
}
void outw(int port, int val) {
#if defined(__GNUC__)
	__asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
#endif
}
extern void lock(void);
extern void unlock(void);
extern int vga_text(void);
void stop_motor(int d) {}
void set_idt(int a, int b, int c, int d, int e) {}
extern void reset_intr_mask(int n);
void wait_int(int *x) {
	if (!x) return;
	while (*x == 0) {
		asm volatile ("hlt"); // 割り込み待機
	}
	*x = 0; // フラグクリア（再利用時）
}
void setup_dma(void *a, int b, int c, int d) { /* ダミー */ }
void busywait(int x) {
	volatile int i;
	for (i = 0; i < x * 1000; i++) {
		asm volatile ("nop");
	}
}
void on_motor(void) {}
void write_vram(void) {}
void set_cursor_pos(int x, int y) {}
void scroll_up(void) {}
void write_cr(void) {}
void write_tab(void) {}
void get_coursor(int *x, int *y) { if(x) *x=0; if(y) *y=0; }
void set_coursor(int x, int y) {}
void ignore_handler(void) {}
void int33_handler(void) {}
void int38_handler(void) {}
extern void intr_fd(void);
extern void intr_ide(void);
void fatal(void) {
	printf("FATAL ERROR!\n");
	while (1) asm volatile ("hlt");
}
int atoi(const char *s) { return 0; }
extern unsigned long strcmp(const unsigned char *a, const unsigned char *b);
extern unsigned char *strcpy(unsigned char *a, const unsigned char *b);
extern unsigned char *strcat(unsigned char *a, const unsigned char *b);
extern unsigned long strlen(const unsigned char *a);
extern unsigned long strncmp(const unsigned char *a, const unsigned char *b, int n);
extern unsigned char *strncpy(unsigned char *a, const unsigned char *b, int n);
void *fs_table = 0;
void *rootdirp = 0;
void *fd_read_with_partition = 0;
void *ide_read_block_1k = 0;
void *fd_read = 0;
void *fd_recalibrate = 0;
void *fd_reset = 0;
void *fd_seek = 0;
void *f_sfs_cat = 0;
void *f_sfs_dir = 0;
void *f_sfs_mount = 0;
void *f_sfs_mountroot = 0;
void *get_page_entry = 0;
void *ide_boot = 0;
void *ide_id = 0;
void *ide_init = 0;
void *ide_read_dump = 0;
void *k101us = 0;
void *k106jp = 0;
void *multi_boot = 0;
void *read_a_out = 0;
void *read_keyboard = 0;
void *read_multi_module = 0;
void *sfs_lookup_file = 0;
void *sfs_read_buf = 0;
extern int sfs_read_file(int fd, struct sfs_superblock *sb, struct sfs_inode *ip, int a, int b, void *buf);
void *status_memory = 0;
int base_mem = 0;
int ext_mem = 0;
int real_mem = 0;
void *last_addr = 0;
void abort(void) {
    printf("ABORT!\n");
    while (1) asm volatile ("hlt");
}
