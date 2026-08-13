# B-Free aarch64

ARM64 (AArch64) 向け B-Free OS ツリー。x86_64 本線（`bfree_x86_64/`）完成後に段階的に実装する。

## ドキュメント

| ファイル | 内容 |
|----------|------|
| [docs/ANDROID_COMPAT_LAYER.ja.md](docs/ANDROID_COMPAT_LAYER.ja.md) | Android 互換層 設計書（Phase 0） |
| [docs/PORT_ROADMAP.ja.md](docs/PORT_ROADMAP.ja.md) | x86_64 → aarch64 移植ロードマップ |

## 開発パス（Windows / WSL）

```
C:\Users\h_kis\Desktop\B-Free-master\Program\bfree_aarch64\
/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_aarch64/
```

## 現状

- **設計フェーズ** — カーネル・ツールチェーン・互換層の骨格は未実装
- 参照実装: `../bfree_x86_64/`（compositor-first + musl guest + Qt6 Wayland）
