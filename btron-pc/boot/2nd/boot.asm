bits 16
org 0x7c00

; Boot sector
_start:
    ; Set up segments
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax
    
    ; Set stack pointer
    mov sp, 0x7c00
    
    ; Clear screen (AH=06, AL=0 means clear entire screen)
    mov ax, 0x0600
    mov bh, 0x07
    mov cx, 0x0000
    mov dx, 0x184f
    int 0x10
    
    ; Set cursor position (AH=02)
    mov ax, 0x0200
    mov bh, 0x00
    mov cx, 0x0000
    int 0x10
    

    ; Display boot banner
    mov ax, 0x1301
    mov bh, 0x00
    mov cx, banner_len
    mov bl, 0x0A
    mov dx, 0x0000
    lea bp, [rel banner]
    int 0x10

    ; Display progress: Loading kernel...
    mov ax, 0x1301
    mov bh, 0x00
    mov cx, loading_len
    mov bl, 0x0A
    mov dx, 0x0100
    lea bp, [rel loading]
    int 0x10

    ; (雛形) カーネル本体をロードする処理をここに追加
    ; 例: ディスクからセクタを読み込む (未実装)
    ; エラー時はエラーメッセージ表示

        ; --- 例: INT 13hでセクタ読み込み（雛形） ---
        mov ah, 0x02         ; Read sectors
        mov al, 0x01         ; Number of sectors
        mov ch, 0x00         ; Cylinder
        mov cl, 0x02         ; Sector (start at 2)
        mov dh, 0x00         ; Head
        mov dl, 0x00         ; Drive (0=floppy, 80h=HDD)
        mov bx, 0x1000       ; Buffer address (例: 0x1000:0000)
        int 0x13             ; BIOS disk service

        jc disk_error        ; エラー時ジャンプ

        ; --- カーネルエントリにジャンプ（雛形） ---
        jmp 0x1000:0000      ; 読み込んだカーネルへ制御移譲

    disk_error:
        mov ax, 0x1301
        mov bh, 0x00
        mov cx, error_len
        mov bl, 0x0A
        mov dx, 0x0300
        lea bp, [rel error]
        int 0x10
        jmp $

    ; Display progress: Kernel loaded.
    mov ax, 0x1301
    mov bh, 0x00
    mov cx, loaded_len
    mov bl, 0x0A
    mov dx, 0x0200
    lea bp, [rel loaded]
    int 0x10

    ; (雛形) カーネルエントリにジャンプ (未実装)
    ; jmp 0x1000:0000 など

    ; Hang (for now)
    jmp $

; Data section
banner: db "B-Free 32bit 2nd Boot",0
banner_len equ $ - banner
loading: db "Loading kernel...",0
loading_len equ $ - loading
loaded: db "Kernel loaded. (stub)",0
loaded_len equ $ - loaded

error: db "Disk read error!",0
error_len equ $ - error

; Boot signature (must be at offset 510-511)
times 510 - ($ - $$) db 0
dw 0xAA55
