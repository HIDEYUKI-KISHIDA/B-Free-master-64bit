# SMARTPHONE TRON OS

スマートフォン向け **T-Kernel 2.0 系 OS（STOS）** の仕様・設計正本。

**x86_64 デスクトップ検証系とは独立した製品ライン。** 本フォルダに STOS 単体の仕様と BSP メモを置く。

ローカル作業パス（Windows）:

```
J:\SMARTPHONE TRON OS\
```

## 現行仕様書

| 版 | ファイル | 状態 |
|----|----------|------|
| **v0.5.1** | [`docs/spec/SMARTPHONE_TRON_OS_仕様書_v0.5.1.ja.md`](docs/spec/SMARTPHONE_TRON_OS_仕様書_v0.5.1.ja.md) | **正本（振り出し版）** |
| v0.5 / v0.4 | ローカル `J:\` 旧版 | アーカイブ候補 → `docs/archive/` |
| vision v0.6 | ローカル追補 | `docs/vision/` へ分離推奨（製品 MVP 外） |

## 製品路線（1 行）

**lena BSP → フェーズ A（kexec 初鳴き）→ フェーズ B（FB・タッチ）→ フェーズ C（QML シェル）**

APK 互換・Linux バイナリそのまま実行は MVP 対象外。

## フォルダ構成

```
smartphone-tron-os/
├── README.md
├── docs/
│   ├── spec/          … 現行仕様（v0.5.1+）
│   ├── archive/       … 旧版
│   ├── vision/        … 社会ポジショニング等（v0.6 追補）
│   ├── hardware/      … BSP・DT・PMIC メモ
│   └── software/      … Runtime・Qt・syscall メモ
├── bsp/               … DTS 断片（将来）
└── tools/
    └── ORGANIZE_LOCAL.ja.md
```

## ハードウェアライン

| ライン | SoC | 備考 |
|--------|-----|------|
| **STOS** | Xperia 10 III（lena / SM6350） | 本仕様書 |
| JH7110 MiniPC | JH7110 (RISC-V) | 別プロジェクト |

## MVP 合格（要約）

1. UART: `STOS: lena OK`
2. 画面: FB でピクセル更新
3. タッチ: 座標取得
4. `shell.elf` + 同梱アプリ 1 個起動
5. ビルド・kexec 手順が再現可能

詳細は v0.5.1 §0.3。
