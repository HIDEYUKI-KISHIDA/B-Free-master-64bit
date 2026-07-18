# T-Kernel 2.0 x86_64 ポーティング仕様書 (v2.4 統合完全網羅版)

## プロジェクト: Bfree (BTRON) 64-bit 移行

| 項目 | 内容 |
|------|------|
| **Document ID** | TK2-PORT-x86_64-005 |
| **Date** | 2026-03-31 |
| **Revised** | 2026-04-01 (v2.4 統合: 前版全誤り訂正・全22ファイル完全網羅) |
| **Target** | T-Kernel 2.0 → x86_64 Long Mode |
| **前版** | v2.3 Final |
| **対象ディレクトリ** | `cpu/x86_64`（10ファイル）、`device/x86_64`（12ファイル） |

---

## 改版履歴

| バージョン | 日付 | 変更内容 |
|-----------|------|---------|
| v2.3 | 2026-03-31 | 初版（アーキテクチャ概要・ブートシーケンス・T_REGS 定義） |
| v2.4 | 2026-04-01 | **[本版]** 下記をすべて統合: ブートシーケンス誤り訂正、コンパイラフラグ統一（`-m64`・`-fno-stack-protector`・`-fno-pic` 追加）、T_REGS コメント明確化、CPU 抽象層追加（GDT/IDT/TSS/MSR/SYSCALL/FPU）、APIC 詳細（レジスタ定数・REDTBL 構造体）、HPET/LAPIC タイマー、コンテキストスイッチ詳細、メモリマップ、全22ファイル仕様を追加、下記 10 件の誤りを訂正: `INVD` 誤用・IOPL/CPL 混同・SYSCALL/SYSRET 役割誤記・Long Mode 移行責任箇所重複・アセンブラ指定曖昧・PDP→PDPT 表記・device ファイル数不整合・RDTSC キャリブレーション・S3/S4 実装難易度・BIOS/ACPI パッチ混同 |

---

## 訂正した主要誤り（v2.3 との差分）

| # | ファイル | 誤り内容 | 訂正内容 |
|---|---------|---------|---------|
| 1 | `cache.c` | `INVD` 命令を使用（書き戻しなしでキャッシュ破棄→データ破壊） | `INVD` 削除。`WBINVD` のみ使用 |
| 2 | `chkplv.c` | RFLAGS.IOPL で CPL を判定 | CPL = CS セレクタ[1:0]。IOPL は I/O 権限で別物 |
| 3 | `cpu_calls.c` | `sysret` でシステムコール「実装」と記述 | SYSCALL=ring3→ring0 エントリ、SYSRET=ring0→ring3 リターン |
| 4 | `cpu_init.c` | Long Mode 移行処理を担当と記述 | `icrt0.S` の責務。`cpu_init.c` は移行後の初期化のみ |
| 5 | `cpu_support.S` | NASM/YASM も選択肢として列挙 | GAS (AT&T 構文) に統一 |
| 6 | 全体 | PDP（Page Directory Pointer）と表記 | 正式名称は PDPT (Page Directory Pointer Table) |
| 7 | `device/x86_64` | 12ファイルと称して 11 ファイルのみ記載 | `memory.c`（E820 管理）を追加し 12 ファイルに |
| 8 | `cntwus.c` | RDTSC 単体でウェイト実装と記述 | RDTSC 前に PIT/HPET で周波数キャリブレーション必須 |
| 9 | `power.c` | S3/S4/S5 を同列に記述 | 初期移植は S5 のみ。S3/S4 は ACPI AML 実装が前提 |
| 10 | `patch.c` | BIOS/UEFI/ACPI パッチを混同 | 対象は ACPI DSDT バグ回避のみ |

---

## 1. Copilot 向けグローバルコンテキスト

**重要**: 以下のブロックをすべての Copilot プロンプト冒頭に貼り付けること。

```
* Mode          : x86_64 Long Mode (64-bit)
* ABI           : System V AMD64 ABI (引数順: RDI, RSI, RDX, RCX, R8, R9)
* Memory Layout : 物理 0x100000 (1 MB) 起点、Identity-mapped (2 MB ページ × 512)
* Compiler Flags: -m64 -mcmodel=large -mno-red-zone -fno-stack-protector
                  -fno-pic -ffreestanding -nostdlib
* Assembler     : GAS (GNU Assembler / AT&T 構文)。NASM/YASM は使用しない
* Stack Align   : call 直前の RSP は 16-byte aligned。callee エントリ時は aligned-8
                  (knl_kernel_stack_top 自体を 16-byte aligned に定義すること)
* Context Switch: "Design A" — C ハンドラリターン直後に interrupt.S 内でディスパッチ
* Struct Match  : T_REGS メンバは interrupt.S の pushq シーケンスの完全逆順
* Interrupt Ctrl: Local APIC + I/O APIC 使用。8259A PIC は必ず無効化
* Timer         : HPET (第一選択) → Local APIC Timer (フォールバック)
* FPU/SSE       : コンテキストスイッチ時に XSAVE/XRSTOR で保存。64-byte aligned
* CPL 判定      : CS セレクタ[1:0] が CPL。RFLAGS.IOPL は I/O 権限であり CPL とは別物
* ページテーブル : PML4 → PDPT → PD → PT（4階層。"PDP" ではなく "PDPT"）
* RDTSC 使用時  : 初回は PIT/HPET で周波数キャリブレーションを行ってから使用
```

---

## 2. ビルド環境

**ファイル**: `${TK_SRC}/sysdepend/x86_64/makefile.common`

```makefile
CC      = x86_64-elf-gcc
AS      = x86_64-elf-gcc   # GAS 使用。gcc -c でアセンブル
LD      = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy

CFLAGS  = -m64 \
          -mno-red-zone \
          -mcmodel=large \
          -fno-stack-protector \
          -fno-pic \
          -ffreestanding \
          -nostdlib \
          -Wall \
          -Wextra \
          -Werror=implicit-function-declaration

ASFLAGS = --64

LDFLAGS = -melf_x86_64 \
          -T $(TK_SRC)/sysdepend/x86_64/kernel.lnk \
          --no-undefined
```

> **`-fno-pic` 追加理由**: `-mcmodel=large` との組み合わせで PIC/PIE を明示的に無効化。
> ツールチェーンによってはデフォルト `-fpic` が有効なため、明示しないとリロケーション問題が起きる。

---

## 3. ブートシーケンスとページング

**ファイル**: `${TK_SRC}/kernel/sysmain/sysdepend/x86_64/reset.S`（または `device/x86_64/icrt0.S` — §6.4 参照）

### ページテーブル配置規則

`boot_pml4`・`boot_pdpt`・`boot_pd` は **`.data` セクション**に定義すること。

> **`.bss` に配置してはならない**。`.bss` はリンカが「ゼロ初期化済み」として最適化するか、
> ブートコードの BSS クリア（ステップ 8）でゼロ化される。ページテーブルは CR3 ロード前（ステップ 3）
> に非ゼロ値が必要なので、明示的な初期化子とともに `.data` に置くこと。

### 4 階層ページテーブル構成

```
PML4[0] → PDPT[0] → PD[0..N]  各 PD エントリで 2 MB をカバー（PS ビット使用）

エントリフラグ:
  PD   エントリ: 物理アドレス | PS(bit7=1) | RW(bit1) | P(bit0)
  PML4/PDPT    : 物理アドレス | RW(bit1)   | P(bit0)
```

