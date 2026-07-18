bits 64
section .multiboot2
align 8
multiboot2_header_start:
    dd 0xE85250D6        ; magic (Multiboot2)
    dd 0                 ; architecture (0 = i386)
    dd multiboot2_header_end - multiboot2_header_start ; header length（16バイト）
    dd -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header_start)) ; checksum

multiboot2_header_end:
    ; ヘッダ直後を0で埋めて8KiB確保（GRUB誤認識対策）
    times (0x2000 - (multiboot2_header_end - multiboot2_header_start)) db 0

section .text
; ここでMultiboot2ヘッダ終了。余計なデータなし。
global _start
extern main_x86_64_entry

section .text
_start:
    cli                        ; 割り込み禁止
    mov ax, 0x10               ; データセグメント(GDT仮定)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x110000   ; 明示的な物理アドレスでスタック初期化
    and rsp, -16
    call main_x86_64_entry
    jmp .error_hang
.error_hang:
    hlt
    jmp .error_hang

section .note.GNU-stack noalloc noexec nowrite progbits