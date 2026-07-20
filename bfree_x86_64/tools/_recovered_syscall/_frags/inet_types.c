#define BFREE_SYSRET_COOP_SWITCH ((long)-4092)
#define BFREE_LINUX_AF_UNIX 1
#define BFREE_LINUX_AF_INET 2
#define BFREE_UNIX_SLOTS 8
#define BFREE_UNIX_FD_BASE 0x3900
#define BFREE_INET_SLOTS 8
#define BFREE_INET_FD_BASE 0x3A00
#define BFREE_INADDR_LOOPBACK 0x7f000001U /* 127.0.0.1 host order */
#define BFREE_INADDR_ANY 0U

typedef struct {
    int used;
    int listening;
    int connected;
    int accept_rd;
    int pipe_magic;
    char path[96];
} bfree_unix_sock_t;

typedef struct {
    int used;
    int listening;
    int connected;
    int bound;
    uint32_t addr; /* host order */
    uint16_t port; /* host order */
    int accept_rd;
    int pipe_magic;
} bfree_inet_sock_t;

static bfree_unix_sock_t g_unix_socks[BFREE_UNIX_SLOTS];
static bfree_inet_sock_t g_inet_socks[BFREE_INET_SLOTS];
static int g_fd_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static uint8_t g_fd_cloexec_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static uint8_t g_fd_cloexec_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static uint64_t g_coop_child_rcx, g_coop_child_r11, g_coop_child_rsp;
static uint64_t g_coop_child_rbx, g_coop_child_rbp, g_coop_child_r12;
static uint64_t g_coop_child_r13, g_coop_child_r14, g_coop_child_r15, g_coop_child_rdx;