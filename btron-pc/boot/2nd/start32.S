.section .text
.globl ignore_handler
ignore_handler = _ignore_handler

IDT		= 0x2000
PAGE_DIR_ADDR	= 0x3000
PAGE_ENTRY_ADDR	= 0x4000
PAGE_FLAG	= 0x80000000

.text
.code32

.globl		start32, setup_idt, enable_page
.globl		get_cr0, get_cr2, get_cr3 
.globl		_start32, _setup_idt, _enable_page
.globl		_get_cr0, _get_cr2, _get_cr3, _set_cr3

.globl _ignore_handler
.globl ignore_handler


_start32:
start32:
	movb $0x41, 0xB8000      # 'A'をVGAテキストバッファに書き込む（デバッグ表示）
	movl $0xB8000, %edi      # VGAテキストバッファの先頭アドレス
	movb $'A', (%edi)        # 'A'を書き込む
	movb $0x07, 1(%edi)      # 属性（白地に黒）
	.byte 0xb4, 0x0e        # mov ah,0x0e
	.byte 0xb0, 0x41        # mov al,'A'
	.byte 0xcd, 0x10        # int 0x10
	cli
	movl $0x10, %eax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	cli
	lss stack_ptr, %esp
	pushl $0
	pushl $0
	pushl $0
	cli
	cld
	call __main
fatal:
	jmp fatal

_setup_idt:
setup_idt:
	lea	_ignore_handler,%edx
	movl	$0x00080000,%eax
	movw	%dx,%ax
	movw	$0x8E00,%dx
	movl	$IDT,%edi
	mov	$256,%ecx
rp_sidt:
	movl	%eax,(%edi)
	movl	%edx,4(%edi)
	addl	$8,%edi
	dec	%ecx
	jne	rp_sidt
	ret

_enable_page:
enable_page:
	mov	$PAGE_DIR_ADDR, %eax
	mov	%eax, %cr3
	mov	%cr0, %eax
	or	$PAGE_FLAG, %eax
	mov	%eax, %cr0
	jmp	pflush
pflush:
	ret

_get_cr0:
get_cr0:
	mov	%cr0, %eax
	ret

_get_cr2:
get_cr2:
	mov	%cr2, %eax
	ret

_get_cr3:
get_cr3:
	mov	%cr3, %eax
	ret

_set_cr3:
set_cr3:
	movl	4(%esp),%eax
	movl	%eax,%cr3
	ret

.align 4
	.word	4
idt_descr:
	.word 256*8-1
	.long IDT
.align 4
gdt_descr:
	.word	256*8-1
	.word	0x1000
	.word	0

.data
.align 4
stack_ptr:
	.long	0x00070000
	.word	0x10
.align 2
start_message:	.ascii	"START 32 bit mode!"
	.word	0
.align 2




