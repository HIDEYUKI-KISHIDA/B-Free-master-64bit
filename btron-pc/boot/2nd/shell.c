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

#endif
/*

B-Free OS - Simple Shell Implementation

*/

#include "types.h"
#include "lib.h"
#include "console.h"
#include "vfs.h"

#define MAX_CMD_LEN 256
#define MAX_ARGS 10

/* Simple command structure */
typedef struct {
	char *name;
	int (*func)(int argc, char *argv[]);
	char *help;
} command_t;

/* Forward declarations */
static int cmd_help(int argc, char *argv[]);
static int cmd_echo(int argc, char *argv[]);
static int cmd_clear(int argc, char *argv[]);
static int cmd_time(int argc, char *argv[]);
static int cmd_uname(int argc, char *argv[]);
static int cmd_exit(int argc, char *argv[]);
static int cmd_cat(int argc, char *argv[]);
static int cmd_ls(int argc, char *argv[]);
static int cmd_mkdir(int argc, char *argv[]);
static int cmd_rm(int argc, char *argv[]);
static int cmd_cp(int argc, char *argv[]);

/* Command table */
static command_t commands[] = {
	{"help", cmd_help, "Show help information"},
	{"echo", cmd_echo, "Print text"},
	{"clear", cmd_clear, "Clear screen"},
	{"time", cmd_time, "Show current time"},
	{"uname", cmd_uname, "Show system information"},
	{"cat", cmd_cat, "Print file contents"},
	{"ls", cmd_ls, "List directory (simple)"},
	{"mkdir", cmd_mkdir, "Create directory"},
	{"rm", cmd_rm, "Remove file or directory"},
	{"cp", cmd_cp, "Copy file"},
	{"exit", cmd_exit, "Exit shell"},
	/*
	 * cmd_cp - Copy file using VFS
	 */
	static int cmd_cp(int argc, char *argv[])
	{
		if (argc < 3) {
			boot_printf("Usage: cp <src> <dst>\n");
			return 0;
		}
		const char *src = argv[1];
		const char *dst = argv[2];
		struct file *f_src = vfs_open(src, FILE_OPEN_R);
		if (!f_src) {
			boot_printf("cp: cannot open %s\n", src);
			return 0;
		}
		char buf[512];
		int total = 0;
		int r;
		char *filedata = NULL;
		int filesize = 0;
		while ((r = vfs_read(f_src, buf, sizeof(buf))) > 0) {
			filesize += r;
			filedata = (char*)realloc(filedata, filesize);
			if (filedata) memcpy(filedata + filesize - r, buf, r);
		}
		vfs_close(f_src);
		if (!filedata) {
			boot_printf("cp: read error\n");
			return 0;
		}
		// 既存なら上書き、なければ新規
		vfs_unlink(dst);
		if (vfs_unlink(dst) == 0 || vfs_find_inode(dst) == NULL) {
			// 新規作成
			vfs_mkdir(dst); // ディレクトリ名ならmkdir、ファイルならunlink後に書き込み
		}
		struct file *f_dst = vfs_open(dst, FILE_OPEN_W);
		if (!f_dst) {
			boot_printf("cp: cannot open/create %s\n", dst);
			free(filedata);
			return 0;
		}
		vfs_write(f_dst, filedata, filesize);
		vfs_close(f_dst);
		free(filedata);
		boot_printf("cp: copied %s to %s\n", src, dst);
		return 0;
	}
	/*
	 * cmd_mkdir - Create directory using VFS
	 */
	static int cmd_mkdir(int argc, char *argv[])
	{
		if (argc < 2) {
			boot_printf("Usage: mkdir <path>\n");
			return 0;
		}
		if (vfs_mkdir(argv[1]) == 0) {
			boot_printf("mkdir: created %s\n", argv[1]);
		} else {
			boot_printf("mkdir: failed to create %s\n", argv[1]);
		}
		return 0;
	}

	/*
	 * cmd_rm - Remove file or directory using VFS
	 */
	static int cmd_rm(int argc, char *argv[])
	{
		if (argc < 2) {
			boot_printf("Usage: rm <path>\n");
			return 0;
		}
		struct inode *inode = vfs_lookup(argv[1]);
		if (!inode) {
			boot_printf("rm: not found: %s\n", argv[1]);
			return 0;
		}
		int res = 0;
		if (inode->type == INODE_TYPE_DIR) {
			res = vfs_rmdir(argv[1]);
		} else {
			res = vfs_unlink(argv[1]);
		}
		if (res == 0) {
			boot_printf("rm: removed %s\n", argv[1]);
		} else {
			boot_printf("rm: failed to remove %s\n", argv[1]);
		}
		return 0;
	}
	{NULL, NULL, NULL}
};

