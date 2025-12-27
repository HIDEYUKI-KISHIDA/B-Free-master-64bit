#ifndef INTERRUPT_H
#define INTERRUPT_H

/* 割り込み番号 */
#define INT_KEYBOARD 33
#define INT_FD 38
#define INT_IDE 46
#define INT_IDE2 47

/* 8259A PIC定数 */
#define MASTER_8259A_COM 0x20
#define MASTER_8259A_DATA 0x21
#define SLAVE_8259A_COM 0xA0
#define SLAVE_8259A_DATA 0xA1

/* 割り込みハンドラのプロトタイプ */
void int33_handler(void);
void intr_keyboard(void);
void intr_fd(void);
void intr_ide(void);
void int38_handler(void);
void ignore_handler(void);

/* 割り込み制御関数のダミープロトタイプ */
void reset_intr_mask(int n);
int wait_int(int *flag);

#endif /* INTERRUPT_H */


