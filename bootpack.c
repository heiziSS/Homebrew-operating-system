#include "bootpack.h"
#include <stdio.h>

#define KEYCMD_LED  0xed

// 文件信息在磁盘中保存的结构（32字节）
// 0x002600-0x0041ff 最多存放224个文件信息
typedef struct {
    unsigned char name[8];      // 文件名，不足8个字节用空格补足，超过8个字节暂不考虑
                                // 文件名第一个字节为0xe5，代表文件删除
                                // 文件名第一个字节为0x00，代表这一段不包含任何文件名信息
    unsigned char ext[3];       // 扩展名
    unsigned char type;         // 文件属性，通常为0x20或0x00
                                // 0x01：只读文件
                                // 0x02：隐藏文件
                                // 0x04：系统文件
                                // 0x08：非文件信息（比如磁盘名称）
                                // 0x10：目录
    char reserve[10];           // 保留位
    unsigned short time;        // 存放文件时间
    unsigned short date;        // 存放文件日期
    unsigned short clustno;     // 簇号
                                // 磁盘映像中的地址 = clustno * 512(即1个扇区容量) + 0x003e00
    unsigned int size;          // 文件大小
} FILEINFO;

void make_wtitle8(unsigned char *buf, int xsize, char *title, char act);
void make_window8(unsigned char *buf, int xsize, int ysize, char *title, char act);
void putfonts8_asc_sht(SHEET *sht, int x, int y, int color, int backColor, char *str, int strLen);
void make_textbox8(SHEET *sht, int x0, int y0, int sx, int sy, int c);
void console_task(SHEET *sheet, unsigned int memtotal);
int cons_newline(int cursor_y, SHEET *sheet);