> **"PDP" は誤り。正式名称は PDPT** (Page Directory Pointer Table)。

### Long Mode 移行フロー

前提: 32-bit Protected Mode（CR0.PE = 1）が既に有効であること（GRUB 等が設定）。

```
1.  cli
2.  lgdt [boot_gdt_ptr]        ; L=1, D=0 の 64-bit コードセグメント記述子を含む GDT
3.  mov  eax, [boot_pml4_phys]
    mov  cr3, eax              ; CR3 ← PML4 物理アドレス
4.  mov  eax, cr4
    or   eax, (1 << 5)         ; CR4.PAE (bit 5) = 1
    mov  cr4, eax
5.  mov  ecx, 0xC0000080       ; MSR_EFER
    rdmsr
    or   eax, (1 << 8)         ; EFER.LME (bit 8) = 1
    wrmsr
6.  mov  eax, cr0
    or   eax, (1 << 31)        ; CR0.PG (bit 31) = 1
    mov  cr0, eax              ; → Long Mode Compatibility Submode（CR0.PE は既に 1）
7.  ljmp $0x08, $long_mode_entry   ; CS = 0x08 (64-bit code seg) → Full 64-bit Mode
--- ここから 64-bit コード ---
8.  xor  eax, eax
    lea  rdi, [_bss_start]
    lea  rcx, [_bss_end]
    sub  rcx, rdi
    rep  stosb                 ; BSS クリア（.data のページテーブルは影響なし）
9.  lea  rsp, [knl_kernel_stack_top]  ; 16-byte aligned であること
    call knl_main              ; call 後 RSP = aligned-8（SysV ABI 準拠）
```

---

## 4. レジスタ保存構造体（T_REGS）

**ファイル**: `${TK_SRC}/include/tk/sysdef_depend.h`

### pushq シーケンスと構造体メンバの対応

```
pushq 実行順（高アドレス → 低アドレス）:
  rdi, rsi, rdx, rcx, rbx, rax, r15..r8, rbp
  [CPU 自動プッシュ: ss, rsp, rflags, cs, rip]

構造体メンバ順（低アドレス → 高アドレス = pushq の完全逆順）:
  rax, rbx, rcx, rdx, rsi, rdi, r8..r15, rbp, rip, cs, rflags, rsp, ss
```

```c
typedef struct {
    /*
     * ソフトウェアプッシュ領域（pushq の逆順）
     * pushq 順: rdi, rsi, rdx, rcx, rbx, rax, r15..r8, rbp
     * 構造体順: rax, rbx, rcx, rdx, rsi, rdi, r8..r15, rbp  ← 逆順
     */
    uint64_t rax, rbx, rcx, rdx, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rbp;   /* pushq %rbp は汎用レジスタ群の最後 */

    /*
     * CPU ハードウェア割り込みフレーム（自動プッシュ）
     * cs/ss は 16-bit セレクタを 64-bit にゼロ拡張した値
     */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;   /* CPL 変化時のみ有効 */
    uint64_t ss;    /* CPL 変化時のみ有効 */
} T_REGS; /* 20 × 8 = 160 バイト、16-byte aligned ✓ */
```

### 例外エラーコード処理

一部の例外（#PF, #DF, #GP 等）では CPU が RIP より前にエラーコード（8バイト）を自動プッシュする。
`T_REGS` にはエラーコードフィールドを持たせない設計とする。

```asm
/* エラーコードなし例外スタブ */
isr_no_err_stub:
    pushq $0              /* ダミー（スタック位置合わせ） */
    pushq $VECTOR_NUM
    jmp   common_isr_entry

/* エラーコードあり例外スタブ（CPU が自動プッシュ済み） */
isr_err_stub:
    pushq $VECTOR_NUM
    jmp   common_isr_entry

common_isr_entry:
    pushq %rbp
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    /* C ハンドラ: void knl_isr(T_REGS *regs, int vector, uint64_t errcode) */
    movq  %rsp,        %rdi
    movq  160(%rsp),   %rsi   /* vector */
    movq  168(%rsp),   %rdx   /* errcode */
    call  knl_isr_handler
```

---

## 5. CPU 抽象層

> **背景**: ARM 系ポートでは `cpu_depend.h/c` に CPU 固有処理が集約されている。
> x86_64 でも同様のファイル群が必要。ARM の GIC → x86_64 の APIC、
> ARM の CP15 → x86_64 の MSR、ARM の SVC → x86_64 の SYSCALL に相当する。

### 5.1 GDT（Global Descriptor Table）

**ファイル**: `${TK_SRC}/sysdepend/x86_64/gdt.c` / `gdt.h`

#### GDT エントリ構成

| インデックス | セレクタ | 種別 | 設定 |
|------------|---------|------|------|
| 0 | 0x0000 | Null descriptor | 必須 |
| 1 | 0x0008 | 64-bit Kernel Code | L=1, D=0, DPL=0, S=1, Type=0xA |
| 2 | 0x0010 | Kernel Data | L=0, B=0, DPL=0, S=1, Type=0x2 |
| 3 | 0x0018 | 64-bit User Code | L=1, D=0, DPL=3, S=1, Type=0xA |
| 4 | 0x0020 | User Data | L=0, B=0, DPL=3, S=1, Type=0x2 |
| 5–6 | 0x0028 | TSS Descriptor | 16-byte エントリ（64-bit TSS） |

> **注意**: 64-bit モードでは Code/Data セグメントのベース・リミットは無視される。
> TSS のみ実際のアドレスとリミットが有効。

#### TSS（Task State Segment）

```c
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;       /* 特権レベル 0 のカーネルスタック先頭（タスクスイッチ時に更新） */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];     /* Interrupt Stack Table（NMI, #DF 等の専用スタック） */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed)) T_TSS;
```

> **重要**: タスクスイッチ時に `TSS.rsp0` を新タスクのカーネルスタック先頭に更新すること
>（ARM の SP_EL1 切り替えに相当）。

### 5.2 IDT（Interrupt Descriptor Table）

**ファイル**: `${TK_SRC}/sysdepend/x86_64/idt.c` / `idt.h`

#### IDT ゲート記述子（64-bit Interrupt Gate）

```c
typedef struct {
    uint16_t offset_low;    /* ハンドラアドレス [15:0]  */
    uint16_t selector;      /* コードセグメントセレクタ (0x0008) */
    uint8_t  ist;           /* IST インデックス [2:0]、上位 5bit = 0 */
    uint8_t  type_attr;     /* P=1 | DPL | Type=0xE (64-bit Interrupt Gate) */
    uint16_t offset_mid;    /* ハンドラアドレス [31:16] */
    uint32_t offset_high;   /* ハンドラアドレス [63:32] */
    uint32_t reserved;
} __attribute__((packed)) T_IDT_ENTRY;
```

#### IDT ベクタ割り当て方針

```
ベクタ 0x00–0x1F : CPU 例外（Intel 定義固定）
ベクタ 0x20–0x2F : IRQ 0–15（レガシー PIC 互換。APIC では再マップ）
ベクタ 0x30–0xFE : デバイス割り込み（APIC Vector として自由割り当て）
ベクタ 0xFF      : Local APIC Spurious Interrupt Vector（固定）
```

> **ARM との対応**: ARM の例外ベクタテーブル（VBAR_EL1）が x86_64 では IDT に相当。

