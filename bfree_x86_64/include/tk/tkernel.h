#ifndef BFREE_TK_TKERNEL_H
#define BFREE_TK_TKERNEL_H

#include <tk/typedef.h>
#include <tk/devmgr.h>
#include <tk/errno.h>

#define TA_HLNG 0x00000001U
#define TA_RNG0 0x00000002U
#define TA_TFIFO 0x00000004U

#define TWF_ORW 0x00000001U
#define TWF_BITCLR 0x00000002U

#define TPRI_SELF   0
#define TA_INHERIT  0x00000008U

typedef struct {
    ATR tskatr;
    size_t stksz;
    void *stkptr;
    PRI itskpri;
    FP task;
    void *exinf;
} T_CTSK;

typedef struct {
    ATR mbfatr;
    INT bufsz;
    INT maxmsz;
} T_CMBF;

typedef struct {
    ATR flgatr;
    UINT iflgptn;
} T_CFLG;

ID tk_cre_tsk(CONST T_CTSK *pk_ctsk);
ER tk_sta_tsk(ID tskid, INT stacd);

ID tk_cre_mbf(CONST T_CMBF *pk_cmbf);
ER tk_snd_mbf(ID mbfid, CONST void *msg, INT msgsz, TMO tmout);
INT tk_rcv_mbf(ID mbfid, void *msg, TMO tmout);

ID tk_cre_flg(CONST T_CFLG *pk_cflg);
ER tk_del_flg(ID flgid);
ER tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout);
ER tk_set_flg(ID flgid, UINT setptn);
ER tk_clr_flg(ID flgid, UINT clrptn);

ID tk_def_dev(CONST UB *devnm, CONST T_DDEV *ddev, T_IDEV *idev);

typedef struct {
    ATR mtxatr;
    PRI ceilpri;
} T_MTXCB;

ID tk_cre_mtx(CONST T_MTXCB *pk_cmtx);
ER tk_del_mtx(ID mtxid);
ER tk_loc_mtx(ID mtxid);
ER tk_unl_mtx(ID mtxid);

ER tk_rot_rdq(PRI tskpri);
void tk_ext_tsk(void);

#endif