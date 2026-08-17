# STOS BSP（Pattern A）

入力は Linux DTS。出力は C ヘッダ。フェーズ A ではカーネル内 FDT パーサを使わない。

| ファイル | 対象 |
|----------|------|
| `include/bsp_lena_pdx213.h` | Xperia 10 III（lena / PDX213 / SM6350） |
| `include/bsp_qemu_virt.h` | QEMU `-M virt`（Phase 2） |

## 出典

- `arch/arm64/boot/dts/qcom/sm6350.dtsi`
- `arch/arm64/boot/dts/qcom/sm6350-sony-xperia-lena-pdx213.dts`

`STOS_DRAM_SIZE` は DTS では 0（ブートローダ埋め）。6 GiB SKU を仮置き。実機で `/proc/iomem` を確認すること。

## ロードアドレス

仕様 §7 の `STOS_KERNEL_LOAD_BASE = 0x88000000` は **pil_cdsp_mem（0x86F00000–0x88D00000）に重なる**。
`STOS_KERNEL_LOAD_SAFE = 0xA4000000`（dfps の後ろ）を Phase 3 の代替 `--mem-min` にする。

検査:

```bash
python3 ../tools/check_bsp.py
```
