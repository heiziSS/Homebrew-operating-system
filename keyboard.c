#include "bootpack.h"

FIFO *keyfifo;
int keydata0;

/* 键盘中断 IRQ1 */
void inthandler21(int *esp)
{
	int data;
    io_out8(PIC0_OCW2, 0x61);   // 通知PIC IRQ-01已经受理完毕
    data = io_in8(PORT_KEYDAT);
    fifo_put(keyfifo, data + keydata0);
    return;
}

#define PORT_KEYSTA             0x0064
#define KEYSTA_SEND_NOTREADY    0x02
#define KEYCMD_WRITE_MODE       0x60
#define KBC_MODE                0x47

/* 等待键盘电路准备完毕 */
void wait_KBC_sendready(void)
{
    for (;;) {
        if ((io_in8(PORT_KEYSTA) & KEYSTA_SEND_NOTREADY) == 0) {
            return;
        }
    }
	return;
}

/* 初始化键盘控制电路 */
void init_keyboard(FIFO *fifo, int data0)
{
    //将fifo缓冲区的信息保存到全部变量中
    keyfifo = fifo;
    keydata0 = data0;

    wait_KBC_sendready();
    io_out8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
    wait_KBC_sendready();
    io_out8(PORT_KEYDAT, KBC_MODE);
    return;
}