### 5.3 MSR アクセスユーティリティ

**ファイル**: `${TK_SRC}/sysdepend/x86_64/msr.h`

```c
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ __volatile__ ("wrmsr"
        :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

/* よく使う MSR アドレス */
#define MSR_EFER           0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR           0xC0000081  /* SYSCALL target CS/SS */
#define MSR_LSTAR          0xC0000082  /* SYSCALL 64-bit RIP */
#define MSR_SFMASK         0xC0000084  /* SYSCALL RFLAGS マスク */
#define MSR_FS_BASE        0xC0000100  /* FS ベースアドレス */
#define MSR_GS_BASE        0xC0000101  /* GS ベースアドレス */
#define MSR_KERNEL_GS_BASE 0xC0000102  /* SWAPGS 用 GS ベース */
#define MSR_APIC_BASE      0x0000001B  /* Local APIC ベースアドレス */
```

> **ARM との対応**: ARM の CP15/CP14 システムレジスタアクセスが x86_64 では MSR に相当。

### 5.4 SYSCALL インタフェース

**ファイル**: `${TK_SRC}/sysdepend/x86_64/syscall.S` / `syscall.h`

> **役割の訂正**: SYSCALL = ユーザ（CPL=3）→ カーネル（CPL=0）のエントリ。
> SYSRET = カーネル（CPL=0）→ ユーザ（CPL=3）のリターン。
> ARM の `SVC` 命令 → エントリが `SYSCALL`、リターンが `SYSRET` に相当。

```c
void knl_syscall_init(void) {
    /* STAR: kernel CS=0x08, user CS=0x18（RPL=3 は CPU が自動付加）*/
    wrmsr(MSR_STAR, ((uint64_t)0x0008 << 32) | ((uint64_t)0x0018 << 48));

    /* LSTAR: SYSCALL エントリポイント（ring3 → ring0）*/
    wrmsr(MSR_LSTAR, (uint64_t)knl_syscall_entry);

    /* SFMASK: SYSCALL 時に RFLAGS でクリアするビット（IF を含む）*/
    wrmsr(MSR_SFMASK, (1U << 9));   /* IF bit */

    /* EFER.SCE (bit 0): SYSCALL/SYSRET 有効化 */
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1);
}
```

### 5.5 FPU / SSE / AVX コンテキスト保存

**ファイル**: `${TK_SRC}/sysdepend/x86_64/fpu.c` / `fpu.h`

> **ARM との対応**: ARM Cortex-A の VFP/NEON が x86_64 では x87/SSE/AVX に相当。
> x86_64 は `XSAVE`/`XRSTOR` で一括保存（ARM の VSTM/VLDM に相当）。

```c
/* XSAVE 領域サイズ（CPUID leaf 0xD, subleaf 0 の ECX で動的取得）*/
extern size_t g_xsave_size;

/* コンテキストスイッチ時の FPU 保存（64-byte アライメント必須）*/
static inline void fpu_save(void *area) {
    uint64_t rfbm = 0x7;  /* x87 + SSE + AVX */
    __asm__ __volatile__ (
        "xsave64 (%0)" :: "r"(area), "A"(rfbm) : "memory"
    );
}

static inline void fpu_restore(const void *area) {
    uint64_t rfbm = 0x7;
    __asm__ __volatile__ (
        "xrstor64 (%0)" :: "r"(area), "A"(rfbm) : "memory"
    );
}

/*
 * 遅延 FPU 保存: CR0.TS + #NM 例外を利用
 * タスクスイッチ時に CR0.TS をセット → FPU アクセス時に #NM 発生
 * → #NM ハンドラ内で前タスクの FPU を保存し、CR0.TS をクリア
 */
```

---

## 6. 割り込みコントローラ（APIC）

> **ARM との対応**: ARM の GIC（Generic Interrupt Controller）が
> x86_64 では Local APIC + I/O APIC に相当する。

**ファイル**: `${TK_SRC}/sysdepend/x86_64/apic.c` / `apic.h`

### 6.1 8259A PIC の無効化（必須）

```c
void knl_disable_pic(void) {
    outb(0x21, 0xFF);   /* マスタ PIC 全 IRQ マスク */
    outb(0xA1, 0xFF);   /* スレーブ PIC 全 IRQ マスク */
}
```

### 6.2 Local APIC

```c
/* APIC ベースアドレス（MSR_APIC_BASE[31:12] × 4KB）*/
#define LAPIC_REG(off)      (*(volatile uint32_t *)(lapic_base + (off)))

#define LAPIC_ID            0x020   /* Local APIC ID */
#define LAPIC_VERSION       0x030   /* Local APIC Version */
#define LAPIC_TPR           0x080   /* Task Priority Register */
#define LAPIC_EOI           0x0B0   /* End Of Interrupt（書き込みで EOI 送信）*/
#define LAPIC_SVR           0x0F0   /* Spurious Interrupt Vector Register */
#define LAPIC_ICR_LO        0x300   /* Interrupt Command Register [31:0] */
#define LAPIC_ICR_HI        0x310   /* Interrupt Command Register [63:32] */
#define LAPIC_TIMER_LVT     0x320   /* Timer Local Vector Table Entry */
#define LAPIC_TIMER_INIT    0x380   /* Timer Initial Count */
#define LAPIC_TIMER_CURR    0x390   /* Timer Current Count（読み取り専用）*/
#define LAPIC_TIMER_DIV     0x3E0   /* Timer Divide Configuration */

void knl_lapic_init(void) {
    lapic_base = rdmsr(MSR_APIC_BASE) & 0xFFFFF000UL;

    /* SVR: APIC 有効化（bit 8）+ Spurious Vector 設定 */
    LAPIC_REG(LAPIC_SVR) = (1U << 8) | 0xFF;   /* ベクタ 0xFF */

    /* TPR: 全優先度の割り込みを受け付け */
    LAPIC_REG(LAPIC_TPR) = 0;
}

static inline void knl_lapic_eoi(void) {
    LAPIC_REG(LAPIC_EOI) = 0;   /* 値は無視される */
}
```

### 6.3 I/O APIC

```c
/* I/O APIC レジスタアクセス（MMIO インダイレクト方式）*/
#define IOAPIC_IOREGSEL     0x00
#define IOAPIC_IOWIN        0x10
#define IOAPIC_VER_REG      0x01    /* Version Register（最大 Redirection Entry 数）*/
#define IOAPIC_REDTBL(n)    (0x10 + 2*(n))

uint32_t ioapic_read(uint8_t reg) {
    *(volatile uint32_t *)(ioapic_base + IOAPIC_IOREGSEL) = reg;
    return *(volatile uint32_t *)(ioapic_base + IOAPIC_IOWIN);
}
void ioapic_write(uint8_t reg, uint32_t val) {
    *(volatile uint32_t *)(ioapic_base + IOAPIC_IOREGSEL) = reg;
    *(volatile uint32_t *)(ioapic_base + IOAPIC_IOWIN) = val;
}

/* Redirection Table Entry（64-bit）*/
typedef union {
    struct {
        uint8_t  vector;             /* 割り込みベクタ (0x10–0xFE) */
        uint8_t  delivery_mode : 3;  /* 0=Fixed, 1=LowPrio, 4=NMI, 7=ExtINT */
        uint8_t  dest_mode     : 1;  /* 0=Physical, 1=Logical */
        uint8_t  delivery_status:1;  /* RO: 0=Idle, 1=Pending */
        uint8_t  polarity      : 1;  /* 0=ActiveHigh, 1=ActiveLow */
        uint8_t  remote_irr    : 1;  /* RO */
        uint8_t  trigger_mode  : 1;  /* 0=Edge, 1=Level */
        uint8_t  mask          : 1;  /* 1=マスク（無効）*/
        uint64_t reserved      :39;
        uint8_t  destination;        /* 送信先 APIC ID */
    } __attribute__((packed));
    uint64_t raw;
} IOAPIC_REDTBL_ENTRY;

void knl_ioapic_route(uint8_t irq, uint8_t vector, uint8_t trigger_mode) {
    IOAPIC_REDTBL_ENTRY e = { .raw = 0 };
    e.vector       = vector;
    e.delivery_mode = 0;       /* Fixed */
    e.trigger_mode  = trigger_mode;
    e.mask          = 0;       /* 有効 */
    e.destination   = knl_lapic_id();
    ioapic_write(IOAPIC_REDTBL(irq),     (uint32_t)e.raw);
    ioapic_write(IOAPIC_REDTBL(irq) + 1, (uint32_t)(e.raw >> 32));
}
```