void HariMain(void)
{
    BOOTINFO *binfo = (BOOTINFO *) ADR_BOOTINFO;
    FIFO fifo, keycmd;
    SHTCTL *shtctl;
    char s[40];
    int fifobuf[128], keycmd_buf[32];
	int mx, my, i, cursor_x, cursor_c;
    unsigned int memtotal;
    MOUSE_DEC mdec;
    MEMMAN *memman = (MEMMAN *) MEMMAN_ADDR;
    SHEET *sht_back, *sht_mouse, *sht_win, *sht_cons;
    unsigned char *buf_back, buf_mouse[256], *buf_win, *buf_cons;
    TASK *task_a, *task_cons;
    TIMER *timer;
	static char keytable0[0x80] = {
		 0,   0,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',   0,   0,
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']',   0,   0, 'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ';',   0,   0,   0,  0,  'Z', 'X', 'C', 'V',
		'B', 'N', 'M', ',', '.', '/',   0, '*',   0, ' ',   0,   0,   0,   0,   0,   0,
		  0,   0,   0,   0,   0,   0,   0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		  0,   0,   0,0x5c,   0,   0,   0,   0,   0,   0,   0,   0,   0,0x5c,   0,   0
	};
	static char keytable1[0x80] = {
		  0,   0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',   0,   0,
	    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',   0,   0, 'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"',   0,   0, '|', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', '<', '>', '?',   0,   0,   0, ' ',   0,   0,   0,   0,   0,   0,
		  0,   0,   0,   0,   0,   0,   0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
	};
    int key_to = 0, key_shift = 0, key_leds = (binfo->leds >> 4) & 7, keycmd_wait = -1;
    
    init_gdtidt();
    init_pic();
    io_sti(); //由于 IDT/PIC 初始化完成，因此取消了 CPU 中断禁令
    fifo_init(&fifo, 128, fifobuf, NULL);
    fifo_init(&keycmd, 32, keycmd_buf, NULL);
    init_pit();
    init_keyboard(&fifo, 256);
    enable_mouse(&fifo, 512, &mdec);
    io_out8(PIC0_IMR, 0xf8);    // 允许PIT(IRQ0), PIC1(IRQ2) 和键盘(IRQ1)（11111000）
    io_out8(PIC1_IMR, 0xef);    // 允许鼠标(IRQ12)（11101111）

    memtotal = memtest(0x00400000, 0xbfffffff); // 0x00400000以前的内存已经被使用了，参考8.5节内存分布图
    memman_init(memman);
    memman_free(memman, 0x00001000, 0x0009e000);    // 0x00001000 - 0x0009efff
    memman_free(memman, 0x00400000, memtotal - 0x00400000);

    init_palette();
    shtctl = shtctl_init(memman, binfo->vram ,binfo->scrnx, binfo->scrny);
    task_a = task_init(memman);
    fifo.task = task_a;
    task_run(task_a, 1, 2);

    /* sht_back */
    sht_back = sheet_alloc(shtctl);
    buf_back = (unsigned char *) memman_alloc_4k(memman, binfo->scrnx * binfo->scrny);
    sheet_setbuf(sht_back, buf_back, binfo->scrnx, binfo->scrny, -1);
    init_screen8(buf_back, binfo->scrnx, binfo->scrny);

    /* sht_cons */
    sht_cons = sheet_alloc(shtctl);
    buf_cons = (unsigned char *) memman_alloc_4k(memman, 256 * 165);
    sheet_setbuf(sht_cons, buf_cons, 256, 165, -1);
    make_window8(buf_cons, 256, 165, "console", 0);
    make_textbox8(sht_cons, 8, 28, 240, 128, COL8_000000);
    task_cons = task_alloc();
    task_cons->tss.esp = memman_alloc_4k(memman, 64 * 1024) + 64 * 1024 - 12;
    task_cons->tss.eip = (int) &console_task;
    task_cons->tss.es = 1 * 8;
    task_cons->tss.cs = 2 * 8;
    task_cons->tss.ss = 1 * 8;
    task_cons->tss.ds = 1 * 8;
    task_cons->tss.fs = 1 * 8;
    task_cons->tss.gs = 1 * 8;
    *((int *) (task_cons->tss.esp + 4)) = (int) sht_cons;
    *((int *) (task_cons->tss.esp + 8)) = memtotal;
    task_run(task_cons, 2, 2); // level=2, priority=2

    /* sht_win */
    sht_win = sheet_alloc(shtctl);
    buf_win = (unsigned char *) memman_alloc_4k(memman, 160 * 52);
    sheet_setbuf(sht_win, buf_win, 144, 52, -1);
    make_window8(buf_win, 144, 52, "task_a", 1);
    make_textbox8(sht_win, 8, 28, 128, 16, COL8_FFFFFF);
    cursor_x = 8;
    cursor_c = COL8_FFFFFF;
    timer = timer_alloc();
    timer_init(timer, &fifo, 1);
    timer_settime(timer, 50);

    /* sht_mouse */
    sht_mouse = sheet_alloc(shtctl);
    sheet_setbuf(sht_mouse, buf_mouse, 16, 16, 99);
    init_mouse_cursor8(buf_mouse, 99);
    mx = (binfo->scrnx - 16) >> 1;   // 坐标计算，使其位于屏幕中心
    my = (binfo->scrny - 28 - 16) >> 1;
    
    sheet_slide(sht_back, 0, 0);
    sheet_slide(sht_cons, 32, 4);
    sheet_slide(sht_win, 64, 56);
    sheet_slide(sht_mouse, mx, my);
    sheet_updown(sht_back, 0);
    sheet_updown(sht_cons, 1);
    sheet_updown(sht_win, 2);
    sheet_updown(sht_mouse, 3);

    // 为了避免和键盘当前状态冲突，在一开始先进行设置
    fifo_put(&keycmd, KEYCMD_LED);
    fifo_put(&keycmd, key_leds);
  
    for (;;) {
        if (fifo_status(&keycmd) > 0 && keycmd_wait < 0) {
            //如果存在向键盘控制器发送的数据，则发送它
            keycmd_wait = fifo_get(&keycmd);
            wait_KBC_sendready();
            io_out8(PORT_KEYDAT, keycmd_wait);
        }
        io_cli();
        if (fifo_status(&fifo) == 0) {
            task_sleep(task_a);
			io_sti();
        } else {
            i = fifo_get(&fifo);
            io_sti();
            if (256 <= i && i <= 511) { //键盘数据
                if (i < 0x80 + 256) { //将按键编码转换为字符编码
                    if (key_shift == 0) {
                        s[0] = keytable0[i - 256];
                    } else {
                        s[0] = keytable1[i - 256];
                    }
                } else {
                    s[0] = 0;
                }
                if ('A' <= s[0] && s[0] <= 'Z') { // 当输入字符为英文字符时
                    if ((key_leds >> 2 == 0 && key_shift == 0) || (key_leds >> 2 == 1 && key_shift == 1)) {
                        s[0] += 0x20;   // 大写字母转换为小写字母
                    }
                }
                if (s[0] != 0) { // 一般字符
                    if (key_to == 0) { // 发送给任务A
                        if (cursor_x < 128) {
                            s[1] = 0;
                            putfonts8_asc_sht(sht_win, cursor_x, 28, COL8_000000, COL8_FFFFFF, s, 1);
                            cursor_x += 8; // 显示完一个字符，光标后移一位
                        }
                    } else { // 发送给命令行窗口
                        fifo_put(&task_cons->fifo, s[0] + 256);
                    }
                }
                if (i == 0xe + 256) { //退格键
                    if (key_to == 0) { // 发送给任务A
                        if (cursor_x > 8) {
                            putfonts8_asc_sht(sht_win, cursor_x, 28, COL8_000000, COL8_FFFFFF, " ", 1);
                            cursor_x -= 8; // 用空格键把光标消去后，后移1次光标
                        }
                    } else {
                        fifo_put(&task_cons->fifo, 8 + 256);
                    }
                }
                if (i == 256 + 0x1c) { // 回车键
                    if (key_to != 0) {
                        fifo_put(&task_cons->fifo, 10 + 256);
                    }
                }
                if (i == 256 + 0x0f) { //tab键
                    if (key_to == 0) {
                        key_to = 1;
                        make_wtitle8(buf_win, sht_win->bxsize, "task_a", 0);
                        make_wtitle8(buf_cons, sht_cons->bxsize, "console", 1);
                        cursor_c = -1; //不显示光标
                        boxfill8(sht_win->buf, sht_win->bxsize, COL8_FFFFFF, cursor_x, 28, cursor_x + 7, 43);
                        fifo_put(&task_cons->fifo, 2);  //命令行窗口光标ON
                    } else {
                        key_to = 0;
                        make_wtitle8(buf_win, sht_win->bxsize, "task_a", 1);
                        make_wtitle8(buf_cons, sht_cons->bxsize, "console", 0);
                        cursor_c = COL8_000000; // 显示光标
                        fifo_put(&task_cons->fifo, 3); //命令行窗口光标OFF
                    }
                    sheet_refresh(sht_win, 0, 0, sht_win->bxsize, 21);
                    sheet_refresh(sht_cons, 0, 0, sht_cons->bxsize, 21);
                }
                if (i == 256 + 0x2a) { // 左shift ON
                    key_shift |= 1;
                }
                if (i == 256 + 0x36) { // 右shift ON
                    key_shift |= 2;
                }
                if (i == 256 + 0xaa) { // 左shift OFF
                    key_shift &= ~1;
                }
                if (i == 256 + 0xb6) { // 右shift OFF
                    key_shift &= ~2;
                }
                if (i == 256 + 0x3a) { // CapsLock
                    key_leds ^= 4;
                    fifo_put(&keycmd, KEYCMD_LED);
                    fifo_put(&keycmd, key_leds);
                }
                if (i == 256 + 0x45) { // NumLock
                    key_leds ^= 2;
                    fifo_put(&keycmd, KEYCMD_LED);
                    fifo_put(&keycmd, key_leds);
                }
                if (i == 256 + 0x46) {
                    key_leds ^= 1;
                    fifo_put(&keycmd, KEYCMD_LED);
                    fifo_put(&keycmd, key_leds);
                }
                if (i == 256 + 0xfa) { // 键盘成功接收到数据
                    keycmd_wait = -1;
                }
                if (i == 256 + 0xfe) { // 键盘没有成功接收到数据
                    wait_KBC_sendready();
                    io_out8(PORT_KEYDAT, keycmd_wait);
                }
                // 光标再现
                if (cursor_c >= 0) {
                    boxfill8(sht_win->buf, sht_win->bxsize, cursor_c, cursor_x, 28, cursor_x + 7, 43);
                }
                sheet_refresh(sht_win, cursor_x, 28, cursor_x + 8, 44);
            } else if (512 <= i && i <= 767) {
                if (mouse_decode(&mdec, i - 512) == 1) {
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
                    sheet_slide(sht_mouse, mx, my);
                    if ((mdec.btn & 0x1) != 0) {
                        sheet_slide(sht_win, mx, my);
                    }
                }
            } else if (i <= 1) {
                if (i == 1) { // 光标用时定时器
                    timer_init(timer, &fifo, 0); //设置成0
                    if (cursor_c >= 0) {
                        cursor_c = COL8_000000;
                    }
                } else { // 光标用时定时器
                    timer_init(timer, &fifo, 1); //设置成1
                    if (cursor_c >= 0) {
                        cursor_c = COL8_FFFFFF;
                    }
                }
                timer_settime(timer, 50);
                if (cursor_c >= 0) {
                    boxfill8(sht_win->buf, sht_win->bxsize, cursor_c, cursor_x, 28, cursor_x + 7, 43);
                    sheet_refresh(sht_win, cursor_x, 28, cursor_x + 8, 44);
                }
            }
        }
    }
}

