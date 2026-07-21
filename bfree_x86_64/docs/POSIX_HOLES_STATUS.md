# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-22 (F1 disk persist / F2 UDP / F3 LTP curated)  
**Full TODOLIST:** [POSIX_COMPAT_TODOLIST.md](./POSIX_COMPAT_TODOLIST.md)  
**P4 scope:** [POSIX_PHASE7_AGREED_SCOPE.md](./POSIX_PHASE7_AGREED_SCOPE.md)  
**📱 指示はこちら:** [NEXT_INSTRUCTIONS.md](./NEXT_INSTRUCTIONS.md)

## Done

- [x] **P0** phase3 ALL PASS
- [x] **P1** residuals 方針
- [x] **P2** NOFORK=0 verified
- [x] **P3** musl `hello.elf` → `MUSL_HELLO_OK`
- [x] **P4** Phase7 **合意範囲** green（`tools/_p4_agreed_gate.sh`）
- [x] **E1** ash `FORK_BG` → AS-copy fork；jobs/fg smoke ALL PASS
- [x] **E2** futex 4-slot waiter queue + cleartid wake
- [x] **E3** nestable `preempt_disable`（timer preempt は未）
- [x] **F1** ATA PIO + `/persist` ディスク永続化 — **再起動をまたいで green**（`tools/_f1_persist_smoke.sh`）
- [x] **F2** UDP loopback（socket/bind/sendto/recvfrom/connect + sendmsg/recvmsg 単一 iovec）
- [x] **F3** LTP curated subset（`userland/ltp_curated/` 13 testcases, LTP 形式 TPASS/TFAIL）

## Phase7 残り

| 済み | 残り |
|------|------|
| `/persist` 実ディスク永続（ATA PIO + BFP1 レコード） | tiny FAT/ext2 での本マウント |
| UDP loopback + sendmsg/recvmsg(1-iov) | e1000 実 NIC / slirp 外向き |
| LTP curated 13 cases in-tree | full LTP vendor（任意） |
| ENOSYS: sendmsg/recvmsg 解消 | 残 ENOSYS ゼロ化 / H01 fpstate / timer preempt |

## Git

- Work: `work/posix-holes-redo`
- Backup: `backup/polish-e1e3-f1-20260722`（E1–E3 時点）
- 最新は push 済み — スマホは GitHub で本ファイルと NEXT_INSTRUCTIONS.md を参照
