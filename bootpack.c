#include "bootpack.h"
#include <stdio.h>

#define KEYCMD_LED  0xed

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

