# SMARTPHONE TRON OS — 仕様書

| 項目 | 内容 |
|------|------|
| 版 | **0.5.1（現実的 MVP・振り出し版）** |
| 日付 | 2026-08-13 |
| 前バージョン | —（本版は v0.5 系を継承せず、構想をゼロから再定義） |
| 対象読者 | OS カーネル担当、BSP 担当、ユーザ空間／UI 担当 |
| ステータス | 検討用・未承認 |
| 位置づけ | **本書は Smartphone TRON OS 単体の正本。x86_64 デスクトップ検証系とは独立。** |

---

## 本書の読み方

| 層 | 章 | 読む目的 |
|----|-----|----------|
| **結論** | §0 | 何を作るか・何を作らないか |
| **全体像** | §1〜§4 | 製品定義・アーキテクチャ・スコープ |
| **カーネル** | §5〜§8 | ARM64 ブート・BSP・例外・メモリ |
| **ユーザ空間** | §9〜§11 | アプリモデル・UI・配布 |
| **実行計画** | §12〜§14 | フェーズ・チェックリスト・リスク |
| **手順** | §15〜§16 | lena BSP 表・kexec 手順 |

**結論（1 行）:**  
**Xperia 10 III（lena）上で T-Kernel 2.0 系カーネルを動かし、ネイティブ ELF アプリと QML シェルで「触れるスマホ OS」まで到達する。** APK 互換・Linux バイナリ互換は目指さない。

---

## 0. 製品定義

### 0.1 Smartphone TRON OS とは

**Smartphone TRON OS（以下 STOS）** は、スマートフォン向けハードウェア上で動作する **TRON 系リアルタイム OS 製品** である。

- カーネル: **T-Kernel 2.0 / μT-Kernel 2.0 系（AArch64）**
- ハードウェア仕様の参照源: **mainline Linux Device Tree（DTS）**
- ユーザ空間: **STOS ネイティブ ELF**（静的リンク可）
- UI: **QML ベースのシェル** + 同梱アプリ数本

Linux カーネル上で動く Android でも、Linux ディストリビューションでもない。  
**「国産 RTOS カーネル + スマホ向け最小ユーザ空間」** を新規に定義する。

### 0.2 作らないもの（固定）

| 対象 | 理由 |
|------|------|
| Android APK / Play Store / GMS | Bionic + ART + Framework が必要。別 OS |
| Linux `.deb` / Flatpak / APK パッケージの実行 | Linux syscall ABI が必要 |
| フル KDE Plasma Mobile / KWin 移植 | Linux 前提・工数過大 |
| 量産向け Verified Boot / DRM 完全代替 | 初期フェーズのスコープ外 |
| VoLTE / SMS / 携帯モデム | ベンダー blob・キャリア契約が壁 |

### 0.3 MVP の定義（合格ライン）

| ID | 内容 | 合格基準 |
|----|------|----------|
| **MVP-1** | カーネル生存 | lena 実機 UART に `STOS: lena OK` |
| **MVP-2** | 画面 | 単色または Simple FB でピクセルが変わる |
| **MVP-3** | 入力 | タッチ 1 点で座標が取れる |
| **MVP-4** | シェル | QML ランチャー 1 本が起動し、同梱アプリ 1 個を起動できる |
| **MVP-5** | 配布 | ビルド手順 + kexec 手順 + 同梱 ELF が再現可能 |

**MVP 以降（任意）:** Wi-Fi、サスペンド、カメラ、ストア、多言語 8 言語 — すべて **Product v2**。

### 0.4 第 1 実機（決定）

| 項目 | 値 |
|------|-----|
| 端末 | Sony Xperia 10 III（PDX213） |
| コードネーム | **lena** |
| SoC | Qualcomm SM6350 |
| 選定理由 | SM8250 系より電源ドメインが単純。mainline DTS・SoMainline 実績あり |

---

## 1. 設計原則

