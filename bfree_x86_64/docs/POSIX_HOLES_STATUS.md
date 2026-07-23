# POSIX holes — mobile status

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-23 (desktop 本線安定優先)  
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
- [x] **F1b** FAT BPB probe scaffold（`BFREE_PERSIST_FAT_PROBE=1`；`_f1_persist_fat_probe_smoke.sh`）— mount は未
- [x] **F2** UDP loopback + **e1000 TX/RX echo**（`_f2_e1000_udp_smoke.sh` / `_f2_e1000_udp_rx_smoke.sh`）
- [x] **F2c** slirp **外向き TCP** active-open（`_f2_e1000_tcp_smoke.sh` → CONNECT_OK / F2_E1000_TCP_OK）
- [x] **F3** LTP curated subset（`userland/ltp_curated/` 13 testcases, LTP 形式 TPASS/TFAIL）
- [x] **A** ENOSYS appearance tracer（`[ENOSYS] nr=…` UART + histogram）
- [x] **B** UDP DNS stub → `10.0.2.3:53` answered from `/etc/hosts`
- [x] **C** futex waiters 8 / spin 64 + timer `need_resched` hint（preempt_count 尊重）
- [x] **D** sigframe `fxsave` blob + nested CATCH queue（1）
- [x] **E** `/persist`+`/home` getdents、`ls /` に persist、`/tmp` NS 汚染除外
- [x] **F** BusyBox curated +17 applets（tee/mktemp/nslookup/sha256sum/…）
- [x] **gthr / pthread→clone** wrap（`guest_link_compat`；`_pthread_clone_smoke.sh` PASS；**desktop.elf relink済み**）
- [x] **desktop 本線回帰**（`_desktop_pthread_clone_smoke.sh`：clone wrap + `QQmlEngine ok` + no panic）

## Phase7 残り

| 済み | 残り |
|------|------|
| `/persist` 実ディスク永続（ATA PIO + BFP1）+ FAT BPB probe | tiny FAT/ext2 での本マウント（**後回し**；desktop 優先） |
| UDP + e1000 DGRAM TX/RX + **外向き TCP** | listen/accept / 本スタック統合 |
| LTP curated 13 cases in-tree | full LTP vendor（任意） |
| ENOSYS tracer（出現ログ） | 残 ENOSYS ゼロ化 / 本プリエンプト |
| pthread clone wrap + **desktop 回帰 green** | QML/UI・入力・描画の深掘り |

## Git

- Work: `work/posix-holes-redo`
- Smoke: `tools/_abcdef_smoke.sh`
- スマホ: GitHub → このブランチ → [`docs/NEXT_INSTRUCTIONS.md`](./NEXT_INSTRUCTIONS.md) を編集して指示
