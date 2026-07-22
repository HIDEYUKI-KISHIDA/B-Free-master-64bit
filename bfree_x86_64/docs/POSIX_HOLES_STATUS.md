# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-22 (A–F compat thicken)  
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
- [x] **F2** UDP loopback + **e1000 TX**（`sendto`→`udp_send` / bind→`udp_register_port`；`tools/_f2_e1000_udp_smoke.sh`）
- [x] **F3** LTP curated subset（`userland/ltp_curated/` 13 testcases, LTP 形式 TPASS/TFAIL）
- [x] **A** ENOSYS appearance tracer（`[ENOSYS] nr=…` UART + histogram）
- [x] **B** UDP DNS stub → `10.0.2.3:53` answered from `/etc/hosts`
- [x] **C** futex waiters 8 / spin 64 + timer `need_resched` hint（preempt_count 尊重）
- [x] **D** sigframe `fxsave` blob + nested CATCH queue（1）
- [x] **E** `/persist`+`/home` getdents、`ls /` に persist、`/tmp` NS 汚染除外
- [x] **F** BusyBox curated +17 applets（tee/mktemp/nslookup/sha256sum/…）

## Phase7 残り

| 済み | 残り |
|------|------|
| `/persist` 実ディスク永続（ATA PIO + BFP1 レコード） | tiny FAT/ext2 での本マウント |
| UDP loopback + e1000 DGRAM TX（`_f2_e1000_udp_smoke.sh`） | slirp 外向き TCP / 本格 RX echo |
| LTP curated 13 cases in-tree | full LTP vendor（任意） |
| ENOSYS tracer（出現ログ） | 残 ENOSYS ゼロ化 / 本プリエンプト |

## Git

- Work: `work/posix-holes-redo`
- Smoke: `tools/_abcdef_smoke.sh`
- スマホ: GitHub → このブランチ → [`docs/NEXT_INSTRUCTIONS.md`](./NEXT_INSTRUCTIONS.md) を編集して指示