1. **DTS をハードウェアの正とする** — レジスタ・IRQ・メモリは Linux DTS から抽出し TRON BSP に転写する。
2. **カーネルは TRON のまま** — Linux 互換 syscall 全面エミュは製品路線に含めない。
3. **ユーザ空間は薄く** — シェル + 同梱アプリ数本。ストアエコシステムは MVP 後。
4. **開発初期は kexec** — PMIC・クロックは Linux が先行初期化。製品目標は直接 boot。
5. **ソースから組み立てる** — 外部 OSS は FLOSS ソースを STOS 用に再ビルド。バイナリ流用はしない。
6. **1 人でも追える粒度** — 各フェーズに出口基準とチェックリストを置く。

---

## 2. 用語

| 用語 | 定義 |
|------|------|
| **STOS** | Smartphone TRON OS |
| **BSP** | ボード／SoC 固有の起動・MMIO・割り込み定義 |
| **DTS / DTB** | Device Tree Source / Binary |
| **Bring-up** | 実機でカーネルが UART 等で生存確認できる状態 |
| **パターン A** | DTS をオフライン解析し C ヘッダ（`bsp_*.h`）を生成（推奨・初期） |
| **パターン B** | カーネル内 FDT パーサで DTB を読む（将来） |
| **STOS Runtime** | ユーザ空間 ELF がカーネルと話すための syscall・libc・起動契約 |
| **STOS Shell** | QML ベースのランチャー／ホーム画面（`shell.elf`） |
| **同梱アプリ** | initrd または固定パスに置く STOS ネイティブ ELF |

---

## 3. 全体アーキテクチャ

### 3.1 レイヤ構成

```
┌─────────────────────────────────────────┐
│  同梱アプリ (settings.elf, clock.elf …)  │
├─────────────────────────────────────────┤
│  STOS Shell (shell.elf) — QML ランチャー  │
├─────────────────────────────────────────┤
│  UI 基盤 — Qt6 Core/Gui/Qml (静的リンク)  │
├─────────────────────────────────────────┤
│  STOS Runtime — musl + stos_syscall.h    │
├─────────────────────────────────────────┤
│  表示・入力 — FB 直書き → (将来) DSI     │
├─────────────────────────────────────────┤
│  T-Kernel 2.0 AArch64（tk2-lena フォーク）│
├─────────────────────────────────────────┤
│  BSP — bsp_lena_pdx213.h（DTS 由来）      │
└─────────────────────────────────────────┘
         Xperia 10 III (SM6350 / lena)
```

### 3.2 Linux の位置づけ

| 役割 | 説明 |
|------|------|
| **仕様書** | DTS の `reg` / `interrupts` / `compatible` を正とする |
| **参照実装** | GICv3、PSCI、GENI UART の Linux ドライバを読む |
| **開発用ブートストラップ** | kexec 前に PMIC・クロックを初期化 |

**製品要件として Linux 常駐は採用しない。**

### 3.3 ソフトウェア資産の扱い（4 層）

| 層 | 内容 | STOS での扱い | MVP |
|----|------|---------------|-----|
| **④ DT / ドライバ知識** | DTS、レジスタ、電源 | BSP に転写 | **○ 最優先** |
| **① ソース** | C/C++/QML の FLOSS | STOS 用再ビルド | **○** |
| **② バイナリ** | `.so` / ELF そのまま | 対象外 | × |
| **③ フレームワーク** | Bionic + ART + Android | 対象外 | × |

---

## 4. スコープ

### 4.1 スコープ内

- lena 向け最小 BSP と T-Kernel 2.0 AArch64 ポート
- kexec による開発 bring-up
- STOS Runtime（最小 syscall セット）
- QML シェル + 同梱アプリ 4〜6 個
- mainline DTS からのオフライン BSP 生成方針

### 4.2 スコープ外（初期）

- テレフォニー（VoLTE / SMS）
- Google Play 相当
- Android / Linux モバイル APK 互換
- クローズド GPU / WLAN ファームの完全制御（調査のみ可）
- 防衛調達・富岳連携・災害メッシュ等の **社会インフラ訴求**（別紙 vision 参照可）

