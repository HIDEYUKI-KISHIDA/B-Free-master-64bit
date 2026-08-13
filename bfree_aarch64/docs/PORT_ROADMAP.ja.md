# B-Free x86_64 → aarch64 移植ロードマップ

更新: 2026-08-13  
前提: [ANDROID_COMPAT_LAYER.ja.md](ANDROID_COMPAT_LAYER.ja.md) の Phase D 以降は本ロードマップ Phase C 完了後

---

## フェーズ概要

| Phase | 目標 | 完了条件 |
|-------|------|----------|
| **A0** | ツリー骨格 | `bfree_aarch64/` Makefile + README + CI 雛形 |
| **A1** | QEMU 起動 | `virt` で UART に `Hello`、例外ベクタ動作 |
| **A2** | カーネル MVP | musl `hello.elf`、基本 syscall 10 個 |
| **A3** | ユーザーランド | init + busybox、devfs/tmpfs |
| **A4** | GUI 本線 | compositor + Qt Wayland client（x86_64 と同構成） |
| **A5** | 実機 | ボード固有 DTB / UEFI ブート |

---

## x86_64 からの差分マップ

```
bfree_x86_64/                          bfree_aarch64/
├── boot/grub/ (Multiboot2)      →     boot/uefi/ または boot/qemu-virt/
├── kernel/sysdepend/cpu/x86_64/ →     kernel/sysdepend/cpu/aarch64/
├── kernel/sysdepend/x86_64/       →     kernel/sysdepend/aarch64/
├── tools/build_x86_64_elf_*.sh  →     tools/build_aarch64_elf_*.sh
├── out/x86_64-elf-libm/         →     out/aarch64-elf-libm/
└── include/bfree/bfree_guest_abi.h →  include/bfree/bfree_guest_abi.h (svc)
```

---

## ブート戦略（推奨）

1. **開発**: QEMU `-machine virt -cpu cortex-a72` + U-Boot または直接 kernel ロード
2. **実機**: UEFI + GRUB または U-Boot + DTB
3. Multiboot2 は **使用しない**（x86 専用）

---

## ツールチェーン

| 項目 | x86_64 | aarch64 |
|------|--------|---------|
| GCC ターゲット | `x86_64-elf` | `aarch64-none-elf` または `aarch64-elf` |
| libc | musl (クロス) | 同手順で再ビルド |
| C++ | libstdc++ 同梱 | 同 |
| Qt guest | x86_64-elf 静的 | aarch64-elf 静的（ホスト moc は x86_64 Linux のまま） |

---

## 既存 ARM 資産の位置づけ

| パス | 扱い |
|------|------|
| `kernel_arm/` | API 参考。本番カーネルには未統合 |
| `btron-pc/kernel_arm/` | 32-bit ARM。AArch64 には流用不可 |
| `ARM64_*.md` (リポジトリルート) | ネットワーク層計画。本ツリー docs とリンク |

---

## x86_64 本線との依存

**推奨**: x86_64 compositor ISO が QEMU で起動確認できてから A4 に入る。  
A0〜A2 は x86_64 と並行可能。
