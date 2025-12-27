// --- GUIサブシステム雛形 ---
void gui_init(void) {
    // TODO: ウィンドウ管理・描画API初期化
}

void gui_draw_window(int x, int y, int w, int h) {
    // TODO: 矩形ウィンドウ描画
}
// kernel_arm/shell_arm.c
// ARM シェル・コマンドインタプリタ雛形
// 2025/12/27 新規作成

#include <stdio.h>
#include <string.h>

#define MAX_CMD_LEN 64
#define MAX_ARGS    8

// コマンド実行関数型
typedef int (*cmd_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    cmd_func_t func;
    const char *desc;
} shell_cmd_t;


// サンプル: GUIウィンドウ描画コマンド
int cmd_guiwin(int argc, char **argv) {
    printf("[GUI] 矩形ウィンドウ描画 (10,10,100,50)\n");
    gui_draw_window(10, 10, 100, 50);
    return 0;
}
// コマンドテーブル（サンプル）
int cmd_help(int argc, char **argv);
int cmd_echo(int argc, char **argv);

shell_cmd_t cmd_table[] = {
    {"guiwin", cmd_guiwin, "GUIウィンドウ描画"},
    {NULL, NULL, NULL}
    {"echo", cmd_echo, "文字列表示"},
    {NULL, NULL, NULL}
};

// helpコマンド
int cmd_help(int argc, char **argv) {
    for (int i = 0; cmd_table[i].name; ++i)
        printf("%s: %s\n", cmd_table[i].name, cmd_table[i].desc);
    return 0;
}

// echoコマンド
int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        printf("%s ", argv[i]);
    printf("\n");
    return 0;
}

// シェル本体
void shell_main(void) {
    char line[MAX_CMD_LEN];
    char *argv[MAX_ARGS];
    int argc;
    printf("ARM Shell> ");
    while (fgets(line, sizeof(line), stdin)) {
        argc = 0;
        char *token = strtok(line, " \t\n");
        while (token && argc < MAX_ARGS) {
            argv[argc++] = token;
            token = strtok(NULL, " \t\n");
        }
        if (argc == 0) {
            printf("ARM Shell> ");
            continue;
        }
        int found = 0;
        for (int i = 0; cmd_table[i].name; ++i) {
            if (strcmp(argv[0], cmd_table[i].name) == 0) {
                cmd_table[i].func(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) printf("Unknown command: %s\n", argv[0]);
        printf("ARM Shell> ");
    }
}
