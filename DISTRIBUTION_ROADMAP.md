cd /c/Users/h_kis/Desktop/B-Free-master/Program/btron-pc/kernel# B-Free OS 64-bit - Modern Distribution Version
## BTRONマイクロカーネルアーキテクチャの現代的再実装

---

## プロジェクト概要
B-Free OSは、BTRON（Business TRON）の設計思想に基づいた、現代的な64ビットOSの再実装プロジェクトです。オリジナルのマイクロカーネルアーキテクチャとモジュラー設計を継承しながら、現代的なハードウェア対応と実用性を実現します。
## 直近の進捗 (2025-12-18)

### 現在の成果
- `C:\msys64\usr\bin\make -C make` に PATH を通す手順を整理し、Windows/MSYS2 環境でも安定して `itron.image` を生成できる状態になりました。
- `pmemory.c` から参照される `end` シンボルを保持するための [btron-pc/kernel/ITRON/common/end_marker.S](btron-pc/kernel/ITRON/common/end_marker.S) を追加し、リンク時の `end` 未定義問題を解消済みです。
- 最新成果物は [btron-pc/kernel/ITRON/make/itron.image](btron-pc/kernel/ITRON/make/itron.image) に配置されており、配布向けの動作検証だけが残っています。

### 残タスク（本日中）
1. `CHANGELOG`/NEWS 相当の差分を読み合わせて、ユーザー向けハイライトをピックアップする。
2. 成果物の配布手順（ビルドコマンドと必要ファイル一覧）を簡潔にまとめる。
3. `itron.image` の最終動作確認（ブート/エミュレータ）を行い 19:00 までに完了報告する。