---

## 7. タイマー

> **ARM との対応**: ARM Generic Timer（CNTP/CNTV）が x86_64 では HPET または
> Local APIC Timer に相当する。

### 7.1 HPET（第一選択）

**ファイル**: `${TK_SRC}/sysdepend/x86_64/hpet.c` / `hpet.h`

HPET は ACPI HPET テーブルからベースアドレスを取得する（固定アドレスなし）。

```c
#define HPET_CAP_ID         0x000   /* Capabilities and ID（上位 32bit = period [fs]）*/
#define HPET_CONFIG         0x010   /* General Configuration */
#define HPET_INTR_STATUS    0x020   /* General Interrupt Status */
#define HPET_COUNTER        0x0F0   /* Main Counter Value */
#define HPET_TIM0_CONFIG    0x100   /* Timer 0 Configuration */
#define HPET_TIM0_COMPAR    0x108   /* Timer 0 Comparator Value */
#define HPET_REG64(base,off) (*(volatile uint64_t *)((base)+(off)))

bool knl_hpet_init(void) {
    uint64_t hpet_base = acpi_get_hpet_base();
    if (!hpet_base) return false;

    uint64_t cap       = HPET_REG64(hpet_base, HPET_CAP_ID);
    uint64_t period_fs = cap >> 32;   /* フェムト秒 / tick */
    if (period_fs == 0 || period_fs > 100000000ULL) return false;

    /* Timer 0: 周期モード + 割り込み有効 */
    uint64_t ticks = (TICK_INTERVAL_NS * 1000000ULL) / period_fs;
    HPET_REG64(hpet_base, HPET_TIM0_CONFIG) =
        (1ULL << 2) |   /* TN_INT_ENB_CNF */
        (1ULL << 3) |   /* TN_TYPE_CNF: 周期モード */
        (1ULL << 6);    /* TN_VAL_SET_CNF */
    HPET_REG64(hpet_base, HPET_TIM0_COMPAR) = ticks;
    HPET_REG64(hpet_base, HPET_CONFIG) |= 1;   /* メインカウンタ有効化 */

    knl_ioapic_route(HPET_IRQ, TIMER_VECTOR, 0 /* Edge */);
    return true;
}
```

### 7.2 Local APIC Timer（フォールバック）

HPET が使用できない場合に使用。周波数は PIT（8254）で事前にキャリブレーションすること。

```c
void knl_lapic_timer_init(uint32_t ticks_per_interval) {
    LAPIC_REG(LAPIC_TIMER_DIV) = 0x3;              /* 分周比 = 16 */
    LAPIC_REG(LAPIC_TIMER_LVT) = (1U << 17)        /* 周期モード */
                                | TIMER_VECTOR;
    LAPIC_REG(LAPIC_TIMER_INIT) = ticks_per_interval;
}
```

---

## 8. コンテキストスイッチ（Design A）

**ファイル**: `${TK_SRC}/sysdepend/x86_64/interrupt.S`

> **Design A**: C ハンドラがリターンした直後に `interrupt.S` 内のディスパッチャが
> 次タスクを選択し、FPU を保存・復元して TSS.RSP0 を更新、`iretq` で復帰する。

```asm
/* GAS AT&T 構文 */
dispatch_after_handler:
    call  knl_schedule          /* 戻り値 RAX = 次タスクの T_REGS*（NULL = スイッチ不要）*/
    testq %rax, %rax
    jz    .restore_current

    /* FPU/SSE コンテキスト保存（現タスク）*/
    call  fpu_save_current
    /* FPU/SSE コンテキスト復元（次タスク）*/
    call  fpu_restore_next

    /* TSS.RSP0 を次タスクのカーネルスタック先頭に更新 */
    movq  TCB_KERNEL_STACK_TOP(%rax), %rbx
    movq  %rbx, tss + TSS_RSP0

.restore_current:
    /* T_REGS から汎用レジスタを復元（pushq の逆順）*/
    popq  %rdi
    popq  %rsi
    popq  %rdx
    popq  %rcx
    popq  %rbx
    popq  %rax
    popq  %r8
    popq  %r9
    popq  %r10
    popq  %r11
    popq  %r12
    popq  %r13
    popq  %r14
    popq  %r15
    popq  %rbp

    addq  $16, %rsp   /* vector・errcode をスタックから除去 */
    iretq              /* RIP, CS, RFLAGS, RSP, SS を復元して復帰 */
```

---

## 9. メモリマップ（参考）

```
物理アドレス空間:
  0x000000000000–0x0000000FFFFF  : Legacy + BIOS（使用不可）
  0x000000100000–0x000000FFFFFF  : カーネルロード領域（Identity-mapped）
  0x000001000000–                : 動的メモリ（T-Kernel メモリマネージャ管理）
                                   実際の使用可能領域は E820 で確認すること

MMIO 領域（Identity-mapped 範囲外 — 個別にページテーブルで MMIO マップ）:
  MSR_APIC_BASE で取得（通常 0xFEE00000）: Local APIC  (4 KB)
  ACPI MADT で取得（通常 0xFEC00000）    : I/O APIC    (4 KB)
  ACPI HPET テーブルで取得              : HPET        (4 KB)
```

---

## 10. cpu/x86_64 ディレクトリ（10 ファイル）

### 10.1 cache.c — キャッシュ制御

```c
/* CLFLUSH: 指定アドレスのキャッシュラインを書き戻してから無効化 */
static inline void cache_flush_line(const void *addr) {
    __asm__ __volatile__ ("clflush (%0)" :: "r"(addr) : "memory");
}

/* WBINVD: 全キャッシュを書き戻してから無効化（特権命令）*/
static inline void cache_flush_all(void) {
    __asm__ __volatile__ ("wbinvd" ::: "memory");
    /*
     * INVD は使用禁止。書き戻しなしにキャッシュを破棄するため
     * メモリ内容が失われ、データ破壊が起きる。
     * 正当な使用場面はごく限られたファームウェアコードのみ。
     */
}

/* L1/L2/L3 キャッシュ情報は CPUID leaf 4（subleaf 列挙）で動的取得 */
void cache_detect(void) {
    for (uint32_t i = 0; ; i++) {
        uint32_t eax, ebx, ecx, edx;
        cpuid_ex(4, i, &eax, &ebx, &ecx, &edx);
        if ((eax & 0x1F) == 0) break;   /* Cache Type = 0: 列挙終端 */
        /* EBX[11:0]+1 = line_size [byte]
           EBX[21:12]+1 = physical_partitions
           EBX[31:22]+1 = ways
           ECX+1 = sets
           size = ways × partitions × line_size × sets */
    }
}
```