---

## 5. ブートチェーン

### 5.1 開発初期（kexec）

```
Bootloader (unlocked)
  → SoMainline Linux (lena)
      → kexec -l stos.Image --dtb=lena.dtb
          → STOS カーネルエントリ (EL1)
              → UART: "STOS: lena OK"
```

**DTB:** Linux と同一 DTB を kexec に渡す。STOS カーネルは **パターン A** のため DTB を読まない（渡すだけ）。

### 5.2 ARM64 Image ヘッダ（64 バイト）

kexec 互換のため、カーネル先頭に Linux ARM64 Image ヘッダを置く。

| オフセット | 内容 |
|-----------|------|
| 0x00 | 分岐命令 → 実エントリ |
| 0x08 | TEXT_OFFSET（例: 0x80000） |
| 0x10 | イメージサイズ |
| 0x38 | マジック `"ARM\x64"` |

### 5.3 エントリ規約

| レジスタ | 意味 |
|----------|------|
| x0 | DTB 物理アドレス（保持のみ） |
| x1〜x3 | 0 |

起動直後の順序:

1. EL チェック（EL2 なら EL1 降格）
2. 割り込み全マスク
3. UART 初鳴き `'S'`
4. BSS クリア
5. MMU identity map
6. GICv3 初期化
7. arch_timer 開始
8. `STOS: lena OK` 出力
9. カーネル本体へ

### 5.4 製品寄り（将来）

kexec で MVP-1 達成後、同一 Image を `mkbootimg` で boot パーティションに書き込む試験を行う。

---

## 6. カーネル要件（AArch64）

### 6.1 ベース

- **T-Kernel 2.0 AArch64 ポート**（公開 tk2 AARCH64 等）を **lena 向けにフォーク**
- リポジトリ名（仮）: `stos-tk2-lena`

### 6.2 例外レベル

- エントリは EL1 を想定。EL2 で入った場合は HCR_EL2 / SPSR_EL2 / ELR_EL2 で EL1 に降格してから続行。

### 6.3 GICv3（SM6350）

| 項目 | 値 |
|------|-----|
| GICD | 0x17A00000 |
| GICR CPU0 | 0x17A60000 |
| GICR stride | 0x20000 × CPU index |
| フェーズ A タイマ | arch_timer PPI **30**（Physical Non-Secure） |
| cntfrq | 19,200,000 Hz |

初期化: GICD 有効化 → GICR WAKER 解除 → ICC_SRE → タイマ PPI 有効化。

### 6.4 MMU

- フェーズ A: **identity mapping**
- Device-nGnRnE: GIC / UART MMIO
- Normal WB: DRAM（reserved 除外後）
- **hyp_mem 0x80000000–0x805FFFFF はマップ禁止**

### 6.5 UART（GENI debug）

| 項目 | 値 |
|------|-----|
| ベース | 0x0098C000（uart9 / geni-debug-uart） |
| 方式 | kexec 後は Linux 初期化済み前提の **TX FIFO ポーリング** |

### 6.6 マルチプロセッサ

- フェーズ A: **CPU0 のみ**
- セカンダリ CPU はフェーズ A 完了後

---

## 7. メモリマップ（lena）

| 領域 | アドレス | 備考 |
|------|----------|------|
| hyp_mem | 0x80000000–0x805FFFFF | **触禁止** |
| その他 reserved | DTS / `/proc/iomem` で確認 | ヒープ・リンクから除外 |
| **カーネルロード推奨** | **0x88000000** | reserved 回避 |
| ユーザ／ヒープ | 0x90000000〜 | 十分な空き RAM |

実機確認:

```bash
grep -iE 'System RAM|reserved' /proc/iomem
```

---

## 8. BSP 生成

### 8.1 パターン A（推奨）

