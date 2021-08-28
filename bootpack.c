#include "bootpack.h"
#include <stdio.h>

typedef struct {
    unsigned char buf[3], phase;
    int x, y, btn;
} MOUSE_DEC;

extern FIFO8 keyfifo, mousefifo;
void init_keyboard(void);
void enable_mouse(MOUSE_DEC *mdec);
int mouse_decode(MOUSE_DEC *mdec, unsigned char dat);

void HariMain(void)
{
    BOOTINFO *binfo = (BOOTINFO *) ADR_BOOTINFO;
    char s[40], mcursor[256], keybuf[32], mousebuf[128];
    int mx, my, i;
    MOUSE_DEC mdec;
    
    init_gdtidt();
    init_pic();
    io_sti(); //由于 IDT/PIC 初始化完成，因此取消了 CPU 中断禁令

    fifo8_init(&keyfifo, 32, keybuf);
    fifo8_init(&mousefifo, 128, mousebuf);
    io_out8(PIC0_IMR, 0xf9);    // 允许 PIC1 和键盘（11111001）
    io_out8(PIC1_IMR, 0xef);    // 允许鼠标（11101111）

    init_keyboard();

    init_palette();
    init_screen8(binfo->vram, binfo->scrnx, binfo->scrny);
    mx = (binfo->scrnx - 16) >> 1;   // 坐标计算，使其位于屏幕中心
    my = (binfo->scrny - 28 - 16) >> 1;
    init_mouse_cursor8(mcursor, COL8_008484);
    putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16);
    sprintf(s, "(%d, %d)", mx, my);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s);

    enable_mouse(&mdec);

    for (;;) {
        io_cli();
        if (fifo8_status(&keyfifo) + fifo8_status(&mousefifo) == 0) {
            io_stihlt();
        } else {
            if (fifo8_status(&keyfifo) != 0) {
                i = fifo8_get(&keyfifo);
                io_sti();
                sprintf(s, "%02X", i);
                boxfill8(binfo->vram, binfo->scrnx, COL8_008484, 0, 16, 15, 31);
                putfonts8_asc(binfo->vram, binfo->scrnx, 0, 16, COL8_FFFFFF, s);
            } else {
                i = fifo8_get(&mousefifo);
                io_sti();
                if (mouse_decode(&mdec, i) == 1) {
                    /* 鼠标的3个字节集齐，显示出来 */
                    sprintf(s, "[lcr %4d %4d]", mdec.x, mdec.y);
                    if ((mdec.btn & 0x1) != 0) {
                        s[1] = 'L';
                    }
                    if ((mdec.btn & 0x2) != 0) {
                        s[3] = 'R';
                    }
                    if ((mdec.btn & 0x4) != 0) {
                        s[2] = 'C';
                    }
                    boxfill8(binfo->vram, binfo->scrnx, COL8_008484, 32, 16, 32+15*8-1, 31);
                    putfonts8_asc(binfo->vram, binfo->scrnx, 32, 16, COL8_FFFFFF, s);
                    // 鼠标指针的移动
                    boxfill8(binfo->vram, binfo->scrnx, COL8_008484, mx, my, mx + 15, my + 15); //隐藏鼠标
                    mx += mdec.x;
                    my += mdec.y;
                    if (mx < 0) {
                        mx = 0;
                    }
                    if (my < 0) {
                        my = 0;
                    }
                    if (mx > binfo->scrnx - 16) {
                        mx = binfo->scrnx - 16;
                    }
                    if (my > binfo->scrny - 16) {
                        my = binfo->scrny - 16;
                    }
                    sprintf(s, "(%3d, %3d)", mx, my);
                    boxfill8(binfo->vram, binfo->scrnx, COL8_008484, 0, 0, 0+10*8-1, 15);
                    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s); // 刷新坐标
                    putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16); //画鼠标
                }
            }
        }
    }
}

#define PORT_KEYDAT             0x0060
#define PORT_KEYSTA             0x0064
#define PORT_KEYCMD             0x0064
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
}

/* 初始化键盘控制电路 */
void init_keyboard(void)
{
    wait_KBC_sendready();
    io_out8(PORT_KEYCMD, KEYCMD_WRITE_MODE);
    wait_KBC_sendready();
    io_out8(PORT_KEYDAT, KBC_MODE);
    return;
}

#define KEYCMD_SENDTO_MOUSE     0xd4
#define MOUSECMD_ENABLE         0xf4

/* 激活鼠标 */
void enable_mouse(MOUSE_DEC *mdec)
{
    wait_KBC_sendready();
    io_out8(PORT_KEYCMD, KEYCMD_SENDTO_MOUSE);
    wait_KBC_sendready();
    io_out8(PORT_KEYDAT, MOUSECMD_ENABLE);
    mdec->phase = 0;
    return; // 顺利的话，键盘控制其会返送回ACK(0xfa)
}

/* 解析鼠标数据 */
int mouse_decode(MOUSE_DEC *mdec, unsigned char dat)
{
    if (mdec->phase == 0) { // 等待鼠标的0xfa状态
        if (dat == 0xfa)  mdec->phase = 1;
        return 0;
    } else if (mdec->phase == 1) {  //等待鼠标的第一字节
        mdec->buf[0] = dat;
        mdec->phase = 2;
        return 0;
    } else if (mdec->phase == 2) {  // 等待鼠标的第二字节
        mdec->buf[1] = dat;
        mdec->phase = 3;
        return 0;
    } else if (mdec->phase == 3) {  // 等待鼠标的第三字节
        mdec->buf[2] = dat;
        mdec->phase = 1;

        // 解析鼠标数据
        mdec->btn = mdec->buf[0] & 0x07;
        mdec->x = mdec->buf[1];
        mdec->y = mdec->buf[2];
        if ((mdec->buf[0] & 0x10) != 0) {
            mdec->x |= 0xffffff00;
        }
        if ((mdec->buf[0] & 0x20) != 0) {
            mdec->y |= 0xffffff00;
        }
        mdec->y = -1 * mdec->y; // 鼠标的y方向与画面符号方向
        return 1;
    }
    return -1;
}