                                                puts64("sigsend  - send signal (dummy)\n");
                                                puts64("sigraise - raise signal (dummy)\n");
                                                puts64("sighandler - set signal handler (dummy)\n");
                                            } else if (!strncmp(cmd, "sigsend ", 8)) {
                                                extern int proc_sigsend64(int, int);
                                                int pid=0, sig=0;
                                                sscanf(cmd+8, "%d %d", &pid, &sig);
                                                proc_sigsend64(pid, sig);
                                                puts64("sigsend: sent (dummy)\n");
                                            } else if (!strncmp(cmd, "sigraise ", 9)) {
                                                extern int proc_sigraise64(int);
                                                int sig=0;
                                                sscanf(cmd+9, "%d", &sig);
                                                proc_sigraise64(sig);
                                                puts64("sigraise: raised (dummy)\n");
                                            } else if (!strncmp(cmd, "sighandler ", 11)) {
                                                extern int proc_sighandler64(int, void*);
                                                int sig=0;
                                                sscanf(cmd+11, "%d", &sig);
                                                proc_sighandler64(sig, 0);
                                                puts64("sighandler: set (dummy)\n");
                                    puts64("getpid   - show current pid (dummy)\n");
                                    puts64("priority - set process priority (dummy)\n");
                                    puts64("sleep    - sleep process (dummy)\n");
                                } else if (!strcmp(cmd, "getpid")) {
                                    extern int proc_getpid64(void);
                                    char buf[32];
                                    snprintf(buf, sizeof(buf), "pid=%d\n", proc_getpid64());
                                    puts64(buf);
                                } else if (!strncmp(cmd, "priority ", 9)) {
                                    extern int proc_setpriority64(int);
                                    int prio = 0;
                                    sscanf(cmd+9, "%d", &prio);
                                    proc_setpriority64(prio);
                                    puts64("priority: set (dummy)\n");
                                } else if (!strncmp(cmd, "sleep ", 6)) {
                                    extern int proc_sleep64(int);
                                    int t = 0;
                                    sscanf(cmd+6, "%d", &t);
                                    proc_sleep64(t);
                                    puts64("sleep: done (dummy)\n");
                        puts64("mkdir    - make directory (dummy)\n");
                        puts64("rmdir    - remove directory (dummy)\n");
                        puts64("pwd      - print working dir (dummy)\n");
                        puts64("stat     - show file info (dummy)\n");
                    } else if (!strncmp(cmd, "mkdir ", 6)) {
                        extern int fs_mkdir64(const char*);
                        fs_mkdir64(cmd+6);
                    } else if (!strncmp(cmd, "rmdir ", 6)) {
                        extern int fs_rmdir64(const char*);
                        fs_rmdir64(cmd+6);
                    } else if (!strcmp(cmd, "pwd")) {
                        extern int fs_pwd64(void);
                        fs_pwd64();
                    } else if (!strncmp(cmd, "stat ", 5)) {
                        extern int fs_stat64(const char*);
                        fs_stat64(cmd+5);
            puts64("mv       - rename file (dummy)\n");
            puts64("cp       - copy file (dummy)\n");
            puts64("echo ... > file - write file (dummy)\n");
        } else if (!strncmp(cmd, "mv ", 3)) {
            extern int fs_mv64(const char*, const char*);
            // mv old new
            char old[32], newf[32];
            if (sscanf(cmd+3, "%31s %31s", old, newf) == 2) {
                fs_mv64(old, newf);
            } else {
                puts64("mv: usage: mv old new\n");
            }
        } else if (!strncmp(cmd, "cp ", 3)) {
            extern int fs_cp64(const char*, const char*);
            char src[32], dst[32];
            if (sscanf(cmd+3, "%31s %31s", src, dst) == 2) {
                fs_cp64(src, dst);
            } else {
                puts64("cp: usage: cp src dst\n");
            }
        } else if (!strncmp(cmd, "echo ", 5) && strstr(cmd, ">")) {
            extern int fs_echo_write64(const char*, const char*);
            char text[64], fname[32];
            const char *gt = strstr(cmd+5, ">\");
            if (gt) {
                int tlen = gt - (cmd+5);
                while (tlen > 0 && (cmd[5+tlen-1]==' ')) tlen--; // trim
                strncpy(text, cmd+5, tlen); text[tlen]=0;
                sscanf(gt+1, "%31s", fname);
                fs_echo_write64(fname, text);
            } else {
                puts64("echo: usage: echo ... > file\n");
            }
// shell64.c - 64ビット用シェル雛形
#include "../include64/types.h"
#include <stddef.h>


#define MAX_CMD_LEN 128

// --- 追加: 環境変数・リダイレクト・パイプ雰囲気のダミーAPI ---
#define MAX_ENV64 16
static char env_names[MAX_ENV64][32];
static char env_values[MAX_ENV64][64];
static int env_count = 0;

int setenv64(const char *name, const char *value) {
    for (int i = 0; i < env_count; i++) {
        if (!strcmp(env_names[i], name)) {
            strncpy(env_values[i], value, 63); env_values[i][63]=0;
            return 0;
        }
    }
    if (env_count < MAX_ENV64) {
        strncpy(env_names[env_count], name, 31); env_names[env_count][31]=0;
        strncpy(env_values[env_count], value, 63); env_values[env_count][63]=0;
        env_count++;
        return 0;
    }
    return -1;
}

const char* getenv64(const char *name) {
    for (int i = 0; i < env_count; i++) {
        if (!strcmp(env_names[i], name)) return env_values[i];
    }
    return NULL;
}

// コマンドリダイレクト・パイプ雰囲気（ダミー）
int shell_redirect64(const char *cmd, const char *file, int mode) {
    // mode: 0=stdout, 1=stdin
    // 本来はファイル記述子操作
    return 0;
}
int shell_pipe64(const char *cmd1, const char *cmd2) {
    // 本来はパイプ生成・プロセス分岐
    return 0;
}
// --- ここまで追加 ---

// 外部出力関数
extern void console_putc64(char c);
static void puts64(const char *s) { while (*s) console_putc64(*s++); }

// 外部入力関数
extern char console_getc64(void);

void shell64_main(void) {
    char cmd[MAX_CMD_LEN];
    int pos;
    #define MAX_HISTORY 8
    static char history[MAX_HISTORY][MAX_CMD_LEN];
    static int hist_count = 0, hist_pos = 0;
    puts64("\nB-Free 64bit Shell (mini)\nType 'help' for commands.\n\n");
    while (1) {
        puts64("bfree64> ");
        pos = 0;
        int hist_nav = hist_count;
        // 1行入力: バックスペース・Enter・↑↓履歴対応
        while (1) {
            char c = console_getc64();
            if (c == '\r' || c == '\n') {
                console_putc64('\n');
                break;
            } else if (c == 0x08 || c == 0x7F) { // バックスペース
                if (pos > 0) {
                    pos--;
                    puts64("\b \b");
                }
            } else if (c == 0x1B) { // 簡易履歴: ↑=0x1B[A, ↓=0x1B[B
                char seq1 = console_getc64();
                char seq2 = console_getc64();
                if (seq1 == '[' && (seq2 == 'A' || seq2 == 'B')) {
                    if (seq2 == 'A' && hist_nav > 0) hist_nav--;
                    if (seq2 == 'B' && hist_nav < hist_count) hist_nav++;
                    if (hist_nav >= 0 && hist_nav < hist_count) {
                        while (pos > 0) { puts64("\b \b"); pos--; }
                        strncpy(cmd, history[hist_nav], MAX_CMD_LEN-1); cmd[MAX_CMD_LEN-1]=0;
                        puts64(cmd); pos = strlen(cmd);
                    }
                }
            } else if (c >= 32 && c < 127 && pos < MAX_CMD_LEN-1) {
                cmd[pos++] = c;
                console_putc64(c);
            }
        }
        cmd[pos] = 0;
        // 履歴保存
        if (pos > 0 && (hist_count == 0 || strcmp(cmd, history[hist_count-1]) != 0)) {
            if (hist_count < MAX_HISTORY) {
                strncpy(history[hist_count++], cmd, MAX_CMD_LEN-1);
            } else {
                for (int i = 1; i < MAX_HISTORY; i++) strncpy(history[i-1], history[i], MAX_CMD_LEN-1);
                strncpy(history[MAX_HISTORY-1], cmd, MAX_CMD_LEN-1);
            }
        }

        // コマンド判定
        if (pos == 0 || !cmd[0] || !strcmp(cmd, "help")) {
            puts64("help     - show this help\n");
            puts64("echo     - print text\n");
            puts64("exit     - halt shell\n");
            puts64("version  - show OS version\n");
            puts64("meminfo  - show memory info\n");
            puts64("uptime   - show uptime\n");
            puts64("ps       - show process table\n");
            puts64("ls       - list files (dummy)\n");
            puts64("cat      - show file (dummy)\n");
            puts64("fork     - fork process (dummy)\n");
            puts64("exec     - exec process (dummy)\n");
            puts64("wait     - wait process (dummy)\n");
            puts64("kill     - kill process (dummy)\n");
            puts64("top      - show system summary\n");
            puts64("clear    - clear screen\n");
            puts64("reboot   - reboot system (dummy)\n");
            puts64("shutdown - halt system\n");
            puts64("date     - show date/time (dummy)\n");
            puts64("\n[File ops]\n");
            puts64("ls       - list files\n");
            puts64("cat      - show file\n");
            puts64("touch    - create file\n");
            puts64("rm       - remove file\n");
            puts64("\n[Process ops]\n");
            puts64("ps       - show process table\n");
            puts64("fork     - fork process\n");
            puts64("exec     - exec process\n");
            puts64("wait     - wait process\n");
            puts64("kill     - kill process\n");
        } else if (!strcmp(cmd, "exit")) {
            puts64("Bye!\n");
            break;
        } else if (!strncmp(cmd, "echo ", 5)) {
            puts64(cmd+5);
            puts64("\n");
        } else if (!strcmp(cmd, "version")) {
            puts64("B-Free 64bit OS v0.1\n");
        } else if (!strcmp(cmd, "meminfo")) {
            extern int get_proc_count64(void);
            int used = get_proc_count64() * 2; // 1プロセス2MB消費と仮定
            int free = 64 - used;
            char buf[64];
            snprintf(buf, sizeof(buf), "Total: 64MB, Free: %dMB\n", free > 0 ? free : 0);
            puts64(buf);
        } else if (!strcmp(cmd, "uptime")) {
            extern u64 get_timer_ticks64(void);
            u64 ticks = get_timer_ticks64();
            // 仮に1tick=10ms(100Hz)とする
            u64 sec = ticks / 100;
            u64 min = sec / 60;
            u64 hour = min / 60;
            char buf[64];
            snprintf(buf, sizeof(buf), "Uptime: %llu:%02llu:%02llu\n", hour, min%60, sec%60);
            puts64(buf);
        } else if (!strcmp(cmd, "shutdown")) {
            puts64("System halted.\n");
            while (1) { __asm__ __volatile__("hlt"); }
        } else if (!strcmp(cmd, "date")) {
            puts64("2025-12-27 12:34:56\n");
        } else if (!strcmp(cmd, "reboot")) {
            puts64("Rebooting...\n");
            extern void kernel_init(void);
            kernel_init();
            puts64("System re-initialized.\n");
        } else if (!strcmp(cmd, "ps")) {
            extern void show_proc_table64(void);
            show_proc_table64();
        } else if (!strcmp(cmd, "ls")) {
            extern int fs_ls64(void);
            fs_ls64();
        } else if (!strcmp(cmd, "top")) {
            // top: プロセス・メモリ・uptimeまとめて表示
            extern void show_proc_table64(void);
            extern int get_proc_count64(void);
            extern u64 get_timer_ticks64(void);
            int used = get_proc_count64() * 2;
            int free = 64 - used;
            u64 ticks = get_timer_ticks64();
            u64 sec = ticks / 100;
            u64 min = sec / 60;
            u64 hour = min / 60;
            char buf[128];
            puts64("--- System Summary ---\n");
            snprintf(buf, sizeof(buf), "Mem: Total 64MB, Free %dMB\n", free > 0 ? free : 0);
            puts64(buf);
            snprintf(buf, sizeof(buf), "Uptime: %llu:%02llu:%02llu\n", hour, min%60, sec%60);
            puts64(buf);
            show_proc_table64();
        } else if (!strcmp(cmd, "clear")) {
            extern void console_clear64(void);
            console_clear64();
        } else if (!strncmp(cmd, "touch ", 6)) {
            extern int fs_touch64(const char*);
            fs_touch64(cmd+6);
        } else if (!strncmp(cmd, "rm ", 3)) {
            extern int fs_rm64(const char*);
            fs_rm64(cmd+3);
            extern int fs_cat64(const char*);
            fs_cat64(cmd+4);
        } else if (!strcmp(cmd, "fork")) {
            extern int fork_proc64(void);
            int pid = fork_proc64();
            if (pid > 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "fork: new process created (pid=%d)\n", pid);
                puts64(buf);
            } else {
                puts64("fork: no free slot\n");
            }
        } else if (!strncmp(cmd, "exec ", 5)) {
            extern int exec_user_program64(const char*);
            if (exec_user_program64(cmd+5) == 0) {
                puts64("exec: user program started\n");
            } else {
                puts64("exec: failed to start user program\n");
            }
        } else if (!strcmp(cmd, "wait")) {
            puts64("wait: child process finished (dummy)\n");
        } else if (!strcmp(cmd, "kill")) {
            extern int kill_proc64(void);
            int pid = kill_proc64();
            if (pid > 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "kill: process killed (pid=%d)\n", pid);
                puts64(buf);
            } else {
                puts64("kill: no process to kill\n");
            }
        } else {
            puts64("Unknown command. Type 'help'.\n");
        }
    }
}
