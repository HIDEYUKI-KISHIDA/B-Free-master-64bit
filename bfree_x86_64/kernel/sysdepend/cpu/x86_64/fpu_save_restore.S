# セキュリティ警告抑制用ディレクティブ
.section .note.GNU-stack,"",@progbits
#-------------------------------------------------------------
# fpu_save_restore.S - x86_64 XSAVE/XRSTOR FPU/SSE保存・復元
# 仕様: TK2_x86_64_Spec.md v2.4準拠
#
# 修正: rfbmをC側グローバル g_fpu_rfbm から直接ロードする。
#       xsave/xrstor命令はマスクを edx:eax で受け取る（上位32bitがedx、下位32bitがeax）。
#-------------------------------------------------------------
	.section .text
	.global knl_save_fpu_state
	.global knl_restore_fpu_state
	.extern g_fpu_rfbm
	.extern g_fpu_save_mode

#-------------------------------------------------------------
# void knl_save_fpu_state(void *buf)
#   - buf: 64byteアライン済み領域 (RDI)
#   - rfbmはグローバル g_fpu_rfbm から取得
#-------------------------------------------------------------
	.global knl_save_fpu_state
	.type   knl_save_fpu_state, @function
knl_save_fpu_state:
	# buf を RSI へ退避（xsave の後で使いやすいように）
	movq    %rdi, %rsi

	# g_fpu_rfbm を rdx:rax にロード (xsave はマスクを edx:eax で受け取る)
	movq    g_fpu_rfbm(%rip), %rax
	movq    %rax, %rdx
	shrq    $32, %rdx        # 上位32bit -> edx
	# eax は下位32bitがそのまま残っている

	movl    g_fpu_save_mode(%rip), %ecx
	cmp     $2, %ecx
	je      .Lsave_xsaveopt
	cmp     $1, %ecx
	je      .Lsave_xsave
	# FXSAVE (g_fpu_save_mode == 0)
	fxsave64 (%rsi)
	ret
.Lsave_xsaveopt:
	xsaveopt64 (%rsi)
	ret
.Lsave_xsave:
	xsave64 (%rsi)
	ret

#-------------------------------------------------------------
# void knl_restore_fpu_state(void *buf)
#   - buf: 64byteアライン済み領域 (RDI)
#   - rfbmはグローバル g_fpu_rfbm から取得
#-------------------------------------------------------------
	.global knl_restore_fpu_state
	.type   knl_restore_fpu_state, @function
knl_restore_fpu_state:
	# buf を RSI へ退避
	movq    %rdi, %rsi

	# g_fpu_rfbm を rdx:rax にロード (xrstor はマスクを edx:eax で受け取る)
	movq    g_fpu_rfbm(%rip), %rax
	movq    %rax, %rdx
	shrq    $32, %rdx        # 上位32bit -> edx

	movl    g_fpu_save_mode(%rip), %ecx
	cmp     $1, %ecx
	jge     .Lrestore_xrstor
	# FXRSTOR (g_fpu_save_mode == 0)
	fxrstor64 (%rsi)
	ret
.Lrestore_xrstor:
	# XRSTOR (XSAVE / XSAVEOPT 共通)
	xrstor64 (%rsi)
	ret