void make_window8(unsigned char *buf, int xsize, int ysize, char *title, char act)
{
    boxfill8(buf, xsize, COL8_C6C6C6, 0, 0, xsize - 1, 0);
    boxfill8(buf, xsize, COL8_FFFFFF, 1, 1, xsize - 2, 1);
    boxfill8(buf, xsize, COL8_C6C6C6, 0, 0, 0, ysize - 1);
    boxfill8(buf, xsize, COL8_FFFFFF, 1, 1, 1, ysize - 2);
    boxfill8(buf, xsize, COL8_848484, xsize - 2, 1, xsize - 2, ysize - 2);
    boxfill8(buf, xsize, COL8_000000, xsize - 1, 0, xsize - 1, ysize - 1);
    boxfill8(buf, xsize, COL8_C6C6C6, 2, 2, xsize - 3, ysize - 3);
    boxfill8(buf, xsize, COL8_848484, 1, ysize - 2, xsize - 2, ysize - 2);
    boxfill8(buf, xsize, COL8_000000, 0, ysize - 1, xsize - 1, ysize - 1);
    make_wtitle8(buf, xsize, title, act);
    return;
}

void make_wtitle8(unsigned char *buf, int xsize, char *title, char act)
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
    char c, tc, tbc;
    if (act != 0) {
        tc = COL8_FFFFFF;
        tbc = COL8_000084;
    } else {
        tc = COL8_C6C6C6;
        tbc = COL8_848484;
    }
    boxfill8(buf, xsize, tbc, 3, 3, xsize - 4, 20);
    putfonts8_asc(buf, xsize, 24, 4, tc, title);
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

