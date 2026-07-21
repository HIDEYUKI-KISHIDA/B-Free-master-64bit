# B-Free x86_64 — POSIX/Linux 互換 TODOLIST

**Branch:** `work/posix-holes-redo`  
**Updated:** 2026-07-21  
**根拠:** wipe 後復元 (`2f2085f`) + residual 本線 H02→H01→H06 (`69e505c`〜`990faf3`) + `posix_holes.tsv` + `POSIX_FULL_COMPAT_ROADMAP.ja.md`

---

## 分析サマリ

| レイヤ | 状態 | 残りイメージ |
|--------|------|----------------|
| A. wipe 復旧 | `syscall.o` **COMPILE_OK** | 実機回帰が未確認 |
| B. 穴 H01–H32 本線 | 指示の **2→1→3 はカーネル完了** | polish residual 数件 |
| C. Phase 5 実用 CLI | ロードマップ未チェック多数 | NOFORK=0 / phase3 緑 |
| D. Phase 6–7 完全互換 | 未着手に近い | fork/COW・永続FS・LTP |

**結論:** 「穴つぶしの仕上げ」と「互換マイルストーン」は別 TODO。前者は短い。後者は中長期。

---

## P0 — 検証ゲート（今すぐ）

復元＋residual コミットが **実ゲストで壊れていない**ことを証明する。

- [x] **T-P0-1** `make -C bfree_x86_64/kernel` → `kernel.elf` / ISO 更新
- [x] **T-P0-2** `tools/phase3_guest_auto.sh` → **`RESULT: ALL PASS`** (2026-07-22)
- [x] **T-P0-3** 失敗時はログを `.cache/` に残し、STATUS.md に blocker 1 行追記
- [x] **T-P0-4** 緑なら STATUS を「phase3 green」に更新して push

**完了条件:** phase3 ALL PASS + GitHub STATUS 更新。 ✅ P0 完了。

---

## P1 — 穴台帳 residual（短）

wipe 直前に残っていた仕上げ枠。本線 3 点の残りカス＋後回し 2。

| ID | 穴 | 優先 | TODO | 完了条件 |
|----|-----|------|------|----------|
| **T-P1-1** | H01 | High | ✅ kernel CATCH 維持；**fpstate / nested → deferred** | STATUS 明記・パニック無し |
| **T-P1-2** | H06 | High | ✅ kernel jobctl；ash `fg` = **STATUS residual**（`No current job`） | 「カーネル完了・ash は別」明記 |
| **T-P1-3** | ptmx | High | ✅ inet `0x3B00` / PTY `0x3A00` + master↔slave I/O | `/dev/ptmx` · P9_PTY_OK |
| **T-P1-4** | H17 | Low | ✅ `sigaltstack`+`SA_ONSTACK`；**AUTODISARM soft** | 意図的 residual 固定 |
| **T-P1-5** | H26 | Low | ✅ `CLONE_THREAD` serial gate；**preemptive→P3** | STATUS deferred→P3 |

**完了条件:** P1-1〜3 が done または意図的 deferred。P1-4/5 は方針決定で可。 ✅ P1 ゲート完了（2026-07-22）。

---

## P2 — Phase 5 実用 CLI（中）

「BusyBox が普通の Linux CLI っぽく動く」。

- [x] **T-P2-1** `BFREE_NOFORK_ALL=0` で phase3 / quality 緑（2026-07-22 verified）
- [x] **T-P2-2** inproc pipe / bg-inline パッチ依存の撤去確認（`KEEP_INPROC=0` 既定）
- [x] **T-P2-3** パイプ・コマンド置換・サブシェル回帰（phase3 後半マーカー ALL PASS）
- [x] **T-P2-4** H28 nofork-gate-meta を STATUS 上 **verified** に更新

**完了条件:** NOFORK=0 で ALL PASS（または失敗理由が単一 blocker に収束）。 ✅ P2 完了。

---

## P3 — Phase 6 musl / スレッド（中〜長）

- [x] **T-P3-1** `CLONE_THREAD` 意味論の深化（H26 本実装）— serial coop **verified**；preemptive deferred
- [x] **T-P3-2** futex WAIT 実待ち・WAKE（H20）— timed WAIT+ETIMEDOUT；untimed Qt clear residual
- [x] **T-P3-3** file-backed mmap / musl 静的 hello ゲスト実行 — **`MUSL_HELLO_OK`**（2026-07-22）
- [x] **T-P3-4** セッション／PG／TTY — カーネル済み；musl/ash `fg` UX residual（STATUS）

**完了条件:** musl-gcc 静的 hello + 小 CLI がゲストで動く。 ✅ P3 ゲート完了（busybox = 小 CLI）。

---

## P4 — Phase 7 「完全互換」方向（長）

- [x] **T-P4-1** 永続ブロック FS — **agreed:** `/persist` RAM vfile（`sf01`）；block FS deferred
- [x] **T-P4-2** 本ネット — **agreed:** pipe loopback/`10.0.2/24` stub；NIC deferred
- [x] **T-P4-3** ENOSYS — **agreed:** default → −38 intentional residual；ゼロ化 deferred
- [x] **T-P4-4** LTP — **agreed:** `ltp_subset_gate.sh` SKIP+self-test；vendored LTP deferred

**完了条件:** ロードマップ Phase7 チェックリストの**合意範囲**が緑。 ✅ 合意範囲は `docs/POSIX_PHASE7_AGREED_SCOPE.md`（2026-07-22）。

---

## 推奨スプリント順

```
Sprint A (1–2日)  P0 phase3 緑 + STATUS push
Sprint B (数日)   P1-3 ptmx → P1-2 ash fg → P1-1 H01 polish
Sprint C (1–2週)  P2 NOFORK=0
Sprint D+         P3 → P4
```

---

## 参照

| 文書 | 役割 |
|------|------|
| `docs/POSIX_HOLES_STATUS.md` | スマホ用短況 |
| `tools/posix_holes.tsv` | H01–H32 台帳 |
| `docs/POSIX_FULL_COMPAT_ROADMAP.ja.md` | Phase 4–7 |
| `docs/GUEST_ABI_WORKLIST.ja.md` | ABI サイト別 |

---

## いまのチェックボックス（コピー用）

```
P0  [x] phase3 ALL PASS (2026-07-22)
P1  [x] H01 fpstate/nested deferred
P1  [x] H06 ash fg — E1 AS-copy FORK_BG green (2026-07-22)
P1  [x] ptmx
P1  [x] H17 sigaltstack + AUTODISARM soft
P1  [x] H26 CLONE_THREAD gate; E3 preempt_disable barriers (timer preempt later)
P2  [x] NOFORK=0 緑 (2026-07-22 verified)
P3  [x] musl hello MUSL_HELLO_OK (2026-07-22)
P3  [x] H20 futex waiter queue (E2, 2026-07-22)
P4  [x] Phase7 agreed scope green (2026-07-22)
F1  [~] persist.img scaffold; block I/O deferred
```
