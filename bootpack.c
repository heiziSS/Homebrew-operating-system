#include "bootpack.h"
#include <stdio.h>

void make_window8(unsigned char *buf, int xsize, int ysize, char *title);
void putfonts8_asc_sht(SHEET *sht, int x, int y, int color, int backColor, char *str, int strLen);
void make_textbox8(SHEET *sht, int x0, int y0, int sx, int sy, int c);

void task_b_main(void);

/*
    任务状态段（task status segment）
    TSS包含26个int成员，总计104字节
    backlink ~ cr3: 任务设置相关的信息，切换任务时这些成员不会被写入，backlink除外，某些情况下会被写入
    第2行是32位寄存器，第3行是16位寄存器
    eip：extended instruction pointer 扩展指令指针寄存器
    sep：栈地址
    ldtr ~ iomap: 任务设置相关信息

    任务切换：
    需要要用到指令JMP，该指令分为两种：只改写EIP的称为near模式，同时改写EIP和CS的称为far模式。
    JMP指令指定的目标地址不是可执行的代码，而是TSS的话，CPU就不会执行通常的改写EIP和CS的操作，
    而是将这条指令理解为任务切换。
*/
typedef struct {
    int backlink, esp0, ss0, esp1, ss1, esp2, ss2, cr3;
    int eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    int es, cs, ss, ds, fs, gs;
    int ldtr, iomap;
} TSS32;