/*
 * cmd_help - Display help information
 */
static int
cmd_help(int argc, char *argv[])
{
	int i = 0;
	
	boot_printf("Available commands:\n");
	boot_printf("==================\n");
	
	while (commands[i].name != NULL) {
		boot_printf("  %-10s - %s\n", commands[i].name, commands[i].help);
		i++;
	}
	
	return 0;
}

/*
 * cmd_echo - Print text
 */
static int
cmd_echo(int argc, char *argv[])
{
	int i;
	
	for (i = 1; i < argc; i++) {
		boot_printf("%s", argv[i]);
		if (i < argc - 1) {
			boot_printf(" ");
		}
	}
	boot_printf("\n");
	
	return 0;
}

/*
 * cmd_clear - Clear screen
 */
static int
cmd_clear(int argc, char *argv[])
{
	int i;
	
	/* Print newlines to clear screen */
	for (i = 0; i < 25; i++) {
		boot_printf("\n");
	}
	
	return 0;
}

/*
 * cmd_time - Display current time
 */
static int
cmd_time(int argc, char *argv[])
{
	// BIOS RTCから時刻取得（雛形）
	unsigned char hour = 0, min = 0, sec = 0;
	// CMOSポートから時刻取得（例: x86）
	outb(0x70, 0x04); hour = inb(0x71);
	outb(0x70, 0x02); min  = inb(0x71);
	outb(0x70, 0x00); sec  = inb(0x71);
	// BCD→10進変換
	hour = ((hour >> 4) * 10) + (hour & 0x0F);
	min  = ((min  >> 4) * 10) + (min  & 0x0F);
	sec  = ((sec  >> 4) * 10) + (sec  & 0x0F);
	boot_printf("Time: %02d:%02d:%02d\n", hour, min, sec);
	boot_printf("(Real-time clock from BIOS CMOS)\n");
	return 0;
}

/*
 * cmd_uname - Display system information
 */
static int
cmd_uname(int argc, char *argv[])
{
	boot_printf("System: B-Free OS\n");
	boot_printf("Version: 64-bit Enhanced\n");
	boot_printf("Architecture: x86-64\n");
	
	return 0;
}

/*
 * cmd_exit - Exit shell
 */
static int
cmd_exit(int argc, char *argv[])
{
	boot_printf("Shutting down...\n");
	return 1;  /* Signal to exit shell */
}

/*
 * Parse command line into arguments
 */
static int
parse_cmd(char *line, char *argv[])
{
	int argc = 0;
	char *p = line;
	int in_arg = 0;
	
	while (*p != '\0' && argc < MAX_ARGS) {
		if (*p == ' ' || *p == '\t') {
			if (in_arg) {
				*p = '\0';
				in_arg = 0;
			}
		} else {
			if (!in_arg) {
				argv[argc++] = p;
				in_arg = 1;
			}
		}
		p++;
	}
	
	argv[argc] = NULL;
	return argc;
}

/*
 * Execute command
 */
static int
execute_cmd(int argc, char *argv[])
{
	int i = 0;
	
	if (argc == 0) {
		return 0;
	}
	
	while (commands[i].name != NULL) {
		if (strcmp(argv[0], commands[i].name) == 0) {
			return commands[i].func(argc, argv);
		}
		i++;
	}
	
	boot_printf("Unknown command: %s\n", argv[0]);
	boot_printf("Type 'help' for available commands\n");
	
	return 0;
}

/*
 * Simple shell main loop
 */
