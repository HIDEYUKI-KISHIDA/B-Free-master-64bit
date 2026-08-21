# D3 / 本デスク Phase チェックリスト

正本: `docs/HONDESK_TODOLIST.ja.md`。このファイルは D3 以降の段階的完成条件。

## Phase 0 — D3 minimal（済）

**目的**: patched kernel + stub ISO で `desktop.elf` が vfork exec され `main entry` まで到達。

**確認**:
```bash
cd bfree_x86_64
make -C kernel clean && make -C kernel RELEASE=1
bash tools/relink_desktop_phdrs_only.sh   # WSL: PHDR patch
BFREE_D3=1 bash tools/_d3_compositor_stub_smoke.sh
```

**シリアル必須**:
- `[D3] execve desktop.elf`
- `[D3] desktop wayland exec`
- `exec transfer desktop.elf`
- `[desktop_qt] main entry`

**WARN 許容**: `[wl] vfork parent` なし、`platform=bfree`（prebuilt desktop は bfree QPA のまま）

---

## Phase 1 — D3 full（Wayland QPA + vfork parent）

**目的**: `desktop.elf` を stub Wayland QPA（`qbfree_wayland`）で relink。1024×768 塗り → `exit_group` → compositor が `[wl] vfork parent` で再開。

**ビルド**（maintainer Qt prefix + Program `.o` ツリー要）:
```bash
bash tools/build_desktop_d3_wayland.sh
```

**リンク内容**:
- `guest_main.o` に `-DBFREE_D3_WAYLAND_QPA`
- `qbfree_wayland.o` + `wl_stub_flush.o`（`libqbfree.a` はリンクしない）
- `/tmp/bfree-d3-wl` マーカーで `guest_d3_wayland_desk_session()` へ分岐

**確認**:
```bash
BFREE_D3=1 BFREE_D3_FULL=1 bash tools/_d3_compositor_stub_smoke.sh
```

**シリアル必須（FULL tier）**:
- Phase 0 のすべて
- `[desktop_qt] D3 wayland desk session`
- `[desktop_qt] platform=wayland`
- `[desktop_qt] D2c fullscreen` / `[desktop_qt] D2 fill desk`
- `[desktop_qt] exit_group`
- `[wl] vfork parent`（または `vfork parent resume`）

---

## Phase 2 — 本デスク MVP（D2 レイアウト on stub）

**目的**: D2c と同じ GuestMvpShell レイアウト（壁紙 `#7A8FA8` / 白カード / EX・VW・TE / バー `#334155`）を **desktop.elf** から 1024×768 で塗る。QQmlEngine / `beginCreate` は使わない。

**実装**: Phase 1 の `guest_d3_wayland_desk_session()` 内 `guest_d3_fill_rect`（`qt_wl_hello` D2c と同系）。

**画面**: stub 仮 chrome が見えない（クライアント SHM が compositor 出力を覆う）。

**未着手（Phase 2 以降）**:
- 製品 QML qmlcache の Wayland クライアント載せ（D2b は `BFREE_D2B_QML=0` で退避中）
- イベントループ常駐（現状は 1 フレーム塗り → `exit_group`）

---

## Phase 3 — persist / 安定化 / ドキュメント

**目的**: 製品机の足場（persist、sha256 固定、WSL 手順）を整える。

| 項目 | 状態 |
|------|------|
| `/persist/desk.txt` = `from-desk` | `guest_persist_create_desk_note()`（D3 session 終了前） |
| `tools/kernel.d3.good.sha256` | kernel TLS/BSS scrub 固定 |
| `tools/desktop.elf.good.sha256` | PHDR-patched desktop 指紋 |
| `docs/HONDESK_TODOLIST.ja.md` | D3 minimal / full を分離記載 |
| PR #61 | draft — WSL で FULL smoke 後に ready |

**WSL 一発**:
```bash
cd ~/bfree-d2c/bfree_x86_64
git pull
export PATH="$HOME/xshim:$HOME/x86_64-elf-toolchain/bin:$PATH"
make -C kernel clean && make -C kernel RELEASE=1
bash tools/build_desktop_d3_wayland.sh
BFREE_D3=1 BFREE_D3_FULL=1 bash tools/_d3_compositor_stub_smoke.sh
```

---

## 今やらない

- D2b (`QQmlEngine` on Wayland client)
- DRM / EGL（G1–G3）
- 日次 `bfree.iso` 上書き
