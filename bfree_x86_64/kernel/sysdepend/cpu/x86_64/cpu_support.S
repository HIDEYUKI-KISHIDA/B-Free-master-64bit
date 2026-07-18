# セキュリティ警告抑制用ディレクティブ
.section .note.GNU-stack,"",@progbits
#-------------------------------------------------------------
# cpu_support.S - x86_64 T-Kernel2.0 ディスパッチ/タスク切替
# 仕様: TK2_x86_64_Spec.md v2.3準拠
#-------------------------------------------------------------
	.section .text
	.global knl_start_dispatch
	.global knl_dispatch

# 外部シンボル
	.extern knl_current_task
	.extern knl_dispatch_main
	.extern knl_kernel_stack_top
# TCBの tskctxb.ssp オフセット（必要に応じて修正）
.set tskctxb_ssp_offset, 0x00

#-------------------------------------------------------------
# knl_start_dispatch - 最初のタスクを開始
#   knl_current_task->tskctxb.ssp からRSP復元
#   T_REGS逆順でpopq、iretqで開始
#-------------------------------------------------------------
knl_start_dispatch:
	movq knl_current_task(%rip), %rax
	movq (tskctxb_ssp_offset)(%rax), %rsp
	# T_REGS逆順でpopq（RAX〜R15, RBP）
	popq %rbp
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	# ここでスタックトップにRIP, CS, RFLAGS, RSP, SSが積まれていること
	iretq

#-------------------------------------------------------------
# knl_dispatch - 自発的ディスパッチ
#   IRQ_ENTRYと同様にレジスタ保存し、スケジューラ呼び出し
#   iretq直前のフレーム整合性に注意
#-------------------------------------------------------------
knl_dispatch:
	pushq %rax
	pushq %rbx
	pushq %rcx
	pushq %rdx
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	pushq %rbp
	# iretqフレームを積む（RIP, CS, RFLAGS, RSP, SS）
	# ...必要に応じてpushq/pushfqで積む...
	# スケジューラ呼び出し（C関数: knl_dispatch_main）
	movq %rsp, %rdi   # T_REGS*渡す
	call knl_dispatch_main
	# 新タスクのスタックに切替済み
	# T_REGS逆順でpopq（RAX〜R15, RBP）
	popq %rbp
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	# ここでスタックトップにRIP, CS, RFLAGS, RSP, SSが積まれていること
	iretq
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	iretq