**入力:** `sm6350.dtsi` + `sm6350-sony-xperia-lena-pdx213.dts`  
**出力:** `bsp_lena_pdx213.h`

必須シンボル:

```c
#define STOS_DRAM_BASE           0x80000000ULL
#define STOS_DRAM_SIZE           /* /proc/iomem で確定 */
#define STOS_KERNEL_LOAD_BASE    0x88000000ULL
#define STOS_GICD_BASE           0x17A00000ULL
#define STOS_GICR_BASE(cpu)      (0x17A60000ULL + (cpu) * 0x20000ULL)
#define STOS_UART_DBG_BASE       0x0098C000ULL
#define STOS_TIMER_PNSIRQ        30U
#define STOS_TIMER_CLK_HZ        19200000U
#define STOS_CPU_COUNT           8U
```

### 8.2 パターン B（将来）

対応機種が 3 機種以上かつパターン A で運用実績ができた後に検討。

### 8.3 ツール

`dts2stos-bsp`（未実装）— 手動生成でも可。Phase 1 出口基準は **ヘッダの値が埋まっていること**。

---

## 9. STOS Runtime（ユーザ空間）

### 9.1 プロセスモデル

- **1 アプリ = 1 ELF**（静的リンク推奨）
- カーネルが `exec` 相当で起動。シェルはランチャーから子プロセスとして起動。
- MVP では **単一アドレス空間に近い簡易モデル** も可（シェル内蔵アプリ）。**Product v1.1 でマルチプロセス化**。

### 9.2 最小 syscall セット（MVP）

| # | syscall | 用途 |
|---|---------|------|
| 0 | exit | 終了 |
| 1 | write | UART / ログ |
| 2 | read | 入力（将来） |
| 3 | open / close | ファイル（initrd） |
| 4 | fb_map | フレームバッファ mmap |
| 5 | fb_flip | 表示更新 |
| 6 | input_poll | タッチ座標 |
| 7 | exec | 子 ELF 起動 |
| 8 | clock_gettime | 時計 |

**Linux syscall 番号との互換は意図しない。** 番号は STOS 専用 ABI。

### 9.3 C ライブラリ

- **musl** をベースに STOS 用にクロスビルド
- ヘッダ: `stos_syscall.h`, `stos_fb.h`, `stos_input.h`
- POSIX 全面実装は **対象外**。必要なら libc 拡張を段階追加。

### 9.4 起動ファイル（crt0）

- スタック設定 → BSS クリア → `main()` → `exit`
- カーネルが ELF をロードしエントリを呼ぶ

---

## 10. UI とアプリ

### 10.1 UI 方針

| 項目 | 決定 |
|------|------|
| ツールキット | **Qt 6**（Core / Gui / Qml）静的リンク |
| シェル | QML 単一フルスクリーン（`shell.elf`） |
| 参考 UX | 一般的なモバイルランチャー（グリッド／ドック） |
| 非採用 | Plasma Mobile バイナリ、Android Material 互換 API |

### 10.2 表示パス（段階）

| 段階 | 方式 |
|------|------|
| MVP-2 | **フレームバッファ直書き**（Simple FB または SoC 固有 FB） |
| Product v1.1 | DSI パネルドライバ |
| 将来 | ゲスト内コンポジタ（マルチウィンドウ需要時） |

### 10.3 入力

| 段階 | 方式 |
|------|------|
| MVP-3 | I2C タッチコントローラ（DTS から IRQ・レジスタ特定） |
| 将来 | 物理キー（電源・音量）GPIO |

### 10.4 同梱アプリ（MVP-4）

| アプリ | 役割 |
|--------|------|
| `shell.elf` | ランチャー |
| `settings.elf` | 時刻・言語・明るさ（最小） |
| `clock.elf` | 時計 |
| `about.elf` | バージョン・カーネル情報 |
| （任意）`notes.elf` | メモ帳デモ |

配布: **initrd に ELF を同梱**。ランチャーが固定パス `/apps/*.elf` を exec。