void console_task(SHEET *sheet, unsigned int memtotal)
{
    FILEINFO *finfo = (FILEINFO *) (ADR_DISKIMG + 0x002600);
    TIMER *timer;
    TASK *task = task_now();
    int i, fifobuf[128], cursor_x = 16, cursor_y = 28, cursor_c = -1;
    char s[30], cmdline[30], *p;
    MEMMAN *memman = (MEMMAN *) MEMMAN_ADDR;
    int x, y;

    fifo_init(&task->fifo, 128, fifobuf, task);
    timer = timer_alloc();
    timer_init(timer, &task->fifo, 1);
    timer_settime(timer, 50);

    // 显示提示符
    putfonts8_asc_sht(sheet, 8, 28, COL8_FFFFFF, COL8_000000, ">", 1);

    for (;;) {
        io_cli();
        if (fifo_status(&task->fifo) == 0) {
            task_sleep(task);
            io_sti();
        } else {
            i = fifo_get(&task->fifo);
            io_sti();
            if (i <= 1) { // 光标用定时器
                if (i != 0) {
                    timer_init(timer, &task->fifo, 0); //下次置0
                    if (cursor_c >= 0) {
                        cursor_c = COL8_FFFFFF;
                    }
                } else {
                    timer_init(timer, &task->fifo, 1); //下次置1
                    if (cursor_c >= 0) {
                        cursor_c = COL8_000000;
                    }
                }
                timer_settime(timer, 50);
            }
            if (i == 2) { // 光标ON
                cursor_c = COL8_FFFFFF;
            }
            if (i == 3) {
                boxfill8(sheet->buf, sheet->bxsize, COL8_000000, cursor_x, cursor_y, cursor_x + 7, cursor_y + 15);
                cursor_c = -1;
            }
            if (256 <= i && i <= 511) { // 键盘数据（通过任务A）
                if (i == 8 + 256) { // 退格键
                    if (cursor_x > 16) {
                        putfonts8_asc_sht(sheet, cursor_x, cursor_y, COL8_FFFFFF, COL8_000000, " ", 1);
                        cursor_x -= 8;
                    }
                } else if (i == 10 + 256) { // 回车键
                    //用空格将光标擦除
                    putfonts8_asc_sht(sheet, cursor_x, cursor_y, COL8_FFFFFF, COL8_000000, " ", 1);
                    cmdline[cursor_x / 8 - 2] = 0;
                    cursor_y = cons_newline(cursor_y, sheet);
                    // 执行命令
                    if (strcmp(cmdline, "mem") == 0) {
                        // mem命令
                        sprintf(s, "total %dMB", memtotal / (1024 * 1024));
                        putfonts8_asc_sht(sheet, 8, cursor_y, COL8_FFFFFF, COL8_000000, s, 30);
                        cursor_y = cons_newline(cursor_y, sheet);
                        sprintf(s, "free %dKB", memman_total(memman) / 1024);
                        putfonts8_asc_sht(sheet, 8, cursor_y, COL8_FFFFFF, COL8_000000, s, 30);
                        cursor_y = cons_newline(cursor_y, sheet);
                        cursor_y = cons_newline(cursor_y, sheet);
                    } else if (strcmp(cmdline, "cls") == 0) {
                        // cls命令
                        for (y = 28; y < 28 + 128; y++) {
                            for (x = 8; x < 8 + 240; x++) {
                                sheet->buf[x + y * sheet->bxsize] = COL8_000000;
                            }
                        }
                        sheet_refresh(sheet, 8, 28, 8 + 240, 28 + 128);
                        cursor_y = 28;
                    } else if (strcmp(cmdline, "dir") == 0) {
                        // dir命令
                        for (x = 0; x < 224; x++) {
                            if (finfo[x].name[0] == 0x00) { // 后面没有再保存文件信息
                                break;
                            } else if (finfo[x].name[0] != 0xe5) {
                                if (finfo[x].name[0] != 0xe5) {
                                    if ((finfo[x].type & 0x18) == 0) {
                                        sprintf(s, "filename.ext   %7d", finfo[x].size);
                                        for (y = 0; y < 8; y++) {
                                            s[y] = finfo[x].name[y];
                                        }
                                        s[9] = finfo[x].ext[0];
                                        s[10] = finfo[x].ext[1];
                                        s[11] = finfo[x].ext[2];
                                        putfonts8_asc_sht(sheet, 8, cursor_y, COL8_FFFFFF, COL8_000000, s, 30);
                                    }
                                }
                            }
                            cursor_y = cons_newline(cursor_y, sheet);
                        }
                    } else if (strncmp(cmdline, "type ", 5) == 0) {
                        // type命令
                        // 文件名
                        for (y = 0; y < 11; y++) {
                            s[y] = ' ';
                        }
                        
                        for (x = 5, y = 0; y < 11 && cmdline[x] != 0; x++) {
                            if (cmdline[x] == '.' && y <= 8) {
                                y = 8;
                            } else {
                                s[y] = cmdline[x];
                                if ('a' <= s[y] && s[y] <= 'z') {
                                    s[y] -= 0x20; // 小写字母转大写字母
                                }
                                y++;
                            }
                        }
                        // 寻找文件
                        for (x = 0; x < 224;) {
                            if (finfo[x].name[0] == 0x00) {
                                break;
                            }
                            if ((finfo[x].type & 0x18) == 0) {
                                for (y = 0; y < 11; y++) {
                                    if (finfo[x].name[y] != s[y]) {
                                        goto type_next_file;
                                    }
                                }
                                break; //找到文件
                            }
            type_next_file:
                        x++;
                        }
                        // 文件找到，输出文件内容
                        if (x < 224 && finfo[x].name[0] != 0x00) {
                            p = (char *) (finfo[x].clustno * 512 + 0x003e00 + ADR_DISKIMG);
                            y = finfo[x].size;
                            cursor_x = 8;
                            for (x = 0; x < y; x++) {
                                s[0] = p[x]; s[1] = 0; //逐字输出
                                if (s[0] == 0x09) { // 制表符
                                    while(1) {
                                        putfonts8_asc_sht(sheet, cursor_x, cursor_y, COL8_FFFFFF, COL8_000000, " ", 1);
                                        cursor_x += 8;
                                        if (cursor_x >= 8 + 240) {
                                            cursor_x = 8;
                                            cursor_y = cons_newline(cursor_y, sheet);
                                        }
                                        if ( (cursor_x - 8) & 0x1f == 0) {
                                            break; // 被32整除则break
                                        }
                                    }
                                } else if (s[0] == 0x0a) { // 换行
                                    cursor_x = 8;
                                    cursor_y = cons_newline(cursor_y, sheet);
                                } else if (s[0] == 0xd) { // 回车
                                    // 暂时不做任何动作
                                } else { // 一般字符
                                    putfonts8_asc_sht(sheet, cursor_x, cursor_y, COL8_FFFFFF, COL8_000000, s, 1);
                                    cursor_x += 8;
                                    if (cursor_x >= 8 + 240) { // 到达最右端后换行
                                        cursor_x = 8;
                                        cursor_y = cons_newline(cursor_y, sheet);
                                    }
                                }
                            }
                        } else { // 没有找到文件的情况
                            putfonts8_asc_sht(sheet, 8, cursor_y, COL8_FFFFFF, COL8_000000, "File not found.", 15);
                            cursor_y = cons_newline(cursor_y, sheet);
                        }
                        cursor_y = cons_newline(cursor_y, sheet);
                    } else if (cmdline[0] != 0) {
                        // 未知命令
                        putfonts8_asc_sht(sheet, 8, cursor_y, COL8_FFFFFF, COL8_000000, "Bad command.", 12);
                        cursor_y = cons_newline(cursor_y, sheet);
                        cursor_y = cons_newline(cursor_y, sheet);
                    }
                    // 显示提示符
                    putfonts8_asc_sht(sheet, 8, cursor_y, COL8_FFFFFF, COL8_000000, ">", 1);
                    cursor_x = 16;
                } else { // 一般字符
                    if (cursor_x < 240) {
                        s[0] = i - 256;
                        s[1] = 0;
                        cmdline[cursor_x / 8 - 2] = i - 256;
                        putfonts8_asc_sht(sheet, cursor_x, cursor_y, COL8_FFFFFF, COL8_000000, s, 1);
                        cursor_x += 8;
                    }
                }
            }
            // 重新显示光标
            if (cursor_c >= 0) {
                boxfill8(sheet->buf, sheet->bxsize, cursor_c, cursor_x, cursor_y, cursor_x + 7, cursor_y + 15);
            }
            sheet_refresh(sheet, cursor_x, cursor_y, cursor_x + 8, cursor_y + 16);
        }
    }
}

int cons_newline(int cursor_y, SHEET *sheet)
{
    int x, y;
    if (cursor_y < 28 + 112) {
        cursor_y += 16;
    } else { // 滚动
        for (y = 28; y < 28 + 112; y++) {
            for (x = 8; x < 8 + 240; x++) {
                sheet->buf[x + y * sheet->bxsize] = sheet->buf[x + (y + 16) * sheet->bxsize];
            }
        }
        for (y = 28 + 112; y < 28 + 128; y++) {
            for (x = 8; x < 8 + 240; x++) {
                sheet->buf[x + y * sheet->bxsize] = COL8_000000;
            }
        }
        sheet_refresh(sheet, 8, 28, 8 + 240, 28 + 128);
    }
    return cursor_y;
}