void HariMain(void)
{
    BOOTINFO *binfo = (BOOTINFO *) ADR_BOOTINFO;
    FIFO fifo;
    char s[40];
    int fifobuf[128];
    TIMER *timer, *timer2, *timer3;
	int mx, my, i;
    unsigned int memtotal;
    MOUSE_DEC mdec;
    MEMMAN *memman = (MEMMAN *) MEMMAN_ADDR;
    SHTCTL *shtctl;
    SHEET *sht_back, *sht_mouse, *sht_win;
    unsigned char *buf_back, buf_mouse[256], *buf_win;
    int cursor_x, cursor_c;
	static char keytable[0x54] = {
		0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '^', 0,   0,
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '@', '[', 0,   0,   'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', ':', 0,   0,   ']', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.'
	};
    TSS32 tss_a, tss_b;
    SEGMENT_DESCRIPTOR *gdt = (SEGMENT_DESCRIPTOR *) ADR_GDT;
    int task_b_esp;
    
    init_gdtidt();
    init_pic();
    io_sti(); //由于 IDT/PIC 初始化完成，因此取消了 CPU 中断禁令
    fifo_init(&fifo, 128, fifobuf);
    init_pit();
    init_keyboard(&fifo, 256);
    enable_mouse(&fifo, 512, &mdec);
    io_out8(PIC0_IMR, 0xf8);    // 允许PIT(IRQ0), PIC1(IRQ2) 和键盘(IRQ1)（11111000）
    io_out8(PIC1_IMR, 0xef);    // 允许鼠标(IRQ12)（11101111）

    timer = timer_alloc();
    timer_init(timer, &fifo, 10);
    timer_settime(timer, 1000);

    timer2 = timer_alloc();
    timer_init(timer2, &fifo, 3);
    timer_settime(timer2, 300);

    timer3 = timer_alloc();
    timer_init(timer3, &fifo, 1);
    timer_settime(timer3, 50);

    memtotal = memtest(0x00400000, 0xbfffffff); // 0x00400000以前的内存已经被使用了，参考8.5节内存分布图
    memman_init(memman);
    memman_free(memman, 0x00001000, 0x0009e000);    // 0x00001000 - 0x0009efff
    memman_free(memman, 0x00400000, memtotal - 0x00400000);

    init_palette();
    shtctl = shtctl_init(memman, binfo->vram ,binfo->scrnx, binfo->scrny);
    sht_back = sheet_alloc(shtctl);
    sht_mouse = sheet_alloc(shtctl);
    sht_win = sheet_alloc(shtctl);
    buf_back = (unsigned char *) memman_alloc_4k(memman, binfo->scrnx * binfo->scrny);
    buf_win = (unsigned char *) memman_alloc_4k(memman, 160 * 52);
    sheet_setbuf(sht_back, buf_back, binfo->scrnx, binfo->scrny, -1);
    sheet_setbuf(sht_mouse, buf_mouse, 16, 16, 99);
    sheet_setbuf(sht_win, buf_win, 160, 52, -1);
    init_screen8(buf_back, binfo->scrnx, binfo->scrny);
    init_mouse_cursor8(buf_mouse, 99);
    make_window8(buf_win, 160, 52, "window");
    make_textbox8(sht_win, 8, 28, 144, 16, COL8_FFFFFF);
    cursor_x = 8;
    cursor_c = COL8_FFFFFF;
    sheet_slide(sht_back, 0, 0);
    mx = (binfo->scrnx - 16) >> 1;   // 坐标计算，使其位于屏幕中心
    my = (binfo->scrny - 28 - 16) >> 1;
    sheet_slide(sht_mouse, mx, my);
    sheet_slide(sht_win, 80, 72);
    sheet_updown(sht_back, 0);
    sheet_updown(sht_win, 1);
    sheet_updown(sht_mouse, 2);
    sprintf(s, "(%3d, %3d)", mx, my);
    putfonts8_asc_sht(sht_back, 0, 0, COL8_FFFFFF, COL8_008484, s, 10);
    sprintf(s, "memory %dMB  free: %dKB", memtotal / (1024 * 1024), memman_total(memman) / 1024);
    putfonts8_asc_sht(sht_back, 0, 32, COL8_FFFFFF, COL8_008484, s, 40);

    tss_a.ldtr = 0;
    tss_a.iomap = 0x40000000;
    tss_b.ldtr = 0;
    tss_b.iomap = 0x40000000;
    set_segmdesc(gdt + 3, 103, (int) &tss_a, AR_TSS32);
    set_segmdesc(gdt + 4, 103, (int) &tss_b, AR_TSS32);
    load_tr(3 * 8);
    task_b_esp = memman_alloc_4k(memman, 64 * 1024) + 64 * 1024; //为任务B分配64KB的内存，同时将ESP指向的地址移到栈末尾
    tss_b.eip = (int) &task_b_main; //定义切换到任务B时运行起始位置
    tss_b.eflags = 0x00000202;
    tss_b.eax = 0;
    tss_b.ecx = 0;
    tss_b.edx = 0;
    tss_b.ebx = 0;
    tss_b.esp = task_b_esp;
    tss_b.ebp = 0;
    tss_b.esi = 0;
    tss_b.edi = 0;
    tss_b.es = 1 * 8;
    tss_b.cs = 2 * 8;
    tss_b.ss = 1 * 8;
    tss_b.ds = 1 * 8;
    tss_b.fs = 1 * 8;
    tss_b.gs = 1 * 8;

    for (;;) {
        io_cli();
        if (fifo_status(&fifo) == 0) {
			io_stihlt();
        } else {
            i = fifo_get(&fifo);
            io_sti();
            if (256 <= i && i < 512) {
                sprintf(s, "%02X", i - 256);
                putfonts8_asc_sht(sht_back, 0, 16, COL8_FFFFFF, COL8_008484, s, 2);
                if (i < 0x54 + 256) {
                    if (keytable[i - 256] != 0 && cursor_x < 144) { // 一般字符
                        s[0] = keytable[i - 256];
                        s[1] = 0;
                        putfonts8_asc_sht(sht_win, cursor_x, 28, COL8_000000, COL8_FFFFFF, s, 1);
                        cursor_x += 8; // 显示完1个字符就前移1次光标
                    }
                }
                if (i == 0xe + 256 && cursor_x > 8) { //退格键
                    putfonts8_asc_sht(sht_win, cursor_x, 28, COL8_000000, COL8_FFFFFF, " ", 1);
                    cursor_x -= 8; // 用空格键把光标消去后，后移1次光标
                }
                // 光标再现
                boxfill8(sht_win->buf, sht_win->bxsize, cursor_c, cursor_x, 28, cursor_x + 7, 43);
                sheet_refresh(sht_win, cursor_x, 28, cursor_x + 8, 44);
            } else if (512 <= i && i <= 767) {
                i -= 512;
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
                    putfonts8_asc_sht(sht_back, 32, 16, COL8_FFFFFF, COL8_008484, s, 15);
                    // 鼠标指针的移动
                    mx += mdec.x;
                    my += mdec.y;
                    if (mx < 0) {
                        mx = 0;
                    }
                    if (my < 0) {
                        my = 0;
                    }
                    if (mx > binfo->scrnx - 1) {
                        mx = binfo->scrnx - 1;
                    }
                    if (my > binfo->scrny - 1) {
                        my = binfo->scrny - 1;
                    }
                    sprintf(s, "(%3d, %3d)", mx, my);
                    putfonts8_asc_sht(sht_back, 0, 0, COL8_FFFFFF, COL8_008484, s, 10);
                    sheet_slide(sht_mouse, mx, my);
                    if ((mdec.btn & 0x1) != 0) {
                        sheet_slide(sht_win, mx, my);
                    }
                }
            } else if (i == 10) { // 10秒定时器
                putfonts8_asc_sht(sht_back, 0, 64, COL8_FFFFFF, COL8_008484, "10[sec]", 7);
                taskswitch4();
            } else if (i == 3) { // 3秒定时器
                putfonts8_asc_sht(sht_back, 0, 80, COL8_FFFFFF, COL8_008484, "3[sec]", 6);
            } else if (i <= 1) {
                if (i == 1) { // 光标用时定时器
                    timer_init(timer3, &fifo, 0); //设置成0
                    cursor_c = COL8_000000;
                } else { // 光标用时定时器
                    timer_init(timer3, &fifo, 1); //设置成1
                    cursor_c = COL8_FFFFFF;
                }
                timer_settime(timer3, 50);
                boxfill8(sht_win->buf, sht_win->bxsize, cursor_c, cursor_x, 28, cursor_x + 7, 43);
                sheet_refresh(sht_win, cursor_x, 28, cursor_x + 8, 44);
            }
        }
    }
}

