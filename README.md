# B-Free / BTRON系OS プロジェクト

## 概要 / Overview
B-FreeはBTRONの理念を受け継ぎ、現代的な価値観（多様性・アクセシビリティ・AI連携・持続可能性）を取り入れたオープンソースOSプロジェクトです。
B-Free inherits the philosophy of BTRON and aims to be a modern, open-source OS with diversity, accessibility, AI integration, and sustainability.

## 特徴 / Features
- 多様なアーキテクチャ対応（x86_64, ARM など）
- モダンUI/UX・アクセシビリティ重視
- POSIX/Web標準API・他OS連携
- サンプルアプリ・実用事例の拡充
- CI/CD・自動テスト・品質管理

## ビルド・実行方法 / Build & Run

x86_64 の本線は `bfree_x86_64/` です。

- clone から一本: **hello ISO**（表紙だけ。机ではない）
- 手元の `bfree.iso` があるとき: **desk ISO**（今動いた EX/TE の机。別名にするだけ）

```
cd bfree_x86_64
bash tools/make_hello_iso.sh
bash tools/make_desk_iso.sh
```

詳細: `bfree_x86_64/docs/ISO_RECIPE.ja.md`

（古い例: Linux/WSL2推奨。詳細はdocs/以下参照）
```bash
cd btron-pc/boot/2nd
make clean
make 2ndboot64
make iso64
qemu-system-x86_64 -m 512 -cdrom bfree-64bit.iso
```

## 参加方法 / How to Contribute
- Issue・Pull Request歓迎
- README, CONTRIBUTING, ISSUE/PULL_REQUESTテンプレートを参照
- 日本語・英語どちらでもOK

## ライセンス / License
GPL-2.0 (c) B-Free Project

## 参考資料 / References
- OS_IMPLEMENTATION_GUIDE.md
- ARM64_ドキュメント設計書拡充方針_20251227.md
- B-Free統合TODO_20251228.md

---

本READMEは日本語・英語併記です。詳細な設計書や進捗はdocs/以下にまとめていきます。
This README is bilingual (Japanese/English). For more details, see docs/.