### 10.2 chkplv.c — 特権レベルチェック

> **訂正**: RFLAGS.IOPL は I/O 権限レベル（IN/OUT 命令の許可条件）であり、
> CPL（Current Privilege Level）とは全く別の概念。CPL は **CS セレクタの[1:0]ビット**。

```c
static inline uint8_t get_cpl(void) {
    uint16_t cs;
    __asm__ __volatile__ ("mov %%cs, %0" : "=r"(cs));
    return (uint8_t)(cs & 0x3);
}

static inline bool is_kernel_mode(void) { return get_cpl() == 0; }

static inline uint8_t get_cpl_from_regs(const T_REGS *regs) {
    return (uint8_t)(regs->cs & 0x3);
}

void chkplv_verify(const T_REGS *regs) {
    if (get_cpl_from_regs(regs) != 0) {
        knl_log_error("Privilege violation: CPL=%u", get_cpl_from_regs(regs));
        knl_panic("Privilege violation");
    }
}
```

### 10.3 cpu_calls.c — システムコール・割り込みハンドラ登録

```c
/* IDT エントリ設定 */
void knl_idt_set_entry(uint8_t vector, void (*handler)(void),
                       uint8_t ist, uint8_t dpl) {
    uint64_t addr = (uint64_t)handler;
    idt[vector] = (T_IDT_ENTRY){
        .offset_low  = addr & 0xFFFF,
        .selector    = GDT_KERNEL_CODE,
        .ist         = ist & 0x7,
        .type_attr   = 0x80 | ((dpl & 0x3) << 5) | 0x0E,  /* P=1, 64-bit gate */
        .offset_mid  = (addr >> 16) & 0xFFFF,
        .offset_high = (addr >> 32) & 0xFFFFFFFF,
        .reserved    = 0
    };
}

static inline void knl_di(void) { __asm__ __volatile__ ("cli" ::: "memory"); }
static inline void knl_ei(void) { __asm__ __volatile__ ("sti" ::: "memory"); }
```

### 10.4 cpu_conf.h — CPU 設定定数・構造体

```c
/* コントロールレジスタ ビット定義 */
#define CR0_PE      (1UL << 0)   /* Protected Mode Enable */
#define CR0_MP      (1UL << 1)   /* Monitor Coprocessor */
#define CR0_EM      (1UL << 2)   /* Emulation（x87 FPU 無効化）*/
#define CR0_TS      (1UL << 3)   /* Task Switched（FPU 遅延保存）*/
#define CR0_WP      (1UL << 16)  /* Write Protect（ring0 でも書込保護）*/
#define CR0_PG      (1UL << 31)  /* Paging Enable */

#define CR4_PAE        (1UL << 5)   /* Physical Address Extension */
#define CR4_PGE        (1UL << 7)   /* Page Global Enable */
#define CR4_OSFXSR     (1UL << 9)   /* SSE 有効化 */
#define CR4_OSXMMEXCPT (1UL << 10)  /* #XM 例外有効化 */
#define CR4_OSXSAVE    (1UL << 18)  /* XSAVE/XRSTOR 有効化 */

/* EFER MSR ビット定義 */
#define EFER_SCE  (1UL << 0)   /* SYSCALL/SYSRET 有効 */
#define EFER_LME  (1UL << 8)   /* Long Mode Enable */
#define EFER_LMA  (1UL << 10)  /* Long Mode Active（read-only）*/
#define EFER_NXE  (1UL << 11)  /* No-Execute Enable */

/* ページサイズ */
#define PAGE_4K   0x1000UL
#define PAGE_2M   0x200000UL
#define PAGE_1G   0x40000000UL

/* GDT セレクタ */
#define GDT_NULL        0x0000
#define GDT_KERNEL_CODE 0x0008
#define GDT_KERNEL_DATA 0x0010
#define GDT_USER_CODE   0x001B   /* 0x0018 | RPL=3 */
#define GDT_USER_DATA   0x0023   /* 0x0020 | RPL=3 */
#define GDT_TSS         0x0028   /* 16-byte エントリ */

/* タイマ割り込みベクタ */
#define TIMER_VECTOR    0x30
#define SPURIOUS_VECTOR 0xFF

/* CPU 例外ベクタ番号 */
#define EXC_DE  0   /* Divide Error */
#define EXC_DB  1   /* Debug */
#define EXC_NMI 2   /* Non-Maskable Interrupt */
#define EXC_BP  3   /* Breakpoint */
#define EXC_OF  4   /* Overflow */
#define EXC_BR  5   /* BOUND Range Exceeded */
#define EXC_UD  6   /* Invalid Opcode */
#define EXC_NM  7   /* Device Not Available（FPU）*/
#define EXC_DF  8   /* Double Fault（errcode=0）*/
#define EXC_TS  10  /* Invalid TSS */
#define EXC_NP  11  /* Segment Not Present */
#define EXC_SS  12  /* Stack-Segment Fault */
#define EXC_GP  13  /* General Protection Fault */
#define EXC_PF  14  /* Page Fault */
#define EXC_MF  16  /* x87 FPU Floating-Point Error */
#define EXC_AC  17  /* Alignment Check */
#define EXC_MC  18  /* Machine Check */
#define EXC_XM  19  /* SIMD Floating-Point Exception */
#define EXC_VE  20  /* Virtualization Exception */
```

### 10.5 cpu_init.c — CPU 初期化（Long Mode 移行後）

> **訂正**: Long Mode 移行（CR3/CR4/EFER/CR0 設定）は `icrt0.S` の責務。
> `cpu_init.c` は `knl_main()` から呼ばれ、**既に Long Mode** の状態で動作する。

```c
void knl_cpu_init(void) {
    knl_cpu_feature_detect();         /* 1. CPUID 機能確認 */
    knl_gdt_init();                   /* 2. 本番 GDT 再構築 */
    knl_tss_init();                   /* 3. TSS 初期化・登録 */
    knl_idt_init();                   /* 4. IDT 全 256 ベクタ初期化 */

    /* 5. CR4 拡張機能有効化 */
    uint64_t cr4 = read_cr4();
    cr4 |= CR4_PGE | CR4_OSFXSR | CR4_OSXMMEXCPT;
    if (cpu_has_xsave()) cr4 |= CR4_OSXSAVE;
    write_cr4(cr4);

    /* 6. CR0: FPU 有効化・ring0 書込保護 */
    write_cr0((read_cr0() & ~CR0_TS & ~CR0_EM) | CR0_WP);

    /* 7. EFER: NXE 有効化 */
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);

    /* 8. SYSCALL/SYSRET 有効化 */
    knl_syscall_init();
}

void knl_cpu_halt(void) {
    knl_di();
    for (;;) __asm__ __volatile__ ("hlt");
}
```

### 10.6 cpu_insn.h — インラインアセンブラ命令マクロ