void
shell_main(void)
{
	char cmd_line[MAX_CMD_LEN];
	char *argv[MAX_ARGS];
	int argc;
	int running = 1;
	int pos = 0;
	char c;

	boot_printf("\n");
	boot_printf(" ____   ____   ______           __         ____   ______  \n");
	boot_printf("|  _ \\ / __ \\ / ____/___ ______/ /_  ___  / __ \\ / ____/  \n");
	boot_printf("| |_) | |  | | |   / __ `/ ___/ __ \\/ _ \\/ / / // /       \n");
	boot_printf("|  _ <| |  | | |__/ /_/ / /__/ / / /  __/ /_/ // /___     \n");
	boot_printf("|_| \\_\\____/ \\____\\__,_/\\___/_/ /_/\\___/\\____(_)____/ 64 \n");
	boot_printf("\n");
	boot_printf("=================================\n");
	boot_printf("Welcome to B-Free OS 64-bit Shell!\n");
	boot_printf("(世界初？の64bit B-Free体験へようこそ)\n");
	boot_printf("Type 'help' for available commands\n");
	boot_printf("=================================\n");
	boot_printf("\n");

	while (running) {
		/* Print prompt */
		boot_printf("bfree64(気分転換モード)> ");

		/* Read command line */
		pos = 0;
		while (pos < MAX_CMD_LEN - 1) {
			c = console_getchar();
			
			if (c == '\r' || c == '\n') {
				cmd_line[pos] = '\0';
				boot_printf("\n");
				break;
			} else if (c == '\b' || c == 0x7F) {
				/* Backspace */
				if (pos > 0) {
					pos--;
					boot_printf("\b \b");
				}
			} else if (c >= 32 && c < 127) {
				/* Printable character */
				cmd_line[pos++] = c;
				boot_printf("%c", c);
			}
		}
		
		if (pos > 0) {
			/* Parse and execute command */
			argc = parse_cmd(cmd_line, argv);
			if (execute_cmd(argc, argv) != 0) {
				running = 0;
			}
		}
	}
	
	boot_printf("\nBye!\n");
}

/*
 * String comparison
 */
int
strcmp(const char *s1, const char *s2)
{
	while (*s1 && *s2) {
		if (*s1 != *s2) {
			return *s1 - *s2;
		}
		s1++;
		s2++;
	}
	
	return *s1 - *s2;
}

/*
 * cmd_cat - Print file contents using VFS
 */
static int
cmd_cat(int argc, char *argv[])
{
	if (argc < 2) {
		boot_printf("Usage: cat <path>\n");
		return 0;
	}

	const char *path = argv[1];
	struct inode *inode = vfs_lookup(path);
	if (!inode) {
		boot_printf("cat: not found: %s\n", path);
		return 0;
	}

	if (inode->type == INODE_TYPE_DIR) {
		boot_printf("cat: %s: Is a directory\n", path);
		if (root_fs && root_fs->ops && root_fs->ops->put_inode) root_fs->ops->put_inode(inode);
		return 0;
	}

	struct file *f = vfs_open(path, FILE_OPEN_R);
	if (!f) {
		boot_printf("cat: cannot open %s\n", path);
		if (inode && root_fs && root_fs->ops && root_fs->ops->put_inode) root_fs->ops->put_inode(inode);
		return 0;
	}

	char buf[512];
	int r;
	while ((r = vfs_read(f, buf, sizeof(buf))) > 0) {
		for (int i = 0; i < r; i++) putchar((int)buf[i]);
	}

	vfs_close(f);
	return 0;
}

/*
 * cmd_ls - very basic listing: lists names by attempting to read directory entries via vfs_readdir
 */
static int
cmd_ls(int argc, char *argv[])
{
	const char *path = "/";
	if (argc >= 2) path = argv[1];

	struct inode *inode = vfs_lookup(path);
	if (!inode) {
		boot_printf("ls: cannot access %s\n", path);
		return 0;
	}

	if (inode->type != INODE_TYPE_DIR) {
		boot_printf("%s\n", path);
		if (root_fs && root_fs->ops && root_fs->ops->put_inode) root_fs->ops->put_inode(inode);
		return 0;
	}

	struct dirent d;
	int idx = 0;
	while (vfs_readdir(inode, &d, idx) == 0) {
		boot_printf("%s\n", d.name);
		idx++;
	}

	if (root_fs && root_fs->ops && root_fs->ops->put_inode) root_fs->ops->put_inode(inode);
	return 0;
}
