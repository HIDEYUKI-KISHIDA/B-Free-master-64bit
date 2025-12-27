# 64ビット版B-Free ビルド＆起動手順（QEMU用）

## 1. 必要なファイル
- boot64.S（btron-pc/boot64/）
- main.c（btron-pc/kernel64/）
- types.h（btron-pc/include64/）
- run_bfree64_qemu.ps1（ビルド・起動用スクリプト）

## 2. ビルド手順（例: Windows/MSYS2, Linux/WSL2）

### (1) アセンブラ・コンパイラの用意
- nasm, gcc などをインストール

### (2) ブートローダのビルド
```
cd btron-pc/boot64
nasm -f bin boot64.S -o boot64.img
```

### (3) カーネルのビルド
```
cd ../../kernel64
# 例: x86-64向けにビルド
x86_64-elf-gcc -ffreestanding -c main.c -o main.o
# リンカスクリプトがあればリンク
# x86_64-elf-ld -T linker.ld -o kernel64.elf main.o
```

### (4) QEMUで起動
```
powershell -ExecutionPolicy Bypass -File ../../run_bfree64_qemu.ps1
```

---

## 3. サンプルアプリ/API拡充例
- shell.cに新コマンド追加
- main.cにメモリ管理や割り込み初期化の雛形追加
- types.hに新しい型定義を追加

---

## 4. 注意
- ビルド・起動に失敗する場合は、パスやツールチェーン、ファイル名を再確認してください。
- 詳細はB-Free64_構想記録_20251227.mdやREADME_BFREE64.mdも参照。