```c
/* コントロールレジスタ読み書き */
static inline uint64_t read_cr0(void)  { uint64_t v; __asm__ __volatile__("mov %%cr0,%0":"=r"(v)); return v; }
static inline uint64_t read_cr2(void)  { uint64_t v; __asm__ __volatile__("mov %%cr2,%0":"=r"(v)); return v; }
static inline uint64_t read_cr3(void)  { uint64_t v; __asm__ __volatile__("mov %%cr3,%0":"=r"(v)); return v; }
static inline uint64_t read_cr4(void)  { uint64_t v; __asm__ __volatile__("mov %%cr4,%0":"=r"(v)); return v; }
static inline void write_cr0(uint64_t v) { __asm__ __volatile__("mov %0,%%cr0"::"r"(v):"memory"); }
static inline void write_cr3(uint64_t v) { __asm__ __volatile__("mov %0,%%cr3"::"r"(v):"memory"); }
static inline void write_cr4(uint64_t v) { __asm__ __volatile__("mov %0,%%cr4"::"r"(v):"memory"); }

/* RFLAGS */
static inline uint64_t read_rflags(void) {
    uint64_t v; __asm__ __volatile__("pushfq; popq %0":"=r"(v)); return v;
}

/* ポート I/O */
static inline void   outb(uint16_t p, uint8_t  v) { __asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void   outw(uint16_t p, uint16_t v) { __asm__ __volatile__("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t  v; __asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p)); return v; }

/* TLB フラッシュ */
static inline void tlb_flush_all(void)            { write_cr3(read_cr3()); }
static inline void tlb_flush_page(uint64_t addr)  { __asm__ __volatile__("invlpg (%0)"::"r"(addr):"memory"); }

/* アトミック操作 */
static inline uint64_t atomic_cmpxchg64(uint64_t *ptr, uint64_t old, uint64_t new) {
    uint64_t result;
    __asm__ __volatile__("lock cmpxchgq %2,%1":"=a"(result),"+m"(*ptr):"r"(new),"0"(old):"memory");
    return result;
}

/* TSC */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc":"=a"(lo),"=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
```

### 10.7 cpu_status.h — CPU ステータス定義

```c
/* RFLAGS ビット定義 */
#define RFLAGS_CF   (1UL << 0)   /* Carry Flag */
#define RFLAGS_PF   (1UL << 2)   /* Parity Flag */
#define RFLAGS_AF   (1UL << 4)   /* Auxiliary Carry */
#define RFLAGS_ZF   (1UL << 6)   /* Zero Flag */
#define RFLAGS_SF   (1UL << 7)   /* Sign Flag */
#define RFLAGS_TF   (1UL << 8)   /* Trap Flag */
#define RFLAGS_IF   (1UL << 9)   /* Interrupt Enable */
#define RFLAGS_DF   (1UL << 10)  /* Direction Flag */
#define RFLAGS_OF   (1UL << 11)  /* Overflow Flag */
/* RFLAGS[13:12] = IOPL: I/O Privilege Level。CPL とは別物 */
#define RFLAGS_IOPL_MASK  (3UL << 12)
#define RFLAGS_NT   (1UL << 14)  /* Nested Task */
#define RFLAGS_RF   (1UL << 16)  /* Resume Flag */
#define RFLAGS_VM   (1UL << 17)  /* Virtual-8086 Mode */
#define RFLAGS_AC   (1UL << 18)  /* Alignment Check */
#define RFLAGS_ID   (1UL << 21)  /* ID Flag（CPUID 使用可否）*/

static inline bool irqs_enabled(void) { return !!(read_rflags() & RFLAGS_IF); }
```

### 10.8 cpu_support.S — アセンブラサポートルーチン（GAS AT&T 構文）

> **GAS に統一**: GCC ツールチェーン（x86_64-elf-gcc）に合わせ GAS (AT&T 構文) を使用。
> NASM/YASM は構文・マクロ体系が全く異なるため混在させない。

```asm
/* cpu_support.S — GAS AT&T 構文 */
.code64
.section .text

/* コンテキストスイッチ */
/* void cpu_context_switch(uint64_t **save_rsp, uint64_t *next_rsp) */
.global cpu_context_switch
cpu_context_switch:
    movq  %rsp, (%rdi)     /* 現タスクの RSP を保存 */
    call  fpu_save_current  /* FPU/SSE 保存 */
    movq  %rsi, %rsp        /* 次タスクの RSP に切り替え */
    call  fpu_restore_next  /* FPU/SSE 復元 */
    ret

/* #NM 例外ハンドラ（遅延 FPU 保存）*/
.global nm_exception_handler
nm_exception_handler:
    clts                   /* CR0.TS クリア（FPU アクセス許可）*/
    call  fpu_lazy_save    /* 前タスクの FPU 保存 */
    iretq
```

### 10.9 cpu_task.h — タスクコンテキスト

```c
typedef struct _TCB {
    /* ... T-Kernel 既存フィールド ... */
    uint64_t *saved_rsp;              /* コンテキストスイッチ後の RSP */
    uint64_t  kernel_stack_top;       /* このタスクのカーネルスタック先頭 */
    uint8_t   fpu_area[512]           /* FXSAVE/XSAVE 保存領域（動的確保推奨）*/
              __attribute__((aligned(64)));
    bool      fpu_used;               /* FPU 使用済み（遅延保存最適化用）*/
    uint8_t   cpl;                    /* 0=kernel, 3=user */
} TCB;

/* タスクスイッチ後処理: TSS.RSP0 を次タスクのカーネルスタックに更新 */
static inline void cpu_switch_post(TCB *next) {
    tss.rsp0 = next->kernel_stack_top;
}
```

### 10.10 offset.h — 構造体オフセット定義

```c
/*
 * アセンブラ（cpu_support.S 等）から C 構造体フィールドにアクセスするための定数。
 * C 側の構造体定義と必ず同期させ、_Static_assert で整合性を検証すること。
 */

/* T_REGS オフセット（単位: バイト）*/
#define TREGS_RAX     0
#define TREGS_RBX     8
#define TREGS_RCX     16
#define TREGS_RDX     24
#define TREGS_RSI     32
#define TREGS_RDI     40
#define TREGS_R8      48
#define TREGS_R9      56
#define TREGS_R10     64
#define TREGS_R11     72
#define TREGS_R12     80
#define TREGS_R13     88
#define TREGS_R14     96
#define TREGS_R15     104
#define TREGS_RBP     112
#define TREGS_RIP     120
#define TREGS_CS      128
#define TREGS_RFLAGS  136
#define TREGS_RSP     144
#define TREGS_SS      152

/* TCB オフセット */
#define TCB_SAVED_RSP          0
#define TCB_KERNEL_STACK_TOP   8
#define TCB_FPU_AREA           16

/* TSS オフセット */
#define TSS_RSP0  4   /* T_TSS.rsp0 のオフセット */

/* 静的アサートで整合性検証 */
_Static_assert(offsetof(T_REGS, rip)    == TREGS_RIP,    "TREGS_RIP mismatch");
_Static_assert(offsetof(T_REGS, rflags) == TREGS_RFLAGS, "TREGS_RFLAGS mismatch");
_Static_assert(offsetof(T_REGS, rsp)    == TREGS_RSP,    "TREGS_RSP mismatch");
```

---

## 11. device/x86_64 ディレクトリ（12 ファイル）

### 11.1 cache_info.h — キャッシュ情報定義

