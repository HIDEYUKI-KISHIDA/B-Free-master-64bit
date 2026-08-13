# SMARTPHONE TRON OS

スマートフォン向け TRON / T-Kernel 2.0 / B-Free 系 OS 計画の **ドキュメント正本**（Git 管理用）。

ローカル作業パス（Windows）:

```
J:\SMARTPHONE TRON OS\
C:\Users\h_kis\Desktop\SMARTPHONE TRON OS\   （旧配置・統合対象）
```

## 関連プロジェクト

| プロジェクト | パス | 役割 |
|-------------|------|------|
| B-Free x86_64 | `Program/bfree_x86_64/` | 検証用デスクトップ・Qt guest・syscall 実装 |
| B-Free aarch64 | `Program/bfree_aarch64/` | TK2 AArch64 移植・Android 互換層設計 |
| JH7110 MiniPC | `J:\JH7110\` | RISC-V ボード（別ハードウェアライン） |

## フォルダ構成

```
smartphone-tron-os/
├── README.md                 … 本ファイル
├── docs/
│   ├── spec/                 … 現行仕様書（v0.5+）
│   ├── archive/              … 旧版（v0.4 以下）
│   ├── hardware/             … BSP・Xperia・DT・電源
│   ├── software/             … Qt guest・POSIX・syscall
│   └── reviews/              … 外部レビュー・メモ
├── bsp/                      … DTS 断片・レジスタメモ（将来）
├── guest/                    … ゲスト ELF 設計メモ（bfree へのリンク）
└── tools/
    └── ORGANIZE_LOCAL.ja.md  … J: ドライブ整理手順
```

## 現行仕様書

| 版 | ファイル | 状態 |
|----|----------|------|
| **v0.5** | `docs/spec/SMARTPHONE_TRON_OS_仕様書_v0.5.md` | **正本（ローカルからコピー待ち）** |
| v0.4 | `docs/archive/SMARTPHONE_TRON_OS_仕様書_v0.4.md` | アーカイブ |

## 製品路線（1 行）

**④ BSP（DTS→TRON）→ フェーズ A 起動 → ① Qt ゲスト ELF**  
APK（③）・Linux バイナリそのまま（②）は MVP 対象外。詳細は仕様書 §17。
