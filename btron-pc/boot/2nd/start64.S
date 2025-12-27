/***********************************************************************


  			S T A R T 6 4


 64-bit mode boot code

 ************************************************************************/

/* Memory addresses for 64-bit mode */
IDT_ADDR	= 0x2000
PAGE_DIR_ADDR	= 0x3000
PAGE_ENTRY_ADDR	= 0x4000
PAGE_PML4_ADDR	= 0x5000		/* PML4 (Page Map Level 4) for 64-bit */
PAGE_PDPT_ADDR	= 0x6000		/* PDPT (Page Directory Pointer Table) */

.text

.globl		start64, setup_idt64, enable_paging64
.globl		get_rax, get_rax, get_rsp, get_rbp
.globl		get_cr0_64, get_cr3_64, get_cr4_64

start64:
		cli
		/* Load kernel GDT and IDT for 64-bit mode */
		lgdt	gdt64_ptr
		lidt	idt64_ptr
		
		/* Set RSP to kernel stack */
		movq	$0x00080000, %rsp
		xorq	%rbp, %rbp
		
		/* Clear registers */
		xorq	%rax, %rax
		xorq	%rbx, %rbx
		xorq	%rcx, %rcx
		xorq	%rdx, %rdx
		xorq	%rsi, %rsi
		xorq	%rdi, %rdi
		
		cld
		/* Jump to main kernel code */
		movq	$_main64, %rax
		jmp	*%rax

fatal64:
		hlt
		jmp	fatal64

/***********************************************************************
 * setup_idt64 - Setup IDT for 64-bit mode
 *
 * Parameters: None
 * Returns: None
 */
setup_idt64:
		leaq	ignore_handler64, %rdx
		movq	$0x0000088E00000000, %rax	/* interrupt gate */
		movq	$IDT_ADDR, %rdi
		movl	$256, %ecx
rp_sidt64:
		movq	%rdx, (%rdi)
		addq	$16, %rdi
		decl	%ecx
		jne	rp_sidt64
		ret

/***********************************************************************
 * enable_paging64 - Enable 64-bit paging
 *
 * Parameters: None
 * Returns: None
 */
enable_paging64:
		/* Load PML4 address into CR3 */
		movq	$PAGE_PML4_ADDR, %rax
		movq	%rax, %cr3
		
		/* Check if PAE is supported */
		movl	$0x1, %eax
		cpuid
		testl	$(1 << 6), %edx		/* Check PAE bit */
		jz	no_pae64
		
		/* Enable PAE */
		movq	%cr4, %rax
		orq	$(1 << 5), %rax		/* Set PAE bit */
		movq	%rax, %cr4
		
		/* Enable 64-bit mode via EFER MSR */
		movl	$0xC0000080, %ecx	/* EFER MSR */
		rdmsr
		orq	$(1 << 8), %rax		/* Set LME bit (Long Mode Enable) */
		orq	$(1 << 11), %rax	/* Set NXE bit (NX Enable) */
		wrmsr
		
		/* Enable paging */
		movq	%cr0, %rax
		orq	$(1 << 31), %rax	/* Set PG bit */
		movq	%rax, %cr0
		
		jmp	paging_enabled64
paging_enabled64:
		ret

no_pae64:
		/* PAE not supported - halt */
		hlt
		jmp	no_pae64

/***********************************************************************
 * get_cr0_64 - Get CR0 register (64-bit mode)
 */
get_cr0_64:
		movq	%cr0, %rax
		ret

/***********************************************************************
 * get_cr3_64 - Get CR3 register (64-bit mode)
 */
get_cr3_64:
		movq	%cr3, %rax
		ret

/***********************************************************************
 * get_cr4_64 - Get CR4 register (64-bit mode)
 */
get_cr4_64:
		movq	%cr4, %rax
		ret

/***********************************************************************
 * get_rax - Get RAX register value
 */
get_rax:
		movq	%rax, %rax
		ret

