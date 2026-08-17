# SMARTPHONE TRON OS — プロジェクト地図

> 正本: [`spec/SMARTPHONE_TRON_OS_仕様書_v0.5.1.ja.md`](spec/SMARTPHONE_TRON_OS_仕様書_v0.5.1.ja.md)

## アーキテクチャ（STOS 単体）

```
[④ BSP / DTS]     lena → bsp_lena_pdx213.h     ★ Phase 1（本ブランチ）
       ↓
[カーネル]        bring-up Image → (将来) stos-tk2-lena
       ↓
[STOS Runtime]    musl + stos_syscall（未着手）
       ↓
[UI]              Qt6 QML → shell.elf（未着手）
```

**x86_64 デスクトップ系への依存は必須としない。**

## フェーズ

| フェーズ | 内容 | MVP | このブランチ |
|----------|------|-----|----------------|
| A / Phase 1 | BSP ヘッダ | — | 完了 |
| A / Phase 2 | QEMU UART | — | 完了（`STOS: qemu OK`） |
| A / Phase 3 | kexec 初鳴き | MVP-1 | 実機待ち |
| B | FB + タッチ | MVP-2,3 | ヘッダに FB/タッチ定数のみ |
| C | Runtime + QML | MVP-4 | 未着手 |
| D | 手順・initrd | MVP-5 | kexec スクリプト草案 |

## 第 1 実機

**Sony Xperia 10 III（lena / SM6350）** — v0.5.1 §0.4
