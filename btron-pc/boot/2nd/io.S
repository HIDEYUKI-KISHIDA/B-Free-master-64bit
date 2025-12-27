/****************************************************************************
 *
 *		I/O ���󥿥ե������ѤΥ롼����
 *
 * $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/boot/2nd/io.S,v 1.1 2011/12/27 17:13:35 liu1 Exp $
 *
 * $Log: io.S,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.1  1999-03-15 02:59:18  monaka
 * *.s files renamed extension to *.S
 *
 * Revision 1.2  1997/06/29 13:14:48  night
 * �ؿ�̾�κǽ�ˤĤ��Ƥ��� '_' ������
 * (C ����ѥ���� ELF ���֥������Ȥ��ѹ��������Ȥˤ�뽤��)
 *
# Revision 1.1  1996/05/11  10:45:04  night
# 2nd boot (IBM-PC �� B-FREE OS) �Υ�������
#
 * Revision 1.1  1993/10/11 21:29:20  btron
 * btron/386
 *
# Revision 1.1.1.1  93/01/14  12:30:22  btron
# BTRON SYSTEM 1.0
# 
# Revision 1.1.1.1  93/01/13  16:50:28  btron
# BTRON SYSTEM 1.0
# 
 */

#include "location.h"

.globl	outb, outw, outl, inb, inw, inl
.globl	_outb, _outw, _outl, _inb, _inw, _inl

.text

.code32

/*
 *	output byte
 *
 *	����
 *		ioaddr: short
 *		data:	byte
 *
 *	����
 *		none
 *
 *	+-----------------------+  0 = sp
 *	|	bp		|
 *	+-----------------------+ +4
 *	|    return addr	|
 *	+-----------------------+ +8
 *	|	arg1		|	ioaddr
 *	+-----------------------+ +12
 *	|	arg2		| 	data
 *	+-----------------------+
 */
_outb:
outb:
		pushl	%ebp
		movl	%esp, %ebp
		movl	8(%ebp), %edx
		andl	$0xffff, %edx
		movl	12(%ebp), %eax
/*		andl	$0xff, %eax */
		outb	%al, %dx
		leave
		ret

/*
 *	output word
 *
 *	����
 *		ioaddr: short
 *		data:	short
 *
 *	����
 *		none
 *
 *	+-----------------------+  0 = sp
 *	|	bp		|
 *	+-----------------------+ +4
 *	|    return addr	|
 *	+-----------------------+ +8
 *	|	arg1		|	ioaddr
 *	+-----------------------+ +12
 *	|	arg2		| 	data
 *	+-----------------------+
 */
_outw:
outw:
		pushl	%ebp
		movl	%esp, %ebp
		movl	8(%ebp), %edx
		movl	12(%ebp), %eax
		outw	%ax, %dx
		leave
		ret

/*
 *	output long
 *
 *	����
 *		ioaddr: short
 *		data:	byte
 *
 *	����
 *		none
 *
 *	+-----------------------+  0 = sp
 *	|	bp		|
 *	+-----------------------+ +4
 *	|    return addr	|
 *	+-----------------------+ +8
 *	|	arg1		|	ioaddr
 *	+-----------------------+ +12
 *	|	arg2		| 	data
 *	+-----------------------+
 */
_outl:
outl:
		pushl	%ebp
		movl	%esp, %ebp
		movl	8(%ebp), %edx
		movl	12(%ebp), %eax
		outl	%eax, %dx
		leave
		ret

/*
 *	input byte
 *
 *	����
 *		ioaddr: short
 *
 *	����
 *		data:	byte
 *
 *	+-----------------------+  0 = sp
 *	|	bp		|
 *	+-----------------------+ +4
 *	|    return addr	|
 *	+-----------------------+ +8
 *	|	arg1		|	ioaddr
 *	+-----------------------+
 */
_inb:
inb:
		pushl	%ebp
		movl	%esp, %ebp
		movl	8(%ebp), %edx
		movl	$0, %eax
		inb	%dx, %al
		leave
		ret


/*
 *	input word
 *
 *	����
 *		ioaddr: short
 *
 *	����
 *		data:	short
 *
 *	+-----------------------+  0 = sp
 *	|	bp		|
 *	+-----------------------+ +4
 *	|    return addr	|
 *	+-----------------------+ +8
 *	|	arg1		|	ioaddr
 *	+-----------------------+
 */
_inw:
inw:
		pushl	%ebp
		movl	%esp, %ebp
		movl	8(%ebp), %edx
		movl	$0, %eax
		inw	%dx, %ax
		leave
		ret


/*
 *	input long
 *
 *	����
 *		ioaddr: short
 *
 *	����
 *		data:	long
 *
 *	+-----------------------+  0 = sp
 *	|	bp		|
 *	+-----------------------+ +4
 *	|    return addr	|
 *	+-----------------------+ +8
 *	|	arg1		|	ioaddr
 *	+-----------------------+
 */
_inl:
inl:
		pushl	%ebp
		movl	%esp, %ebp
		movl	8(%ebp), %edx
		movl	$0, %eax
		inl	%dx, %eax
		leave
		ret