### 作業記録 (2025-12-18)
- 15:55  `C:\msys64\usr\bin\make -C make` で [btron-pc/kernel/ITRON/make/itron.image](btron-pc/kernel/ITRON/make/itron.image) の生成を再確認。`PATH` に `C:\msys64\usr\bin` を追加してからビルドする手順を整理。
- 16:05  [btron-pc/kernel/ITRON/common/end_marker.S](btron-pc/kernel/ITRON/common/end_marker.S) を新設し、`end/_end/__end__` シンボルを `.bss` に常駐させて `pmemory.c` の依存を解消。対応に合わせて [btron-pc/kernel/ITRON/make/Makefile](btron-pc/kernel/ITRON/make/Makefile) と [btron-pc/kernel/ITRON/make/SFILES](btron-pc/kernel/ITRON/make/SFILES) を更新。
- 16:20  NEWS/ChangeLog を探索した結果、直近の NEWS ファイルは存在せず、最上位の [btron-pc/ChangeLog](btron-pc/ChangeLog#L1-L200) も 2000 年で停止していることを確認。今回の対応履歴は本ドキュメントに記録する方針を確定。
- 16:30  本ドキュメントへ進捗・作業ログを追記し、残タスクを明文化。以後の検証・配布準備で参照するベースラインとする。
- 17:05  `qemu-system-i386 -m 64 -cpu 486 -nographic -kernel itron.image` で直接起動を試行するも、ブートローダ不在のため `Booting from ROM..` で停止。`qemu-system-i386.exe` を `Stop-Process` で終了。→ 今後は `boot/` 配下の 1st/2nd ステージを含むイメージ生成（`create_bootable_img.ps1` か ISO スクリプト）が必要。
- 18:15  [btron-pc/kernel/ITRON/make/pack_module.ps1](btron-pc/kernel/ITRON/make/pack_module.ps1) を追加し、`objcopy`/`objdump`/`nm` を使って `itron.image` から `.text/.data/.rdata/.eh_fram/.idata/.reloc` を抽出 → `.bss` サイズを読み取って a.out ヘッダ（ZMAGIC/M_386, entry=`startup`）を 512B ブロックに整形 → [btron-pc/kernel/ITRON/make/itron.module](btron-pc/kernel/ITRON/make/itron.module) を生成（ヘッダ + 216,424 bytes, BSS=114,128 bytes）。このモジュールを 1st/2nd ブートが読める形式に組み込めば、既存 32bit ローダで ITRON を展開できる状態。


---

## BTRONの設計思想

### 1. マイクロカーネルアーキテクチャ
```
┌─────────────────────────────────┐
│   User Space Applications       │
│  (Init, Managers, Drivers)      │
├─────────────────────────────────┤
│  IPC (Message Passing)          │
├─────────────────────────────────┤
│   Microkernel (Minimal)         │
│  - Process/Task Management      │
│  - Memory Management            │
│  - Interrupt Handling           │
│  - IPC Framework                │
└─────────────────────────────────┘
```

### 2. 主要な特徴
- **プロセス間通信（IPC）** - メッセージパッシング方式
- **メモリ保護** - 各プロセスが独立したメモリ空間
- **リアルタイム性** - プリエンティブマルチタスク
- **モジュール性** - ドライバやマネージャを動的にロード可能

### 3. レイヤ構成（オリジナル）
```
BTRON層（汎用インターフェース）
  ↓
ITRON層（リアルタイム拡張）
  ↓
POSIX層（Unix互換性）
  ↓
マイクロカーネル（最小限の実装）
```

### POSIX互換性について
- 目標: ソースレベルの互換（POSIX libc準拠）を優先。`fork/exec/wait`, `pipe`, `select/poll`, `signals`, `mmap`, `pthread`, `fcntl`, `tty/pty` 等を提供し、`musl` または `newlib` の移植を前提に実装します。
- 非目標: Linuxバイナリ互換（ELF＋syscall ABI＋/proc 等）は当面対象外。完全互換には Linux カーネルABIの再実装が必要で現実的ではありません。
- 補足: Linux固有拡張（`epoll`, `inotify`, 多数の `ioctl` など）は段階的に対応範囲を選定し、必要に応じて互換ラッパや簡易 `procfs` の提供を検討します。


---

## 現在の実装状況

### ✅ 完了したコンポーネント

1. **64ビットブートローダー**
   - `start16.S` - 16ビット実モード
   - `start32.S` - 32ビット保護モード
   - `start64.S` - 64ビットロングモード
   - `bootable.img` - 動作確認済み

2. **カーネル初期化**
   - `main64.c` - エントリポイント
   - `gdt_idt_64.c` - GDT/IDT設定
   - `memory64.c` - メモリ管理
   - `page64.c` - ページング管理

3. **シェル・ユーザーインターフェース**
   - `shell.c` - コマンドシェル
   - `console.c` - コンソールI/O
   - 基本コマンド（help, echo, clear, uname, exit）

### ⚠️ 部分実装

- `keyboard.c` - キーボード入力（スタブ）
- `console.c/h` - console_getchar()（スケルトン）

### ❌ 未実装

1. **IPC（プロセス間通信）**
   - メッセージキュー
   - パイプ
   - ソケット

2. **プロセス管理**
   - マルチプロセス対応
   - プロセススケジューラ
   - コンテキストスイッチ

3. **ファイルシステム**
   - SFS（Simple File System）ドライバ
   - VFS（仮想ファイルシステム）

4. **デバイスドライバ**
   - IDE/ATAディスク
   - ネットワーク（NE2000）
   - シリアルポート（RS-232C）

5. **POSIX互換性**
   - POSIX libc
   - シグナル処理
   - パイプ・リダイレクト

---

## 配布可能なOSへのロードマップ

### フェーズ 1: コアカーネル（3-4週間）
**目標:** 動作するマイクロカーネルの完成

1. **プロセス管理の実装**
   ```c
   // kernel/process.h/c
   struct process {
       int pid;
       int parent_pid;
       unsigned int state;      // READY, RUNNING, BLOCKED
       unsigned long rsp;       // スタックポインタ
       unsigned long rip;       // 命令ポインタ
       struct mm_struct *mm;    // メモリマネージャ
       // ...
   };
   ```

2. **IPC（メッセージパッシング）の実装**
   ```c
   // kernel/ipc.h/c
   int send_message(pid_t to, struct msg *msg, int timeout);
   int recv_message(pid_t from, struct msg *msg, int timeout);
   ```

3. **スケジューラの実装**
   ```c
   // kernel/scheduler.h/c
   void schedule(void);
   void context_switch(struct process *old, struct process *new);
   ```

4. **割り込みハンドリングの強化**
   ```c
   // kernel/interrupt.h/c
   int register_irq_handler(int irq, void (*handler)(void));
   ```

### フェーズ 2: ドライバ・I/O層（3-4週間）
**目標:** 実用的なI/O機能の完成

1. **キーボードドライバ完成**
   ```c
   // drivers/keyboard/keyboard.c
   char get_keychar(void);
   int get_scancode(void);
   ```

2. **ディスクドライバ実装**
   ```c
   // drivers/ide/ide.c
   int read_sector(int drive, int sector, void *buf);
   int write_sector(int drive, int sector, void *buf);
   ```

3. **VFS（仮想ファイルシステム）実装**
   ```c
   // fs/vfs.h/c
   struct inode *vfs_lookup(const char *path);
   int vfs_read(struct inode *inode, void *buf, int size);
   ```

### フェーズ 3: ファイルシステム（2-3週間）
**目標:** SFSドライバの完成と基本的なファイル操作

1. **SFS（Simple File System）ドライバ**
   ```c
   // fs/sfs/sfs.c
   int sfs_read_inode(int inode_num, struct sfs_inode *inode);
   int sfs_read_file(struct inode *inode, int offset, void *buf, int size);
   ```

2. **ファイルディスクリプタ管理**
   ```c
   // fs/file.h/c
   struct file {
       struct inode *inode;
       int offset;
       int flags;
   };
   ```

3. **ディレクトリ操作**
   ```c
   // fs/dir.c
   int ls_dir(const char *path);
   int mkdir(const char *path);
   int rmdir(const char *path);
   ```

### フェーズ 4: ユーザーランドプログラム（2-3週間）
**目標:** 実用的なコマンド群と基本ユーティリティ

1. **シェル機能の拡張**
   ```c
   // user/shell/shell.c
   // - パイプ対応
   // - リダイレクト対応
   // - シェルスクリプト対応
   // - ジョブコントロール
   ```

2. **基本コマンドの実装**
   - `ls` - ファイル一覧
   - `cat` - ファイル表示
   - `cp` - ファイルコピー
   - `rm` - ファイル削除
   - `mkdir` - ディレクトリ作成
   - `cd` - ディレクトリ変更
   - `pwd` - 現在位置表示

3. **ユーティリティプログラム**
   - `init` - システム初期化
   - `login` - ログイン管理
   - `mount` - ファイルシステムマウント

### フェーズ 5: POSIX互換性・高度な機能（3-4週間）
**目標:** 配布可能な実用的なOS

1. **POSIX libc の実装**
   - 標準I/O関数（fopen, fread, fwrite等）
   - 文字列処理関数
   - メモリ管理関数

2. **プロセス制御**
   - `fork()` - プロセス生成
   - `exec()` - プログラム実行
   - `wait()` - プロセス終了待機

3. **パイプ・リダイレクト**
   - ` | ` パイプ演算子
   - `>`, `<` リダイレクト演算子

4. **シグナル処理**
   - SIGTERM, SIGKILL等
   - シグナルハンドラ登録

---

## ファイル構成（最終形）

```
bfree-64bit/
├── boot/                   # ブートローダ
│   ├── 1st/                # 第1段階（MBR）
│   ├── 2nd/                # 第2段階（ブートコード）
│   └── bootable.img        # ブート可能イメージ
│
├── kernel/                 # マイクロカーネル
│   ├── arch/x86_64/        # アーキテクチャ依存
│   │   ├── cpu.c
│   │   ├── interrupt.S
│   │   └── paging.c
│   ├── core/               # カーネルコア
│   │   ├── main.c
│   │   ├── process.c       # プロセス管理
│   │   ├── scheduler.c     # スケジューラ
│   │   ├── ipc.c           # IPC
│   │   ├── memory.c        # メモリ管理
│   │   └── interrupt.c     # 割り込み処理
│   └── lib/                # カーネルライブラリ
│       └── lib.c
│
├── drivers/                # デバイスドライバ
│   ├── console/            # コンソール
│   ├── keyboard/           # キーボード
│   ├── disk/               # ディスク
│   │   └── ide.c
│   ├── timer/              # タイマー
│   └── network/            # ネットワーク
│
├── fs/                     # ファイルシステム
│   ├── vfs.c               # 仮想FS
│   ├── sfs/                # Simple File System
│   │   ├── sfs.c
│   │   └── sfs.h
│   ├── file.c              # ファイル操作
│   └── inode.c             # inode管理
│
├── user/                   # ユーザーランド
│   ├── libc/               # C標準ライブラリ
│   │   ├── stdio.c
│   │   ├── stdlib.c
│   │   └── string.c
│   ├── bin/                # 基本コマンド
│   │   ├── shell.c
│   │   ├── ls.c
│   │   ├── cat.c
│   │   ├── cp.c
│   │   └── mkdir.c
│   ├── init/               # 初期化プログラム
│   │   └── init.c
│   └── login/              # ログイン
│       └── login.c
│
├── include/                # 共通ヘッダ
│   ├── types.h
│   ├── errno.h
│   ├── syscall.h
│   └── btron.h
│
├── Makefile                # ビルドスクリプト
├── README.md               # プロジェクト説明
├── INSTALL.md              # インストール方法
└── VERSION                 # バージョン情報
```

---

## 技術仕様

### CPU: x86-64
- **命令セット:** AMD64/Intel 64
- **特殊機能:**
  - PAE（Physical Address Extension）
  - NXE（No-Execute Enforcement）
  - 4KB ページング（2MBラージページ対応）

### メモリレイアウト
```
0x0000000000000000 - 0x0000000000001000  : IDT
0x0000000000001000 - 0x0000000000002000  : GDT
0x0000000000100000 - 0x0000000010000000  : カーネル領域
0xFFFF800000000000 - 0xFFFF800100000000  : カーネル仮想アドレス
```

### プロセスメモリ構造
```
0xFFFFFFFFFFFF0000 - 0xFFFFFFFFFFFFFFFF  : カーネルスペース
0x00007FFFFFFFE000 - 0x00008000000000   : スタック
0x0000000000500000 - 0x00007FFFFFFFE000 : ヒープ
0x0000000000010000 - 0x0000000000500000 : BSS, データセグメント
0x0000000000000000 - 0x0000000000010000 : テキストセグメント
```

### システムコール インターフェース

```c
// arch/x86_64/syscall.h
#define SYSCALL_EXIT        0
#define SYSCALL_FORK        1
#define SYSCALL_EXEC        2
#define SYSCALL_WAIT        3
#define SYSCALL_OPEN        4
#define SYSCALL_CLOSE       5
#define SYSCALL_READ        6
#define SYSCALL_WRITE       7
#define SYSCALL_SEEK        8
#define SYSCALL_STAT        9
#define SYSCALL_SEND_MSG   10
#define SYSCALL_RECV_MSG   11
```

---

## ビルド・テスト方法

### ビルド手順
```bash
# 準備
sudo apt-get install build-essential nasm qemu-system-x86 grub-mkrescue

# B-Free をクローン
git clone https://github.com/b-free/bfree-64bit.git
cd bfree-64bit

# ビルド
make clean
make all

# ブート可能なISOイメージ作成
make iso
```

### テスト実行
```bash
# QEMU での実行
make run

# または手動で
qemu-system-x86_64 -m 512 -cdrom bfree-64bit.iso
```

### 実ハードウェアでの起動
```bash
# USB フラッシュドライブに書き込み（Linux）
dd if=bfree-64bit.iso of=/dev/sdX bs=4M && sync

# USB フラッシュドライブから起動
```

---

## 配布形式

### ISO イメージ
- ハイブリッドISO（BIOS/UEFI両対応予定）
- サイズ： 10-20 MB

### ソースコード
- GitHub リポジトリ
- ライセンス： GPL-2.0
- ドキュメント完備

### インストーラ（将来）
- Live USB イメージ
- ハードディスクインストール対応

---

## 開発スケジュール

| フェーズ | 期間 | マイルストーン |
|---------|------|---------------|
| 1. コア | 3-4週 | マイクロカーネル動作 |
| 2. I/O | 3-4週 | ドライバ完成 |
| 3. FS | 2-3週 | ファイルシステム動作 |
| 4. ユーザ | 2-3週 | 基本コマンド完成 |
| 5. POSIX | 3-4週 | 配布可能 |

**予定リリース:** 2025年3月末（約3-4ヶ月）

---

## 参考情報

### オリジナルB-Freeプロジェクト
- https://github.com/b-free/bfree-src
- ライセンス： GPL-2.0

### BTRON仕様
- マイクロカーネルアーキテクチャ
- メッセージパッシングIPC
- プロセスメモリ保護

### 関連技術
- x86-64 アーキテクチャ
- ELF（実行可能形式）
- FAT32/ext4（マウント対応予定）

---

## まとめ

B-Free 64-bit は、BTRONの設計思想（マイクロカーネル、モジュール性、リアルタイム性）を継承しながら、現代的なハードウェア（64ビットx86-64）に対応した、配布可能なOSプロジェクトです。

段階的なフェーズアプローチにより、確実で品質の高い実装を目指します。

---

## 32-bit 2nd boot 起動画面（記録）


本リポジトリの 32bit セカンドブートは Windows/MSYS2 (MinGW32/PE) 環境でビルド・起動可能です。下記の手順で「No bootable disk」問題を回避し、正常な起動画面（B-Free 2nd bootのロゴやバージョン情報等）が表示されるISOを作成できます。

### 32bit版 正しいビルド・ISO作成・起動手順

1. **2ndbootのビルド**
   ```powershell
   cd c:\Users\h_kis\Desktop\B-Free-master\Program\btron-pc\boot\2nd
   make clean
   make USE_MINGW32=1 mode32
   make USE_MINGW32=1 2ndboot
   # 2ndbootファイルが生成されていることを確認
   ```

2. **ISOイメージの作成**
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\create_bootable_iso.ps1 -BootImagePath 2ndboot -OutputISO bfree-32bit-bootable.iso
   # bfree-32bit-bootable.iso が生成されていることを確認
   ```

3. **QEMUでの起動テスト**
   ```powershell
   qemu-system-i386 -m 64 -cdrom bfree-32bit-bootable.iso -boot d -vga std
   # 画面に「B-Free 2nd boot」等のロゴやバージョン情報が表示されれば正常
   # 「No bootable disk」等が出る場合は、2ndbootやISO作成手順を再確認
   ```

4. **スクリーンショット取得（自動）**
   - スクリプト: [btron-pc/boot/2nd/capture_boot_screenshot.ps1](btron-pc/boot/2nd/capture_boot_screenshot.ps1)
   - 保存先: [btron-pc/boot/2nd/screenshots](btron-pc/boot/2nd/screenshots)
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\capture_boot_screenshot.ps1 -ISO bfree-32bit-bootable.iso -Mem 64 -WaitSec 4
   ```

5. **一括自動（ビルド→起動→スクショ→PNG 変換まで）**
   - スクリプト: [btron-pc/boot/2nd/auto_capture_boot.ps1](btron-pc/boot/2nd/auto_capture_boot.ps1)
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\auto_capture_boot.ps1 -Rebuild -Mem 64 -WaitSec 10 -Control qmp -Display win32
   ```

**注意:**
- create_bootable_iso.ps1のデフォルトは64bit用なので、32bit用には必ず `-BootImagePath 2ndboot -OutputISO bfree-32bit-bootable.iso` を指定してください。
- 2ndbootが正しく生成されていない場合や、ISO作成時にファイル指定を誤ると「No bootable disk」になります。
- 正常な起動画面例は [btron-pc/boot/2nd/screenshots/boot-latest.png](btron-pc/boot/2nd/screenshots/boot-latest.png) を参照してください。

必要に応じてPPMをPNGへ変換して本ドキュメントへ貼り付けてください（例: ImageMagick `magick convert` など）。

---

## 将来のアプリ戦略とARM計画（暫定）

### アプリ戦略（互換の考え方）
- **方針:** バイナリ互換よりも「ファイル形式互換＋ソース互換」を重視。POSIX層の整備と一般的なOSSライブラリの移植でアプリ資産を取り込む。
- **GUIスタック:** まずはフレームバッファ＋SDL/ソフト描画から着手し、段階的にグラフィクス抽象（将来的にWayland相当）を検討。フォントはFreeType、文字処理はICU/harfbuzzの適用を視野。
- **ブラウザ:** 初期は依存の軽い [btron-pc/kernel/POSIX] 上の NetSurf 移植を目標。ネットワーク/スレッド/描画の基盤整備後、Firefox/Chromium級は長期目標。
- **オフィス:** 軽量の AbiWord/Gnumeric から段階的に開始。LibreOffice は依存が大きいためPOSIX・GUI・フォント周辺が安定してから検討。MS形式との互換はlibmspub/libwps/libvisio等のフィルタライブラリ活用を優先。
- **メーラー:** Claws Mail または Mutt を最初の候補。ネットワークスタックとTLS(例えばmbedTLS/OpenSSL)の連携が必須。

### 歴史的教訓（BTRONに学ぶ）
- **エコシステム:** アプリ数・開発者数の確保が鍵。POSIX互換と一般的OSSの受け入れで孤立を避ける。
- **ハード対応:** ドライバ/周辺機器対応を継続的に拡充。仮想環境(QEMU)での再現性確保と、実機ポーティングの両輪で進める。
- **開発者体験:** ビルドツール・パッケージ管理・ドキュメントの整備を並行して推進。

### ARMポート計画（32-bit安定後）
- **対象:** まずは ARMv7-A（Cortex-A系）→ 次に AArch64。QEMU `virt` を使った最小ブートで検証。
- **手順:** ブートコード（MMU/例外/割り込み）→ メモリ管理 → タイマ/コンソール → スレッド/IPC → ストレージ。
- **ツールチェーン:** `clang/gcc` のクロスビルド環境を用意し、x86_64 と同等のCI再現性を確保。

