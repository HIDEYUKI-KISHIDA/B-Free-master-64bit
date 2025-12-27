
; boot64.S - 64ビット用ブートローダ雛形 (nasm用)
BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, msg
    call print_string

    ; カーネル(2ndステージ)を0x100000にロード（雰囲気: 1セクタのみ）
    mov bx, 0x1000  ; 0x10000:0x0000 = 0x100000
    mov dh, 1       ; 1セクタ分
    call read_kernel

    ; 64ビットロングモード突入準備（本物はGDT/CR0/CR4/EFER/CR3等の設定が必要）
    ; ここでは雰囲気のみ

    ; カーネルエントリにジャンプ（0x100000）
    jmp 0x1000:0x0000

    hlt

print_string:
    mov ah, 0x0E
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    ret

read_kernel:
    ; BIOS INT 13hで1セクタだけロード（雰囲気）
    mov ah, 0x02    ; read sectors
    mov al, dh      ; sector count
    mov ch, 0x00    ; cylinder
    mov cl, 0x02    ; sector 2 (bootは1)
    mov dh, 0x00    ; head
    mov dl, 0x80    ; drive
    int 0x13
    ret

msg db 'Booting 64bit kernel...', 0

times 510-($-$$) db 0
    dw 0xAA55