### 10.5 エコシステム段階

| 段階 | 内容 |
|------|------|
| 0 | カーネル生存のみ |
| 1（MVP） | 同梱 4〜6 ELF |
| 2 | `adb push` によるサイドロード + 簡易 manifest |
| 3（将来） | `.stos-pkg` 形式・SDK テンプレ |

---

## 11. ビルド環境

### 11.1 ホスト

- Ubuntu 22.04+ / WSL2
- 実機: lena + SoMainline + adb root

### 11.2 クロスツールチェーン

| 用途 | ツールチェーン |
|------|----------------|
| カーネル（tk2-lena） | `aarch64-linux-gnu-` |
| ユーザ ELF（Runtime + Qt） | `aarch64-none-elf-` または musl 付き aarch64 クロス（要選定・Phase B 前に固定） |

**本書時点では x86_64 用ツールチェーンに依存しない。** AArch64 用を STOS リポジトリ内で定義する。

### 11.3 主要リポジトリ（仮構成）

```
stos/
├── kernel/          … tk2-lena フォーク
├── bsp/             … bsp_lena_pdx213.h, dts2stos-bsp
├── runtime/         … musl, crt0, stos_syscall
├── shell/           … QML シェル
├── apps/            … settings, clock, …
├── tools/           … kexec 手順、initrd 生成
└── docs/            … 本仕様書
```

---

## 12. フェーズとロードマップ

### 12.1 フェーズ一覧

| フェーズ | 名称 | 出口 |
|----------|------|------|
| **A** | カーネル生存 | MVP-1 |
| **B** | 表示・入力 | MVP-2, MVP-3 |
| **C** | シェル | MVP-4 |
| **D** | 配布・手順 | MVP-5 |

### 12.2 フェーズ A — カーネル生存

| ID | 成果 | 合格基準 |
|----|------|----------|
| A-1 | UART | `STOS: lena OK` |
| A-2 | タイマ | 10ms heartbeat ログ |
| A-3 | メモリ | ヒープ alloc/free |
| A-4 | GIC | PPI 30 で割り込み |
| A-5 | reserved | hyp_mem 非接触 |

**概算:** 経験者 1 人で **2〜4 週間**（並行作業・デバッグ含む）。

#### Phase 0 — 環境（1〜2 日）

- [ ] SoMainline が lena で起動
- [ ] adb root
- [ ] `aarch64-linux-gnu-gcc` 導入
- [ ] tk2 AARCH64 ソース取得

#### Phase 1 — BSP（1〜2 日）

- [ ] DTS 取得・読解
- [ ] `/proc/iomem` で DRAM / reserved 確定
- [ ] `bsp_lena_pdx213.h` 完成

#### Phase 2 — QEMU（2〜3 日）

- [ ] tk2 を virt 向けビルド
- [ ] Image ヘッダ追加
- [ ] QEMU UART に文字列出力

#### Phase 3 — kexec 初鳴き（3〜5 日）

- [ ] Image を端末へ push
- [ ] `kexec -l` / `kexec -e`
- [ ] **'S' または `STOS: lena OK` を UART で確認** ← 最重要

#### Phase 4 — タイマ + GIC（3〜5 日）

- [ ] §6.3 シーケンス実装
- [ ] heartbeat ログ

#### Phase 5 — メモリ監査（1〜2 日）

- [ ] A-3, A-5 確認 → **フェーズ A 完了**

### 12.3 フェーズ B — 表示・入力

| サブ | 内容 | 概算 |
|------|------|------|
| B-1 | フレームバッファ | 2〜4 週 |
| B-2 | タッチ（I2C） | 2〜4 週 |
| B-3 | ストレージ read-only | 4〜8 週（MVP 後回し可） |
| B-4 | 物理キー GPIO | 1〜2 週 |
| B-5 | サスペンド基礎 | 3〜6 週（MVP 後回し可） |
| B-6 | Wi-Fi | 長期（blob 依存） |

