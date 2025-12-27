/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* itron_func.h - �ؿ������
 *
 */

#ifndef __ITRON_FUNC_H__
#define __ITRON_FUNC_H__	1

/* --------------------------------------------------------- */
/* port manager                                              */
/* --------------------------------------------------------- */
extern ER   find_port (B *name, ID *rport);
extern ID   get_port (W minsize, W maxsize);
extern ER   regist_port (B *name, ID port);
extern W    get_req (ID port, VP request, W *size);

extern void     bzero (B *bufp, W size);
extern void     bcopy (UB *buf1, UB *buf2, W size);
extern W        dbg_printf (B *fmt, ...);
extern ER       dbg_puts (B *msg);


extern void outb(UH, UB);
extern void outw(UH, UH);
extern void outl(UH, UW);
extern B inb(UH);
extern H inw(UH);
extern W inl(UH);

#endif /* __ITRON_FUNC_H__ */
