/*
 * sysmain/tkernel_stubs.c
 *
 * カーネルビルド用 TK2 API ミニマルスタブ
 * - -ffreestanding/-nostdlib 環境で動作
 * - 本物のスケジューラなし: tk_sta_tsk は同期呼び出し
 * - Stage 1 TK2 API 実行確認用
 */

#include <tk/tkernel.h>

/* ---- Task ---- */
#define STUB_MAX_TASKS 16

typedef void (*stub_task_fn_t)(INT, void *);

typedef struct {
    ID id;
    stub_task_fn_t fn;
    void *exinf;
} stub_task_t;

static stub_task_t s_tasks[STUB_MAX_TASKS];
static ID s_next_task_id = 1;

static stub_task_t *find_task(ID id)
{
    int i;
    for (i = 0; i < STUB_MAX_TASKS; ++i)
        if (s_tasks[i].id == id) return &s_tasks[i];
    return (stub_task_t *)0;
}

ID tk_cre_tsk(CONST T_CTSK *pk)
{
    int i;
    if (!pk || !pk->task) return E_PAR;
    for (i = 0; i < STUB_MAX_TASKS; ++i) {
        if (s_tasks[i].id == 0) {
            s_tasks[i].id = s_next_task_id++;
            s_tasks[i].fn = (stub_task_fn_t)pk->task;
            s_tasks[i].exinf = pk->exinf;
            return s_tasks[i].id;
        }
    }
    return E_LIMIT;
}

ER tk_sta_tsk(ID tskid, INT stacd)
{
    stub_task_t *t = find_task(tskid);
    if (!t) return E_NOEXS;
    t->fn(stacd, t->exinf);
    return E_OK;
}

/* ---- Mutex ---- */
#define STUB_MAX_MTXS 8

typedef struct {
    ID id;
    int locked;
} stub_mtx_t;

static stub_mtx_t s_mtxs[STUB_MAX_MTXS];
static ID s_next_mtx_id = 1;

static stub_mtx_t *find_mtx(ID id)
{
    int i;
    for (i = 0; i < STUB_MAX_MTXS; ++i)
        if (s_mtxs[i].id == id) return &s_mtxs[i];
    return (stub_mtx_t *)0;
}

ID tk_cre_mtx(CONST T_MTXCB *pk)
{
    int i;
    (void)pk;
    for (i = 0; i < STUB_MAX_MTXS; ++i) {
        if (s_mtxs[i].id == 0) {
            s_mtxs[i].id = s_next_mtx_id++;
            s_mtxs[i].locked = 0;
            return s_mtxs[i].id;
        }
    }
    return E_LIMIT;
}

ER tk_del_mtx(ID mtxid)
{
    stub_mtx_t *m = find_mtx(mtxid);
    if (!m) return E_NOEXS;
    m->id = 0; m->locked = 0;
    return E_OK;
}

ER tk_loc_mtx(ID mtxid)
{
    stub_mtx_t *m = find_mtx(mtxid);
    if (!m) return E_NOEXS;
    m->locked = 1;
    return E_OK;
}

ER tk_unl_mtx(ID mtxid)
{
    stub_mtx_t *m = find_mtx(mtxid);
    if (!m) return E_NOEXS;
    m->locked = 0;
    return E_OK;
}

/* ---- Scheduler stubs (no-op: シングルスレッド実行) ---- */
ER tk_rot_rdq(PRI tskpri)
{
    (void)tskpri;
    return E_OK;
}

void tk_ext_tsk(void)
{
    /* stub: 単純リターン */
    return;
}
