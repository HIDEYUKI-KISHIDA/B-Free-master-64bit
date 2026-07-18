# T-Kernel 2.0 x86_64 Porting Specification v2.5
BFREE OS / POSIX2017 / Qt/QML GUI 統合版

作成者：Hideyuki
日付：2026-04-XX
版数：v2.5

---

## 1. 目的・背景
T-Kernel 2.0をx86_64アーキテクチャへ移植し、リアルタイムOS基盤（Layer 0）、POSIX2017サブシステム（Layer 1）、GUIサブシステム（Layer 2）を統合した実用OSを構築する。BFREE ShellをOS操作のフロントエンドとし、デスクトップ/モバイル両対応の現代的なユーザインターフェースを実現する。

## 2. 全体アーキテクチャ
### 2.1 3層構造
- Layer 0: T-Kernelコア（タスク管理、メモリ管理、IPC、割込み、タイマ、デバイス管理）
 Layer 1: POSIX2017サブシステム（musl libcベース、API 682.40個が未実装［実装進捗608/688］）

### 2.2 サブシステム初期化テーブル

## 3. CPU / 割込み / タイマ（x86_64）
 musl libcベース、API 688個中、残り80個が未実装（実装進捗608/688）、syscall_x86_64.cでT-Kernel APIへマッピング
## 4. メモリ管理
- 4レベルページング（PML4/PDPT/PD/PT）、カーネル仮想アドレス空間、ページフォルトハンドラ、buddy allocator
 fork/exec、ネットワーク、KWin互換、Plasma Mobile最適化、POSIX API残り80個（API総数688）、デバイスサーバ/IPCサーバ分離
## 5. タスク管理・コンテキストスイッチ
- RAX〜R15, RIP, RSP, RFLAGS保存/復元、TCBのx86_64対応、Design-Aスイッチ方式

## 6. POSIX2017サブシステム
- musl libcベース、API 688個の実装状況、syscall_x86_64.cでT-Kernel APIへマッピング
- スレッド/タイマ/ファイル/シグナルのT-Kernel上での実装、互換性テスト

## 7. GUIサブシステム
- KDE Plasma/QML資産活用、Dolphin/Konsole/System Settings/Discover/Plasmoid等の統合
- QML部品の拡張性・POSIX/UNIXアプリ連携ラッパー

## 8. Shellサブシステム
- shell.c（shell_main）によるコマンド受付、GUI（Konsole）やDolphin等からの直接操作

## 9. ブート方式
- UEFI/GRUB2（Multiboot2）両対応、カーネルELF64、Long Mode移行

## 10. デバイスドライバ
- UEFI GOP（フレームバッファ）、キーボード、マウス、APIC、HPET

## 11. テスト・互換性検証
- T-Kernel APIテスト（タスク生成・メモリ割当・メッセージバッファ等）、musl libcテスト資産流用によるPOSIX API互換性チェック、GUIテスト

## 12. 今後の課題
- fork/exec、ネットワーク、KWin互換、Plasma Mobile最適化、POSIX API残り80個、デバイスサーバ/IPCサーバ分離

## 付録
- BFREE Shell統合リスト、BFREE GUI実装プラン、T-Kernel 2.0 x86_64仕様書、サンプルコード（IDT, PML4, interrupt.S など）

---

# コア設計・現状成果物の精査ポイント

- Program/bfree_x86_64配下のカーネル・サブシステム・ユーザランド資産を最大限活用し、T-Kernel2.0の基本OS機構（タスク・メモリ・IPC・デバイス・VFS・シェル）を整備
- Program/x86_64配下のQML/GUI資産・サンプル・KDE/Plasma部品をGUIサブシステムとして統合
- 32bit資産のアルゴリズム・API設計を流用し、CPU依存部のみx86_64用に再設計
- サブシステム初期化テーブルで依存関係・初期化順序を明確化
- POSIX/musl libc/GUI資産を段階的に統合し、wrapper層でアーキテクチャ差分を吸収
- テスト・互換性検証を徹底し、ドキュメント・設計書も一元化
- 今後の拡張（ネットワーク・デバイスサーバ等）を見据えたモジュール分割・API設計