**MVP 必須:** B-1, B-2 のみ。

### 12.4 フェーズ C — シェル

| サブ | 内容 |
|------|------|
| C-1 | musl + crt0 + 最小 syscall |
| C-2 | Qt6 AArch64 クロスビルド（Gui + Qml） |
| C-3 | FB 用 QPA プラグイン（STOS 専用） |
| C-4 | `shell.elf` + `settings.elf` |
| C-5 | ランチャーから exec でアプリ起動 |

### 12.5 フェーズ D — 配布

- initrd 生成スクリプト
- kexec 手順書（§16）
- ビルド README
- 既知の制限一覧

---

## 13. 並行開発の指針

STOS は **単一リポジトリ内で完結** させる。外部 x86 デスクトップ検証系への依存は **必須としない**。

| トラック | 内容 | 依存 |
|----------|------|------|
| **K** | カーネル + BSP + kexec（フェーズ A） | なし |
| **U** | Runtime + Qt + シェル（フェーズ C） | フェーズ A 完了後が理想。QEMU 上で先行開発も可 |
| **I** | 表示・入力 BSP（フェーズ B） | フェーズ A と部分並行可 |

**推奨:** 週単位で K と I を進め、A 完了後に U を実機統合。

---

## 14. リスク

| リスク | 深刻度 | 緩和 |
|--------|--------|------|
| hyp_mem 誤アクセス | 高 | リンクスクリプトで 0x88000000 以降のみ |
| GENI UART 不一致 | 中 | Linux 起動中に stdout-path / iomem 再確認 |
| kexec 即再起動 | 中 | mem-min/max を iomem に合わせる |
| Qt AArch64 クロス | 中 | フェーズ C 前にホスト Qt + 最小 QML で設計固定 |
| GPU / WLAN blob | 低（MVP） | FB + UART のみで MVP 成立 |

---

## 15. 付録 A — lena DTS→BSP 対応表

| # | DTS | STOS シンボル | 値 |
|---|-----|---------------|-----|
| 1 | cpu count | STOS_CPU_COUNT | 8 |
| 2 | intc @ 17a00000 | STOS_GICD_BASE | 0x17A00000 |
| 3 | GICR | STOS_GICR_BASE(n) | 0x17A60000 + n×0x20000 |
| 4 | timer PPI 30 | STOS_TIMER_PNSIRQ | 30 |
| 5 | uart9 @ 98c000 | STOS_UART_DBG_BASE | 0x0098C000 |
| 6 | memory | STOS_DRAM_BASE / SIZE | 0x80000000 / 実機確認 |
| 7 | hyp_mem | — | 使用禁止 |

---

## 16. 付録 B — kexec 手順（lena）

### 16.1 前提

- Xperia 10 III、bootloader unlock
- SoMainline 起動済み、root

### 16.2 実行例

```bash
adb push stos/Image /data/local/tmp/stos.Image
adb push lena.dtb /data/local/tmp/lena.dtb

adb shell su -c '
  kexec -l /data/local/tmp/stos.Image \
    --dtb=/data/local/tmp/lena.dtb \
    --command-line="" \
    --mem-min=0x88000000 \
    --mem-max=0x200000000
  sync
  kexec -e
'
```

`--mem-max` は `/proc/iomem` の System RAM 上限に合わせて調整（6GB 機種は 32bit 上限 4GB では不足）。

### 16.3 失敗時

| 症状 | 確認 |
|------|------|
| 即再起動 | mem-min/max と reserved 衝突 |
| ログなし | UART ベース、EL 降格 |
| 'S' のみ | MMU / ページテーブル |

---

## 17. 改訂履歴

| 版 | 日付 | 内容 |
|----|------|------|
| **0.5.1** | 2026-08-13 | **振り出し版。** x86_64 検証系非依存。STOS 単体アーキテクチャ・Runtime・フェーズ・MVP 定義を新規記述 |

---

*本文書は検討用ドラフトです。実機試験の結果により更新する。*
