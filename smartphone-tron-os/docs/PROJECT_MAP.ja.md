# SMARTPHONE TRON OS — プロジェクト地図

## レイヤと優先度（仕様書 §17 要約）

```
[④ BSP / DTS]     Xperia 等 → TRON BSP 転写     ★ 今やる（Phase 1〜5）
       ↓
[カーネル]        TK2 / T-Kernel 2.0 AArch64
       ↓
[ゲスト ABI]      B-Free guest syscall + musl
       ↓
[① アプリ]        Qt/QML guest ELF（desktop 型）  ★ フェーズ A 後
       ↓
[③ APK]          対象外
[② .so そのまま]  対象外（長期研究 §17.7）
```

## 他リポジトリとの対応

| SMARTPHONE TRON OS | B-Free リポジトリ |
|--------------------|-------------------|
| Qt スマホ UI / QML | `bfree_x86_64/userland/desktop_qt/` |
| guest syscall | `bfree_x86_64/include/bfree/bfree_guest_abi.h` |
| compositor / Wayland | `bfree_x86_64/docs/COMPOSITOR_MIGRATION.ja.md` |
| aarch64 移植 | `bfree_aarch64/docs/PORT_ROADMAP.ja.md` |
| Android 互換（部分） | `bfree_aarch64/docs/ANDROID_COMPAT_LAYER.ja.md` |
| x86 検証 | `bfree_x86_64/` ISO / QEMU |

## ハードウェアライン（混同しない）

| ライン | SoC | 用途 |
|--------|-----|------|
| **SMARTPHONE TRON OS** | Xperia 系 / ARM64 スマホ | 本仕様書 |
| JH7110 Compact MiniPC | StarFive JH7110 (RISC-V) | 別ボード・別フォルダ |

## フェーズ（製品 MVP）

| フェーズ | 内容 |
|----------|------|
| A | カーネル + 最小 BSP で電源 ON・UART |
| B | 入力・表示・ネットワーク |
| C | Qt guest 1 本（ランチャー相当） |
| D | settings / 電話アプリ等（ゲスト追加） |

## ドキュメント整理チェックリスト

- [ ] `docs/spec/` に v0.5 のみ
- [ ] v0.4 は `docs/archive/`
- [ ] Desktop 旧フォルダと J: を統合済み
- [ ] JH7110 資料が混ざっていない
- [ ] Git に `smartphone-tron-os/` を commit
