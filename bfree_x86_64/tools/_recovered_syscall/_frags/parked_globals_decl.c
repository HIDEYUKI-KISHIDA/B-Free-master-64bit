/* AS-copy: kernel brk/cwd are global. Dual-park across coop switches so each
 * side resumes its own markers. On child exit restore parent park (not
 * fork-enter) so post-fork parent brk is not rewound. First child resume uses
 * fork-enter (birth heap) until the child gets its own park slot. */
static int g_guest_parent_parked_heap_valid;
static uint64_t g_guest_parent_parked_heap_next;
static uint64_t g_guest_parent_parked_brk;
static char g_guest_parent_parked_cwd[256];
static int g_guest_child_parked_heap_valid;
static uint64_t g_guest_child_parked_heap_next;
static uint64_t g_guest_child_parked_brk;
static char g_guest_child_parked_cwd[256];