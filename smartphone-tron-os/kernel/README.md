# STOS カーネル bring-up

フェーズ A の入口。T-Kernel 2.0 本体の AArch64 フォークは未取込。
ここでは仕様 §5 の **Image ヘッダ・EL 降格・UART 初鳴き** だけを実装する。

```bash
make BOARD=qemu_virt
make BOARD=qemu_virt qemu    # timeout 付き。成功文字列は STOS: qemu OK
make BOARD=lena              # 実機 kexec 用 Image
```

| BOARD | UART | バナー |
|-------|------|--------|
| `qemu_virt` | PL011 `0x09000000` | `STOS: qemu OK` |
| `lena` | GENI uart9 `0x0098C000` TX FIFO | `STOS: lena OK` |
