#include "bootpack.h"
#include <stdio.h>

unsigned int memtest(unsigned int start, unsigned int end);
unsigned int memtest_sub(unsigned int start, unsigned int end);

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
    enable_mouse(&mdec);

    init_palette();
    init_screen8(binfo->vram, binfo->scrnx, binfo->scrny);
    mx = (binfo->scrnx - 16) >> 1;   // 坐标计算，使其位于屏幕中心
    my = (binfo->scrny - 28 - 16) >> 1;
    init_mouse_cursor8(mcursor, COL8_008484);
    putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16);
    sprintf(s, "(%3d, %3d)", mx, my);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s);

    i = memtest(0x00400000, 0xbfffffff) / (1024 * 1024); // 0x00400000以前的内存已经被使用了，参考8.5节内存分布图
    sprintf(s, "memory %dMB", i);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 32, COL8_FFFFFF, s);

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

#define EFLAGS_AC_BIT       0x00040000
#define CR0_CACHE_DISABLE   0x60000000

/*
    内存检查：往内存写值再马上读取，检查前后值是否相等来判断内存连接是否正常
    注：内存检查时需要将缓存设为OFF，否则写入读出的值可能不是内存而是缓存，结果所有内存都 “正常”
*/
unsigned int memtest(unsigned int start, unsigned int end)
{
    char flg486 = 0;
    unsigned int eflg, cr0, i;
    
    /* 确认CPU是386还是486以上的，486以上才有缓存 */
    eflg = io_load_eflags();
    eflg |= EFLAGS_AC_BIT;  // AC bit 为 1
    io_store_eflags(eflg);
    eflg = io_load_eflags();
    if ((eflg & EFLAGS_AC_BIT) != 0) { // 如果是386，即使设定AC=1，AC值还会自动回到0
        flg486 = 1;
    }
    eflg &= ~EFLAGS_AC_BIT; // AC bit设为 0
    io_store_eflags(eflg);

    if (flg486 != 0) {
        cr0 = load_cr0();
        cr0 |= CR0_CACHE_DISABLE;   // 关闭缓存
        store_cr0(cr0);
    }

    i = memtest_sub(start, end);

    if (flg486 !=  0) {
        cr0 = load_cr0();
        cr0 &= ~CR0_CACHE_DISABLE;  // 开启缓存
        store_cr0(cr0);
    }

    return i;
}

/*
    内存读写测试
    返回start~end内存地址中以start为起始点的有效结束地址
*/
unsigned int memtest_sub(unsigned int start, unsigned int end)
{
    unsigned int i, *p, old, pat0 = 0xaa55aa55, pat1 = 0x55aa55aa;
    for (i = start; i <= end; i += 0x1000) {
        p = (unsigned int *) (i + 0xffc);   // 为提高检查效率，每次只检查0x1000字节中的末尾4个字节
        old = *p;           // 保存修改前的值
        *p = pat0;          // 试写
        *p ^= 0xffffffff;   // 反转
        if (*p != pat1) {   // 检查反转结果
not_memory:
            *p = old;
            break;
        }
        *p ^= 0xffffffff;   // 再次反转
        if (*p != pat0) {
            goto not_memory;
        }
        *p = old;           // 恢复为修改前的值
    }
    return i;
}