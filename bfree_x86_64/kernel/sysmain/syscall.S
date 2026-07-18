# SYSCALL/SYSRETエントリ（GAS構文, 仕様書v2.4準拠）
#
# * System V ABI順（RDI, RSI, RDX, RCX, R8, R9）でCハンドラに渡す
# * RSPアラインメント・カーネルスタック切替は事前に確保済みとする
#
	.text
	.global knl_syscall_entry
knl_syscall_entry:
	# x86_64 syscall ABI:
	#   RAX=syscall番号, RDI,RSI,RDX,R10,R8,R9=引数
	# C ABIへ num,arg1,arg2,arg3,arg4,arg5 の順へ並べ替える。
	push %r11
	push %rcx
	mov %r8, %r9
	mov %r10, %r8
	mov %rdx, %rcx
	mov %rsi, %rdx
	mov %rdi, %rsi
	mov %rax, %rdi
	call knl_syscall_handler
	pop %rcx
	pop %r11
	# SYSRETでユーザに戻る
	sysretq

.section .note.GNU-stack,"",@progbits