```c
typedef struct {
    uint32_t level;       /* 1=L1, 2=L2, 3=L3 */
    uint32_t type;        /* 1=Data, 2=Instruction, 3=Unified */
    uint32_t line_size;   /* [byte] */
    uint32_t sets;
    uint32_t ways;
    uint32_t size_kb;
} CacheLevel;

#define MAX_CACHE_LEVELS 4
extern CacheLevel g_cache_info[MAX_CACHE_LEVELS];
extern uint32_t   g_cache_level_count;
```

### 11.2 cntwus.c — マイクロ秒ウェイト処理

> **訂正**: RDTSC 単体での精度保証は不可。使用前に **PIT または HPET で TSC 周波数を
> キャリブレーション**すること。キャリブレーション未完了時は PIT/HPET で直接ウェイト。

```c
static uint64_t g_tsc_freq_hz = 0;

void cntwus_calibrate(void) {
    /* PIT ch.2 で 10 ms ウェイトし、前後の TSC 差分から周波数算出 */
    uint64_t t0 = rdtsc();
    pit_delay_10ms();          /* PIT で正確な 10 ms 待機 */
    uint64_t t1 = rdtsc();
    g_tsc_freq_hz = (t1 - t0) * 100;   /* × 100 = per second */
}

void knl_CountWait(uint32_t us) {
    if (g_tsc_freq_hz == 0) { pit_wait_us(us); return; }
    uint64_t deadline = rdtsc() + (uint64_t)us * g_tsc_freq_hz / 1000000ULL;
    while (rdtsc() < deadline) __asm__ __volatile__ ("pause");
}
```

### 11.3 devinit.c — デバイス初期化統括

```c
void knl_device_init(void) {
    outb(0x21, 0xFF); outb(0xA1, 0xFF);  /* 1. 8259A PIC 無効化 */
    knl_lapic_init();                     /* 2. Local APIC 初期化 */
    knl_ioapic_init();                    /* 3. I/O APIC 初期化 */
    if (!knl_hpet_init()) knl_lapic_timer_init(calibrate_lapic_freq());
                                          /* 4. タイマ初期化 */
    cntwus_calibrate();                   /* 5. TSC キャリブレーション */
    knl_pci_enumerate();                  /* 6. PCI/PCIe デバイス列挙 */
    if (knl_ps2_probe()) knl_ps2_init(); /* 7. PS/2 コントローラ */
    knl_uart_init(0x3F8, 115200);        /* 8. COM1 シリアルコンソール */
}
```

### 11.4 icrt0.S — Long Mode 移行エントリ（GAS AT&T 構文）

```asm
/* icrt0.S — §3 のブートシーケンスを GAS AT&T 構文で実装 */
.code32
.section .text
.global _start
_start:
    cli
    /* GDT ロード → CR4.PAE → EFER.LME → CR0.PG → ljmp */
    /* 詳細は §3 のフロー（ステップ 1–7）参照 */
.code64
long_mode_entry:
    xorl  %eax, %eax
    leaq  _bss_start(%rip), %rdi
    leaq  _bss_end(%rip),   %rcx
    subq  %rdi, %rcx
    rep   stosb                       /* BSS クリア */
    leaq  knl_kernel_stack_top(%rip), %rsp
    call  knl_main
    cli
.halt: hlt
    jmp   .halt
```

### 11.5 icrt0_ram.S — RAM 初期化

```asm
/* RAM 検証・クリア（E820 で確認済みの使用可能領域のみ操作）*/
.code64
.global knl_ram_init
knl_ram_init:
    /* memory.c の g_e820_map を参照して使用可能領域を列挙・クリア */
    ret
```

### 11.6 memory.c — E820 メモリマップ管理（新規）

> **ARM との差分**: ARM は Device Tree/ATAG でメモリマップを取得する。
> x86_64 は BIOS E820 または UEFI GetMemoryMap() を使用する。x86_64 固有。

```c
#define E820_MAX_ENTRIES  128
#define E820_TYPE_RAM     1   /* 使用可能 RAM */
#define E820_TYPE_RESERVED 2
#define E820_TYPE_ACPI    3
#define E820_TYPE_NVS     4
#define E820_TYPE_UNUSABLE 5

typedef struct { uint64_t base, length; uint32_t type; } E820Entry;

static E820Entry g_e820_map[E820_MAX_ENTRIES];
static uint32_t  g_e820_count;

void knl_memory_init(const void *mbi) {
    parse_multiboot2_mmap(mbi, g_e820_map, &g_e820_count);
    for (uint32_t i = 0; i < g_e820_count; i++)
        if (g_e820_map[i].type == E820_TYPE_RAM)
            knl_mem_register_free(g_e820_map[i].base, g_e820_map[i].length);
}

bool knl_memory_is_usable(uint64_t base, uint64_t len) {
    for (uint32_t i = 0; i < g_e820_count; i++)
        if (g_e820_map[i].type == E820_TYPE_RAM &&
            g_e820_map[i].base <= base &&
            base + len <= g_e820_map[i].base + g_e820_map[i].length)
            return true;
    return false;
}
```

### 11.7 patch.c / patch.h — ACPI DSDT パッチ処理

> **訂正**: 対象は **ACPI DSDT バグ回避**のみ。
> BIOS パッチは Legacy BIOS 環境のみ有効。UEFI パッチはスコープ外。

```c
typedef struct {
    const char *vendor; const char *board;
    void (*apply)(void); const char *description;
} HwPatch;

static const HwPatch hw_patches[] = {
    { "VendorX", "BoardY", patch_dsdt_timer_fix, "Fix HPET DSDT entry" },
    { NULL, NULL, NULL, NULL }
};

void knl_patch_apply(void) {
    const char *v = dmi_get_vendor(), *b = dmi_get_board();
    for (const HwPatch *p = hw_patches; p->vendor; p++)
        if (!strcmp(p->vendor,v) && !strcmp(p->board,b))
            { p->apply(); knl_log_info("Patch: %s", p->description); }
}
```

### 11.8 power.c — 電源管理

> **訂正**: S3/S4 は ACPI AML インタプリタが必要で初期移植では実装困難。
> 初期移植対象は **S5（シャットダウン）のみ**。

```c
void knl_power_off(void) {
    /* ACPI FADT: PM1a_CNT_BLK に S5 の SLP_TYP を書いて SLP_EN をセット */
    uint16_t pm1a = acpi_get_pm1a_cnt_blk();
    uint16_t typa = acpi_get_s5_slp_typa();
    outw(pm1a, typa | (1U << 13));   /* SLP_EN = bit 13 */
    for (;;) __asm__ __volatile__ ("hlt");
}

void knl_power_suspend(void) { knl_log_warn("S3: not implemented"); }
void knl_power_hibernate(void) { knl_log_warn("S4: not implemented"); }
```

### 11.9 tkdev_conf.h — デバイス設定定数

```c
/* APIC（実際のアドレスは MSR_APIC_BASE / ACPI MADT で確認すること）*/
#define LAPIC_BASE_DEFAULT   0xFEE00000UL
#define IOAPIC_BASE_DEFAULT  0xFEC00000UL

/* PIT (8254) */
#define PIT_CH0  0x40
#define PIT_CH2  0x42
#define PIT_CMD  0x43
#define PIT_FREQ 1193182UL

/* UART (16550 COM1) */
#define UART_COM1_BASE 0x3F8
#define UART_COM1_IRQ  4

/* PS/2 コントローラ */
#define PS2_DATA   0x60
#define PS2_CMD    0x64

/* タイマ割り込みベクタ（§10.4 の TIMER_VECTOR と一致させること）*/
#define HPET_IRQ       2     /* I/O APIC GSI */
#define TICK_INTERVAL_NS  1000000UL   /* 1 ms */
#define TICK_HZ           1000
```