void make_window8(unsigned char *buf, int xsize, int ysize, char *title)
{
	static char closebtn[14][16] = {
		"OOOOOOOOOOOOOOO@",
		"OQQQQQQQQQQQQQ$@",
		"OQQQQQQQQQQQQQ$@",
		"OQQQ@@QQQQ@@QQ$@",
		"OQQQQ@@QQ@@QQQ$@",
		"OQQQQQ@@@@QQQQ$@",
		"OQQQQQQ@@QQQQQ$@",
		"OQQQQQ@@@@QQQQ$@",
		"OQQQQ@@QQ@@QQQ$@",
		"OQQQ@@QQQQ@@QQ$@",
		"OQQQQQQQQQQQQQ$@",
		"OQQQQQQQQQQQQQ$@",
		"O$$$$$$$$$$$$$$@",
		"@@@@@@@@@@@@@@@@"
	};
    int x, y;
    char c;
    boxfill8(buf, xsize, COL8_C6C6C6, 0, 0, xsize - 1, 0);
    boxfill8(buf, xsize, COL8_FFFFFF, 1, 1, xsize - 2, 1);
    boxfill8(buf, xsize, COL8_C6C6C6, 0, 0, 0, ysize - 1);
    boxfill8(buf, xsize, COL8_FFFFFF, 1, 1, 1, ysize - 2);
    boxfill8(buf, xsize, COL8_848484, xsize - 2, 1, xsize - 2, ysize - 2);
    boxfill8(buf, xsize, COL8_000000, xsize - 1, 0, xsize - 1, ysize - 1);
    boxfill8(buf, xsize, COL8_C6C6C6, 2, 2, xsize - 3, ysize - 3);
    boxfill8(buf, xsize, COL8_000084, 3, 3, xsize - 4, 20);
    boxfill8(buf, xsize, COL8_848484, 1, ysize - 2, xsize - 2, ysize - 2);
    boxfill8(buf, xsize, COL8_000000, 0, ysize - 1, xsize - 1, ysize - 1);
    putfonts8_asc(buf, xsize, 24, 4, COL8_FFFFFF, title);
    for (y = 0; y < 14; y++) {
        for (x = 0; x < 16; x++) {
            c = closebtn[y][x];
            if (c == '@') {
                c = COL8_000000;
            } else if (c == '$') {
                c = COL8_848484;
            } else if (c == 'Q') {
                c = COL8_C6C6C6;
            } else {
                c = COL8_FFFFFF;
            }
            buf[(5 + y) * xsize + (xsize - 21 + x)] = c;
        }
    }
    return;
}

void putfonts8_asc_sht(SHEET *sht, int x, int y, int color, int backColor, char *str, int strLen)
{
    boxfill8(sht->buf, sht->bxsize, backColor, x, y, x + strLen * 8 - 1, y + 15);
    putfonts8_asc(sht->buf, sht->bxsize, x, y, color, str);
    sheet_refresh(sht, x, y, x + strLen * 8, y + 16);
    return;
}

void make_textbox8(SHEET *sht, int x0, int y0, int sx, int sy, int c)
{
    int x1 = x0 + sx, y1 = y0 + sy;
    boxfill8(sht->buf, sht->bxsize, COL8_848484, x0 - 2, y0 - 3, x1 + 1, y0 - 3);
    boxfill8(sht->buf, sht->bxsize, COL8_848484, x0 - 3, y0 - 3, x0 - 3, y1 + 1);
    boxfill8(sht->buf, sht->bxsize, COL8_FFFFFF, x0 - 3, y1 + 2, x1 + 1, y1 + 2);
    boxfill8(sht->buf, sht->bxsize, COL8_FFFFFF, x1 + 2, y0 - 3, x1 + 2, y1 + 2);
    boxfill8(sht->buf, sht->bxsize, COL8_000000, x0 - 1, y0 - 2, x1 + 0, y0 - 2);
    boxfill8(sht->buf, sht->bxsize, COL8_000000, x0 - 2, y0 - 2, x0 - 2, y1 + 0);
    boxfill8(sht->buf, sht->bxsize, COL8_C6C6C6, x0 - 2, y1 + 1, x1 + 0, y1 + 1);
    boxfill8(sht->buf, sht->bxsize, COL8_C6C6C6, x1 + 1, y0 - 2, x1 + 1, y1 + 1);
    boxfill8(sht->buf, sht->bxsize, c,           x0 - 1, y0 - 1, x1 + 0, y1 + 0);
    return;
}

void task_b_main(void)
{
    FIFO fifo;
    TIMER *timer;
    int i, fifobuf[128];

    fifo_init(&fifo, 128, fifobuf);
    timer = timer_alloc();
    timer_init(timer, &fifo, 1);
    timer_settime(timer, 500);

    for (;;) {
        io_cli();
        if (fifo_status(&fifo) == 0) {
            io_stihlt();
        } else {
            i = fifo_get(&fifo);
            io_sti();
            if (i == 1) {
                taskswitch3();  // 返回任务A
            }
        }
    }
}
