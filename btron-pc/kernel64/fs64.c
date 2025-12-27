// mkdir: ディレクトリ作成（ダミー）
int fs_mkdir64(const char *name) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64("mkdir: directory created (dummy)\n");
    return 0;
}

// rmdir: ディレクトリ削除（ダミー）
int fs_rmdir64(const char *name) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64("rmdir: directory removed (dummy)\n");
    return 0;
}

// pwd: カレントディレクトリ表示（ダミー）
int fs_pwd64(void) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64("/ (dummy root)\n");
    return 0;
}

// stat: ファイル情報表示（ダミー）
int fs_stat64(const char *filename) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64("stat: size=42, type=file, mtime=2025-12-27\n");
    return 0;
}
// mv: ファイル名変更
int fs_mv64(const char *old, const char *newf) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    for (int i = 0; i < filecount; i++) {
        if (!filelist[i][0]) continue;
        const char *p = filelist[i];
        const char *q = old;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*p && (!*q || *q=='\n' || *q=='\r')) {
            int n = 0; while (newf[n] && n < 31) { filelist[i][n] = newf[n]; n++; } filelist[i][n] = 0;
            puts64("mv: file renamed\n");
            return 0;
        }
    }
    puts64("mv: file not found\n");
    return -1;
}

// cp: ファイルコピー
int fs_cp64(const char *src, const char *dst) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    int srcidx = -1;
    for (int i = 0; i < filecount; i++) {
        if (!filelist[i][0]) continue;
        const char *p = filelist[i];
        const char *q = src;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*p && (!*q || *q=='\n' || *q=='\r')) srcidx = i;
    }
    if (srcidx == -1) { puts64("cp: source not found\n"); return -1; }
    if (filecount < MAX_FILES64) {
        int idx = filecount;
        for (int i = 0; i < MAX_FILES64; i++) {
            if (!filelist[i][0]) { idx = i; break; }
        }
        int n = 0; while (dst[n] && n < 31) { filelist[idx][n] = dst[n]; n++; } filelist[idx][n] = 0;
        strncpy(filedata[idx], filedata[srcidx], sizeof(filedata[idx])-1);
        filedata[idx][sizeof(filedata[idx])-1]=0;
        if (idx == filecount) filecount++;
        puts64("cp: file copied\n");
    } else {
        puts64("cp: file table full\n");
    }
    return 0;
}

// echo ... > file: 内容書き込み
// --- 追加: ファイル配列管理・open/close/read/write等のダミーAPI ---

#define MAX_FILES64 16
static char filelist[MAX_FILES64][32];
static char filedata[MAX_FILES64][256];
static int filecount = 0;

int fs_open64(const char *name) {
    for (int i = 0; i < MAX_FILES64; i++) {
        if (!strcmp(filelist[i], name)) return i;
    }
    return -1;
}

int fs_close64(int fd) {
    // ダミー: 何もしない
    return 0;
}

int fs_read64(int fd, char *buf, int len) {
    if (fd < 0 || fd >= MAX_FILES64) return -1;
    strncpy(buf, filedata[fd], len);
    return strlen(filedata[fd]);
}

int fs_write64(int fd, const char *buf, int len) {
    if (fd < 0 || fd >= MAX_FILES64) return -1;
    strncpy(filedata[fd], buf, len);
    filedata[fd][len < 255 ? len : 255] = 0;
    return len;
}

// --- ここまで追加 ---

// --- 本物のファイルシステム雰囲気API ---
#define FS_TYPE_FILE64 1
#define FS_TYPE_DIR64  2

typedef struct inode64 {
    int type;
    char name[32];
    int size;
    int parent;
    int first_block;
} inode64_t;

#define MAX_INODE64 32
static inode64_t inode_table64[MAX_INODE64];
static int inode_count64 = 0;

typedef struct block64 {
    char data[256];
    int next;
} block64_t;

#define MAX_BLOCK64 64
static block64_t block_table64[MAX_BLOCK64];
static int block_count64 = 0;

// inode作成
int fs_create_inode64(const char *name, int type, int parent) {
    if (inode_count64 < MAX_INODE64) {
        inode64_t *ino = &inode_table64[inode_count64];
        ino->type = type;
        strncpy(ino->name, name, 31); ino->name[31]=0;
        ino->size = 0;
        ino->parent = parent;
        ino->first_block = -1;
        return inode_count64++;
    }
    return -1;
}

