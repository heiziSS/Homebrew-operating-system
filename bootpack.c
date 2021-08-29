#include "bootpack.h"
#include <stdio.h>

#define MEMMAN_FREES        4090    // 大约是32KB
#define MEMMAN_ADDR         0x003c0000

typedef struct {
    unsigned int addr, size;
} FREEINFO;     // 空余内存的信息

typedef struct {
    int frees, maxfrees, lostsize, losts;
    FREEINFO free[MEMMAN_FREES];
} MEMMAN;       // 内存管理

unsigned int memtest(unsigned int start, unsigned int end);
void memman_init(MEMMAN *man);
unsigned int memman_total(MEMMAN *man);
unsigned int memman_alloc(MEMMAN *man, unsigned int size);
int memman_free(MEMMAN *man, unsigned int addr, unsigned int size);

void HariMain(void)
{
    BOOTINFO *binfo = (BOOTINFO *) ADR_BOOTINFO;
    char s[40], mcursor[256], keybuf[32], mousebuf[128];
    int mx, my, i;
    unsigned int memtotal;
    MOUSE_DEC mdec;
    MEMMAN *memman = (MEMMAN *) MEMMAN_ADDR;
    
    init_gdtidt();
    init_pic();
    io_sti(); //由于 IDT/PIC 初始化完成，因此取消了 CPU 中断禁令

    fifo8_init(&keyfifo, 32, keybuf);
    fifo8_init(&mousefifo, 128, mousebuf);
    io_out8(PIC0_IMR, 0xf9);    // 允许 PIC1 和键盘（11111001）
    io_out8(PIC1_IMR, 0xef);    // 允许鼠标（11101111）

    init_keyboard();
    enable_mouse(&mdec);
    memtotal = memtest(0x00400000, 0xbfffffff); // 0x00400000以前的内存已经被使用了，参考8.5节内存分布图
    memman_init(memman);
    memman_free(memman, 0x00001000, 0x0009e000);    // 0x00001000 - 0x0009efff
    memman_free(memman, 0x00400000, memtotal - 0x00400000);

    init_palette();
    init_screen8(binfo->vram, binfo->scrnx, binfo->scrny);
    mx = (binfo->scrnx - 16) >> 1;   // 坐标计算，使其位于屏幕中心
    my = (binfo->scrny - 28 - 16) >> 1;
    init_mouse_cursor8(mcursor, COL8_008484);
    putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16);
    sprintf(s, "(%3d, %3d)", mx, my);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s);

    sprintf(s, "memory %dMB  free: %dKB", memtotal / (1024 * 1024), memman_total(memman) / 1024);
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

/* 初始化内存管理列表 */
void memman_init(MEMMAN *man)
{
    man->frees = 0;         // 可用信息数目
    man->maxfrees = 0;      // 用于观察可用状况：frees的最大值
    man->lostsize = 0;      // 释放失败的内存的大小总和
    man->losts = 0;         // 释放失败次数
    return;
}

/* 统计空余内存大小 */
unsigned int memman_total(MEMMAN *man)
{
    unsigned int i, t = 0;
    for (i = 0; i < man->frees; i++) {
        t += man->free[i].size;
    }
    return t;
}

/* 分配内存 */
unsigned int memman_alloc(MEMMAN *man, unsigned int size)
{
    unsigned int i, a;
    for (i = 0; i < man->frees; i++) {
        if (man->free[i].size >= size) {
            a = man->free[i].addr;
            man->free[i].addr += size;
            man->free[i].size -= size;
            if (man->free[i].size == 0) {   // 剩余空间为0，则删除该条信息
                man->frees--;
                for (; i < man->frees; i++) {
                    man->free[i] = man->free[i + 1];    // 内存管理列表中后面的信息全部前移一位
                }
            }
            return a;
        }
    }
    return 0;   // 无可分配空间
}

/* 释放内存 */
int memman_free(MEMMAN *man, unsigned int addr, unsigned int size)
{
    int i, j;

    /* 为便于归纳内存，将free[]按照addr的顺序排列 */
    /* 所以，先决定应该放在哪里 */
    for (i = 0; i < man->frees; i++) {
        if (man->free[i].addr > addr) {
            break;
        }
    }
    // free[i-1].addr < addr < free[i].addr

    // 情况一
    if (i > 0) { // 前面有可用内存
        if (man->free[i-1].addr + man->free[i-1].size == addr) { // 可以和前面的可用内存归纳到一起
            man->free[i-1].size += size;
            if (i < man->frees) {   // 后面也有可用内存
                if (addr + size == man->free[i].addr) { // 也可以和后面的可用内存归纳到一起
                    man->free[i-1].size += man->free[i].size;
                    // 删除 man->free[i]
                    man->frees--;
                    for (; i < man->frees; i++) {
                        man->free[i] = man->free[i+1];  // 内存管理列表中后面的信息全部前移一位
                    }
                }
            }
            return 0;
        }
    }

    // 情况二：不能与前面的可用空间归纳到一起
    if (i < man->frees) { // 后面有可用内存
        if (addr + size == man->free[i].addr) { // 可以与后面的可用内存归纳到一起
            man->free[i].addr = addr;
            man->free[i].size += size;
            return 0;
        }
    }

    // 情况三：不能与前面的归纳到一起，也不能与后面的归纳到一起
    if (man->frees < MEMMAN_FREES) {
        // free[i]之后的全部后移一位
        for (j = man->frees; j > i; j--) {
            man->free[j] = man->free[j-1];
        }
        man->frees++;
        if (man->maxfrees < man->frees) {
            man->maxfrees = man->frees; //更新最大值
        }
        man->free[i].addr = addr;
        man->free[i].size = size;
        return 0;
    }

    // 内存释放失败，内存列表已满，直接放弃释放该空间，等列表空闲后再释放
    man->losts++;
    man->lostsize += size;
    return -1;
}