/***********************************************************************
 * get_rsp - Get RSP register value
 */
get_rsp:
		movq	%rsp, %rax
		ret

/***********************************************************************
 * get_rbp - Get RBP register value
 */
get_rbp:
		movq	%rbp, %rax
		ret

.align 16
/* 64-bit GDT */
.globl	gdt64
gdt64:
		/* Null descriptor */
		.quad	0x0000000000000000
		
		/* Code segment (CS) */
		.quad	0x00AF9A000000FFFF	/* L=1, DB=0, Present, Code, Read */
		
		/* Data segment (DS) */
		.quad	0x00AF92000000FFFF	/* L=1, DB=0, Present, Data, Write */
		
		/* TSS descriptor (will be filled at runtime) */
		.quad	0x0000000000000000
		.quad	0x0000000000000000

gdt64_limit = (gdt64 - . - 1)

.align 8
gdt64_ptr:
		.word	gdt64_limit
		.quad	gdt64

.align 16
/* 64-bit IDT */
.globl	idt64
idt64:
		.rept	256
		.quad	0x0000088E0000F000	/* Interrupt gate, kernel code segment, offset 0xF000 */
		.quad	0x0000000000000000
		.endr

idt64_limit = (idt64 - . - 1)

.align 8
idt64_ptr:
		.word	idt64_limit
		.quad	idt64

.align 8
ignore_handler64:
		iretq

/***********************************************************************
 * Initialize paging structures for 64-bit mode
 * 
 * Sets up PML4 -> PDPT -> PD -> PT hierarchy
 * Maps kernel virtual address space to physical address space
 */
.globl	init_paging64
init_paging64:
		pushq	%rbp
		movq	%rsp, %rbp
		
		/* Initialize PML4 */
		movq	$PAGE_PML4_ADDR, %rdi
		xorq	%rax, %rax
		movl	$512, %ecx
		cld
		repq	stosq
		
		/* Set first PML4 entry to point to PDPT */
		movq	$PAGE_PML4_ADDR, %rax
		movq	$PAGE_PDPT_ADDR, %rcx
		orq	$0x003, %rcx		/* Present, Writable */
		movq	%rcx, (%rax)
		
		/* Initialize PDPT */
		movq	$PAGE_PDPT_ADDR, %rdi
		xorq	%rax, %rax
		movl	$512, %ecx
		cld
		repq	stosq
		
		/* Set PDPT entries to point to page directories */
		movq	$PAGE_PDPT_ADDR, %rax
		movq	$PAGE_DIR_ADDR, %rcx
		orq	$0x003, %rcx		/* Present, Writable */
		movl	$4, %edx		/* Number of 1GB pages to map */
map_pdpt:
		movq	%rcx, (%rax)
		addq	$8, %rax
		addq	$0x1000, %rcx
		decl	%edx
		jne	map_pdpt
		
		popq	%rbp
		ret

/***********************************************************************
 * Setup long mode and transition from 32-bit to 64-bit mode
 */
.globl	setup_longmode
setup_longmode:
		/* Save current state */
		pushq	%rax
		pushq	%rbx
		pushq	%rcx
		pushq	%rdx
		pushq	%rsi
		pushq	%rdi
		
		/* Check for 64-bit support */
		movl	$0x80000001, %eax
		cpuid
		testl	$(1 << 29), %edx	/* Check LM bit */
		jz	no_longmode
		
		/* Initialize paging structures */
		call	init_paging64
		
		/* Enable long mode */
		call	enable_paging64
		
		/* Restore state */
		popq	%rdi
		popq	%rsi
		popq	%rdx
		popq	%rcx
		popq	%rbx
		popq	%rax
		ret

no_longmode:
		/* 64-bit not supported - halt */
		hlt
		jmp	no_longmode

.data
.align 4
stack_ptr64:
		.quad	0x00080000	/* 512KB stack */

.align 2
start_message64:
		.ascii	"START 64 bit mode!"
		.word	0
