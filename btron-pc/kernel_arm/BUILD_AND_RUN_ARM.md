# B-Free ARMカーネル ビルド・起動手順（2025-12-27）

## 1. 必要なツール
- ARMクロスコンパイラ（例: arm-none-eabi-gcc, binutils）
- make
- QEMU（エミュレータ）または実機（Raspberry Pi等）

## 2. ビルド手順
```sh
cd kernel_arm
make
```
- kernel_arm.img が生成されます

## 3. QEMUでの起動例
```sh
qemu-system-arm -M raspi2 -kernel kernel_arm.img -serial stdio
```
- QEMUのバージョンやターゲットボードに応じて -M オプションを調整してください

## 4. 実機での起動例
- SDカード等に kernel_arm.img を配置し、ブートローダ設定に従って起動

## 5. ユーザランドアプリのビルド例
```sh
cd userland_arm
$(CROSS_COMPILE)gcc -O2 -nostdlib -ffreestanding -I../include_arm hello_arm.c -o hello_arm.elf
$(CROSS_COMPILE)objcopy -O binary hello_arm.elf hello_arm.bin
```

---

詳細な移植・起動方法は今後の進捗に応じて追記します。
