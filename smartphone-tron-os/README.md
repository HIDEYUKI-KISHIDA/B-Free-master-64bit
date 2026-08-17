# SMARTPHONE TRON OS

Xperia 10 III（**lena** / SM6350）向け **T-Kernel 2.0 系 OS（STOS）**。
x86_64 デスクトップ検証系とは独立。仕様正本は v0.5.1。

## 製品路線

**lena BSP → フェーズ A（kexec 初鳴き）→ フェーズ B（FB・タッチ）→ フェーズ C（QML シェル）**

APK 互換・Linux バイナリそのまま実行は MVP 対象外。

## いまの実装（Phase 1 から）

| 段階 | 内容 | 状態 |
|------|------|------|
| Phase 1 | `bsp_lena_pdx213.h`（DTS 転写） | このブランチ |
| Phase 2 | QEMU virt で UART 文字列 | このブランチ（`STOS: qemu OK`） |
| Phase 3 | lena kexec で `STOS: lena OK` | 実機待ち |
| tk2 本体 | T-Kernel 2.0 AArch64 フォーク | 未取込（bring-up が入口） |

## ビルドと確認

ホスト: `gcc-aarch64-linux-gnu`, `qemu-system-aarch64`

```bash
# Phase 1: BSP ヘッダが仕様 §8.1 を満たすこと
python3 smartphone-tron-os/tools/check_bsp.py
python3 smartphone-tron-os/tests/test_bsp_header.py

# Phase 2: QEMU UART
make -C smartphone-tron-os/kernel BOARD=qemu_virt check
```

lena Image（実機 kexec 用）:

```bash
make -C smartphone-tron-os/kernel BOARD=lena
# → smartphone-tron-os/kernel/build/lena/stos.Image
./smartphone-tron-os/tools/kexec_lena.sh \
    smartphone-tron-os/kernel/build/lena/stos.Image lena.dtb
```

## フォルダ

```
smartphone-tron-os/
├── docs/spec/          v0.5.1 正本
├── bsp/include/        Pattern A ヘッダ
├── kernel/bringup/     AArch64 Image + UART
├── tools/              BSP 検査 / QEMU / kexec
├── runtime/            フェーズ C（空）
├── shell/              フェーズ C（空）
└── apps/               フェーズ C（空）
```

## 仕様

[`docs/spec/SMARTPHONE_TRON_OS_仕様書_v0.5.1.ja.md`](docs/spec/SMARTPHONE_TRON_OS_仕様書_v0.5.1.ja.md)