### 11.10 tkdev_init.c — タイマ・割り込み初期化

（§6.2, §6.3, §7.1, §7.2 のコードを統合して実装。詳細は各節参照。）

### 11.11 tkdev_timer.h — タイマ定数・レジスタ定義

```c
/* HPET レジスタオフセット（§7.1 の定義と同一）*/
#define HPET_CAP_ID       0x000
#define HPET_CONFIG       0x010
#define HPET_COUNTER      0x0F0
#define HPET_TIM0_CONFIG  0x100
#define HPET_TIM0_COMPAR  0x108

/* Local APIC Timer（§6.2 の定義と同一）*/
#define LAPIC_TIMER_LVT   0x320
#define LAPIC_TIMER_INIT  0x380
#define LAPIC_TIMER_CURR  0x390
#define LAPIC_TIMER_DIV   0x3E0
```

### 11.12 memory.c — E820 メモリマップ管理

（§11.6 参照）

---

## 12. 共通設計指針（Linux 流堅牢性・拡張性）

```
1. 動的機能検出    : CPUID/ACPI/MSR で実行時確認。機能なしを前提にしたコードを書かない
2. Intel/AMD 両対応: CPUID ベンダ ID で異なる挙動（TSC 安定性等）を分岐
3. 安全フォールバック: XSAVE 非対応 → FXSAVE、HPET 非存在 → LAPIC Timer
4. 予約ビット管理  : MSR/CR レジスタの予約ビット書き込みは #GP。CPUID で確認してから操作
5. カーネル/ユーザ分離: CR3 切り替えによる完全なアドレス空間分離（KPTI 考慮設計）
6. デバッグ容易性  : panic 時は CPU レジスタ・スタックトレースをシリアル (UART) に出力
7. SMP 考慮       : 初期移植は BSP のみ。ただしロック・IPI・TLB シュートダウンは
                    SMP 拡張を見据えた設計にすること
```

---

## 13. ファイル対応表（ARM/EM1D ↔ x86_64）

| ARM/EM1D ファイル | x86_64 ファイル | 主な差分 |
|-----------------|---------------|---------|
| cpu_depend.h | cpu_conf.h, cpu_insn.h, cpu_status.h | CR0-4, RFLAGS, MSR 定義 |
| cpu_support.S | cpu_support.S (GAS AT&T 構文) | XSAVE/XRSTOR、NASM/YASM 不使用 |
| cpu_task.h | cpu_task.h + offset.h | TSS, XSAVE 領域追加 |
| chkplv.c | chkplv.c | CPL = CS[1:0]。IOPL は別物 |
| cache.c | cache.c | INVD 禁止。WBINVD + CLFLUSH |
| gic.c | tkdev_init.c (APIC 部分) | GIC → Local APIC + I/O APIC |
| arm_timer.c | tkdev_init.c (HPET 部分) | Generic Timer → HPET/LAPIC |
| なし（ARM）| memory.c | ARM=DT/ATAG。x86_64=E820（固有）|
| なし（ARM）| cpu_init.c (GDT/TSS 部分) | GDT/TSS は x86_64 固有 |

---

## 14. CPUID 機能確認チェックリスト

```c
void knl_cpu_feature_detect(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    KASSERT(edx & (1 << 26), "SSE2 required");      /* SSE2 */
    KASSERT(edx & (1 <<  9), "APIC required");      /* APIC */
    KASSERT(edx & (1 <<  4), "TSC required");       /* TSC */
    cpu_has_xsave = !!(ecx & (1 << 26));            /* XSAVE */

    if (cpu_has_xsave) {
        cpuid_ex(0xD, 0, &eax, &ebx, &ecx, &edx);
        g_xsave_size = ecx;   /* XSAVE 領域サイズ [byte] */
    } else {
        g_xsave_size = 512;   /* FXSAVE サイズ */
    }
}
```

---

## 付録 A: ファイル一覧

### cpu/x86_64（10 ファイル）

| # | ファイル | 主な責務 |
|---|---------|---------|
| 1 | cache.c | WBINVD/CLFLUSH（INVD 禁止）|
| 2 | chkplv.c | CPL 判定（CS[1:0]）・特権違反検出 |
| 3 | cpu_calls.c | IDT 登録・SYSCALL 初期化・CLI/STI |
| 4 | cpu_conf.h | CR0-4, RFLAGS, EFER, GDT/IDT/例外定数 |
| 5 | cpu_init.c | Long Mode 移行後の CPU 初期化（knl_main から呼ばれる）|
| 6 | cpu_insn.h | CRx/RFLAGS/ポート I/O/TLB/アトミック/RDTSC マクロ |
| 7 | cpu_status.h | RFLAGS ビット定義・ステータス操作 |
| 8 | cpu_support.S | GAS AT&T 構文。コンテキストスイッチ・iretq |
| 9 | cpu_task.h | TCB 拡張（TSS, XSAVE 領域, kernel stack）|
| 10 | offset.h | アセンブラ向け構造体オフセット定数・静的アサート |

### device/x86_64（12 ファイル）

| # | ファイル | 主な責務 |
|---|---------|---------|
| 1 | cache_info.h | L1/L2/L3 キャッシュ情報構造体 |
| 2 | cntwus.c | TSC キャリブレーション + μs ウェイト |
| 3 | devinit.c | デバイス初期化統括 |
| 4 | icrt0.S | Long Mode 移行エントリ（GAS AT&T 構文）|
| 5 | icrt0_ram.S | RAM 検証・クリア |
| 6 | memory.c | E820 メモリマップ管理（**x86_64 固有・新規**）|
| 7 | patch.c | ACPI DSDT バグ回避パッチ |
| 8 | patch.h | パッチテーブル定義 |
| 9 | power.c | S5 シャットダウン（S3/S4 は後期実装）|
| 10 | tkdev_conf.h | APIC/HPET/PIT/UART/PS2 定数 |
| 11 | tkdev_init.c | LAPIC/IOAPIC/HPET 初期化 |
| 12 | tkdev_timer.h | タイマ定数・レジスタオフセット |

---

## 付録 B: 参考文書

| 文書 | 用途 |
|------|------|
| Intel SDM Vol. 3A | GDT/IDT/TSS/CR0-4/APIC/MSR/CPUID |
| Intel SDM Vol. 3B | APIC プログラミング詳細 |
| AMD64 Architecture Programmer's Manual Vol. 2 | Long Mode・SYSCALL/SYSRET |
| HPET Specification Rev 1.0a | HPET レジスタ仕様 |
| ACPI Specification 6.5 | MADT/HPET/DSDT テーブル |
| System V AMD64 ABI Supplement | 呼び出し規約・スタックレイアウト |
| Multiboot2 Specification | E820 メモリマップ取得 |
| T-Kernel 2.0 Specification | T-Kernel インタフェース仕様 |
| OSDev Wiki (osdev.org) | x86_64 実装リファレンス |

---

*本書は v2.4 統合完全網羅版をもって正式版とする。v2.5 は本書に統合済み。*