// inode検索
int fs_find_inode64(const char *name, int parent) {
    for (int i = 0; i < inode_count64; i++) {
        if (!strcmp(inode_table64[i].name, name) && inode_table64[i].parent == parent) return i;
    }
    return -1;
}

// ディレクトリ作成
int fs_mkdir_real64(const char *name, int parent) {
    return fs_create_inode64(name, FS_TYPE_DIR64, parent);
}

// ファイル作成
int fs_create_file64(const char *name, int parent) {
    return fs_create_inode64(name, FS_TYPE_FILE64, parent);
}

// ディレクトリエントリ列挙
int fs_list_dir64(int parent) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    for (int i = 0; i < inode_count64; i++) {
        if (inode_table64[i].parent == parent) {
            puts64(inode_table64[i].name);
            puts64(inode_table64[i].type == FS_TYPE_DIR64 ? "/\n" : "\n");
        }
    }
    return 0;
}

// --- ここまで追加 ---
int fs_echo_write64(const char *fname, const char *text) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    for (int i = 0; i < filecount; i++) {
        if (!filelist[i][0]) continue;
        const char *p = filelist[i];
        const char *q = fname;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*p && (!*q || *q=='\n' || *q=='\r')) {
            snprintf(filedata[i], sizeof(filedata[i]), "%s\n", text);
            puts64("echo: file written\n");
            return 0;
        }
    }
    puts64("echo: file not found\n");
    return -1;
}
// fs64.c - 64ビット用 ファイルシステム雛形
#include "../include64/types.h"

void fs_init64(void) {
    // TODO: ファイルシステム初期化
}

// ダミーファイル一覧（最大10ファイル）
#define MAX_FILES64 10
static char filelist[MAX_FILES64][32] = {"file1.txt", "file2.txt", "README.md"};
static char filedata[MAX_FILES64][128] = {
    "file1.txt: Hello, this is file1.\n",
    "file2.txt: This is file2 contents.\n",
    "# README.md\nThis is a dummy README.\n"
};
static int filecount = 3;

int fs_ls64(void) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    for (int i = 0; i < filecount; i++) {
        if (filelist[i][0]) {
            puts64(filelist[i]);
            puts64("\n");
        }
    }
    return 0;
}

int fs_cat64(const char *filename) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    for (int i = 0; i < filecount; i++) {
        if (!filelist[i][0]) continue;
        const char *p = filelist[i];
        const char *q = filename;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*p && (!*q || *q=='\n' || *q=='\r')) {
            puts64(filedata[i]);
            return 0;
        }
    }
    puts64("cat: file not found\n");
    return -1;
}

// touch: ファイル追加
int fs_touch64(const char *filename) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    // 既存なら何もしない
    for (int i = 0; i < filecount; i++) {
        if (!filelist[i][0]) continue;
        const char *p = filelist[i];
        const char *q = filename;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*p && (!*q || *q=='\n' || *q=='\r')) {
            puts64("touch: already exists\n");
            return 0;
        }
    }
    if (filecount < MAX_FILES64) {
        int idx = filecount;
        for (int i = 0; i < MAX_FILES64; i++) {
            if (!filelist[i][0]) { idx = i; break; }
        }
        int n = 0; while (filename[n] && n < 31) { filelist[idx][n] = filename[n]; n++; } filelist[idx][n] = 0;
        snprintf(filedata[idx], sizeof(filedata[idx]), "%s: (empty file)\n", filelist[idx]);
        if (idx == filecount) filecount++;
        puts64("touch: file created\n");
    } else {
        puts64("touch: file table full\n");
    }
    return 0;
}

// rm: ファイル削除
int fs_rm64(const char *filename) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    for (int i = 0; i < filecount; i++) {
        if (!filelist[i][0]) continue;
        const char *p = filelist[i];
        const char *q = filename;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*p && (!*q || *q=='\n' || *q=='\r')) {
            filelist[i][0] = 0;
            filedata[i][0] = 0;
            puts64("rm: file removed\n");
            return 0;
        }
    }
    puts64("rm: file not found\n");
    return -1;
}
