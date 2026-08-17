# フェーズ A チェックリスト（v0.5.1 §12.2）

開始点: **Phase 1（BSP）**。Phase 0 の実機項目はクラウドでは不可。

## Phase 0 — 環境

- [x] `aarch64-linux-gnu-gcc` でカーネル bring-up をクロスビルドできる
- [ ] SoMainline が lena で起動（実機）
- [ ] adb root（実機）
- [ ] tk2 ソース取得（`tools/fetch_tkernel.sh`。公式ツリーは AArch64 ではない）

## Phase 1 — BSP

- [x] DTS 読解（sm6350.dtsi + lena pdx213）
- [x] reserved / hyp_mem をヘッダに転写
- [x] `bsp_lena_pdx213.h` の §8.1 シンボルが埋まっている
- [ ] `/proc/iomem` で DRAM サイズ確定（実機。仮置き 6 GiB）

## Phase 2 — QEMU

- [x] Linux ARM64 Image ヘッダ付きバイナリ
- [x] QEMU virt UART に `STOS: qemu OK`
- [ ] 公式 tk2 を virt 向けにビルド（bring-up が仮の入口）

## Phase 3 — kexec 初鳴き（次）

- [ ] Image を端末へ push
- [ ] `kexec -l` / `kexec -e`
- [ ] UART で `'S'` または `STOS: lena OK`

## Phase 4 / 5

- [ ] GICv3 + arch_timer heartbeat
- [ ] ヒープ alloc/free、hyp_mem 非接触
