# SMARTPHONE TRON OS — プロジェクト地図

> 正本: [`spec/SMARTPHONE_TRON_OS_仕様書_v0.5.1.ja.md`](spec/SMARTPHONE_TRON_OS_仕様書_v0.5.1.ja.md)

## アーキテクチャ（STOS 単体）

```
[④ BSP / DTS]     lena → bsp_lena_pdx213.h     ★ Phase 1
       ↓
[カーネル]        stos-tk2-lena (T-Kernel 2.0 AArch64)
       ↓
[STOS Runtime]    musl + stos_syscall（STOS 専用 ABI）
       ↓
[UI]              Qt6 QML → shell.elf
       ↓
[同梱アプリ]      settings / clock / about …
```

**x86_64 デスクトップ系への依存は必須としない。**

## ソフトウェア資産 4 層

| 層 | MVP |
|----|-----|
| ④ DT/ドライバ知識 → BSP | ○ 最優先 |
| ① FLOSS ソース再ビルド | ○ |
| ② バイナリそのまま | × |
| ③ Android Framework / APK | × |

## フェーズ

| フェーズ | 内容 | MVP |
|----------|------|-----|
| A | kexec + UART + タイマ | MVP-1 |
| B | FB + タッチ | MVP-2,3 |
| C | Runtime + QML シェル | MVP-4 |
| D | 手順・initrd | MVP-5 |

## 並行トラック

| トラック | 内容 |
|----------|------|
| **K** | カーネル + BSP（フェーズ A） |
| **I** | 表示・入力 BSP（フェーズ B） |
| **U** | Runtime + Qt（フェーズ C） |

## 第 1 実機

**Sony Xperia 10 III（lena / SM6350）** — v0.5.1 §0.4

## 混同しないもの

| 項目 | 扱い |
|------|------|
| JH7110 RISC-V ボード | 別フォルダ |
| Android / APK 互換 | スコープ外 |
| v0.6 社会インフラ・富岳・メッシュ | `docs/vision/`（Product v2） |
