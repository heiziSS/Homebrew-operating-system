#include "bootpack.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    SHEET *sht;     // 当前窗口所处的图层
    int cur_x;      // 光标x坐标
    int cur_y;      // 光标y坐标
    int cur_c;      // 光标颜色
} CONSOLE;

typedef void (*CmdFunction) (CONSOLE *, char *);

typedef struct {
    char cmdkey[30];
    CmdFunction func;
} CMD_LIST;

void cmd_mem(CONSOLE *cons, char *cmd);
void cmd_cls(CONSOLE *cons, char *cmd);
void cmd_dir(CONSOLE *cons, char *cmd);
void cmd_type(CONSOLE *cons, char *cmd);
void cmd_hlt(CONSOLE *cons, char *cmd);

void console_task(SHEET *sheet, unsigned int memtotal)
{
    TIMER *timer;
    TASK *task = task_now();
    MEMMAN *memman = (MEMMAN *) MEMMAN_ADDR;
    int i, fifobuf[128];
    char cmdline[30], s[30];
    CONSOLE cons;
    cons.sht = sheet;
    cons.cur_x = 8;
    cons.cur_y = 28;
    cons.cur_c = -1;

    fifo_init(&task->fifo, 128, fifobuf, task);
    timer = timer_alloc();
    timer_init(timer, &task->fifo, 1);
    timer_settime(timer, 50);

    // 显示提示符
    putfonts8_asc_sht(sheet, 8, 28, COL8_FFFFFF, COL8_000000, ">", 1);
    cons.cur_x = 16;

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
                    if (cons.cur_c >= 0) {
                        cons.cur_c = COL8_FFFFFF;
                    }
                } else {
                    timer_init(timer, &task->fifo, 1); //下次置1
                    if (cons.cur_c >= 0) {
                        cons.cur_c = COL8_000000;
                    }
                }
                timer_settime(timer, 50);
            }
            if (i == 2) { // 光标ON
                cons.cur_c = COL8_FFFFFF;
            }
            if (i == 3) {
                boxfill8(sheet->buf, sheet->bxsize, COL8_000000, cons.cur_x, cons.cur_y, cons.cur_x + 7, cons.cur_y + 15);
                cons.cur_c = -1;
            }
            if (256 <= i && i <= 511) { // 键盘数据（通过任务A）
                if (i == 8 + 256) { // 退格键
                    if (cons.cur_x > 16) {
                        putfonts8_asc_sht(sheet, cons.cur_x, cons.cur_y, COL8_FFFFFF, COL8_000000, " ", 1);
                        cons.cur_x -= 8;
                    }
                } else if (i == 10 + 256) { // 回车键
                    //用空格将光标擦除
                    putfonts8_asc_sht(sheet, cons.cur_x, cons.cur_y, COL8_FFFFFF, COL8_000000, " ", 1);
                    cmdline[cons.cur_x / 8 - 2] = 0;
                    cursor_move_down(&cons);
                    // 执行命令
                    cons_cmdrun(&cons, cmdline);
                    // 显示提示符
                    putfonts8_asc_sht(cons.sht, 8, cons.cur_y, COL8_FFFFFF, COL8_000000, ">", 1);
                    cons.cur_x = 16;
                } else { // 一般字符
                    if (cons.cur_x < 240) {
                        s[0] = i - 256;
                        s[1] = 0;
                        cmdline[cons.cur_x / 8 - 2] = i - 256;
                        putfonts8_asc_sht(sheet, cons.cur_x, cons.cur_y, COL8_FFFFFF, COL8_000000, s, 1);
                        cons.cur_x += 8;
                    }
                }
            }
            // 重新显示光标
            if (cons.cur_c >= 0) {
                boxfill8(sheet->buf, sheet->bxsize, cons.cur_c, cons.cur_x, cons.cur_y, cons.cur_x + 7, cons.cur_y + 15);
            }
            sheet_refresh(sheet, cons.cur_x, cons.cur_y, cons.cur_x + 8, cons.cur_y + 16);
        }
    }
}

/* 光标下移 */
void cursor_move_down(CONSOLE *cons)
{
    int x, y;
    SHEET *sheet = cons->sht;
    if (cons->cur_y < 28 + 112) {
        cons->cur_y += 16;  // 到下一行
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
    cons->cur_x = 8;
    return;
}

/* 光标右移 */
void cursor_move_right(CONSOLE *cons)
{
    cons->cur_x += 8;
    if (cons->cur_x >= 8 + 240) {   // 到达窗口最右端
        cursor_move_down(cons);
    }
    return;
}

/* 窗口输出单个字符 */
void cons_putchar(CONSOLE *cons, char chr, char move)
{
    char s[2] = {chr, 0};
    switch (s[0])
    {
    case 0x09:  // 制表符
        for (;;) {
            putfonts8_asc_sht(cons->sht, cons->cur_x, cons->cur_y, COL8_FFFFFF, COL8_000000, " ", 1);
            cursor_move_right(cons);
            if ((cons->cur_x == 8) || (((cons->cur_x - 8) & 0x1f) == 0)) {
                break;  // 被32整除或者已经到达下一行，则break
            }
        }
        break;

    case 0x0a:  // 换行
        cursor_move_down(cons);
        break;

    case 0x0d:  // 回车
        // 暂不做任何操作
        break;

    default:    // 一般字符
        putfonts8_asc_sht(cons->sht, cons->cur_x, cons->cur_y, COL8_FFFFFF, COL8_000000, s, 1);
        if (move != 0) {    // move为0时光标不后移
            cursor_move_right(cons);
        }
        break;
    }
    return;
}

void cons_printf(CONSOLE *cons, char *s)
{
    char c;
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        c = s[i];
        cons_putchar(cons, c, 1);
    }
    cursor_move_down(cons);
    return;
}

unsigned char cmdkey_match(char *cmd, char *cmdkey)
{
    int i;
    for (i = 0; cmdkey[i] != '\0'; i++) {
        if (cmdkey[i] != cmd[i]) {
            return FALSE;
        }
    }
    if (cmd[i] == '\0' || cmd[i] == ' ') {
        return TRUE;
    }
    return FALSE;
}

void cons_cmdrun(CONSOLE *cons, char *cmd)
{
    static CMD_LIST cmdlist[] ={
        {"mem", cmd_mem},
        {"cls", cmd_cls},
        {"dir", cmd_dir},
        {"type", cmd_type},
        {"hlt", cmd_hlt},
    };
    static int cmdlistLen = sizeof(cmdlist) / sizeof(CMD_LIST);
    int i;
    for (i = 0; i < cmdlistLen; i++) {
        if (cmdkey_match(cmd, cmdlist[i].cmdkey) == TRUE) {
            break;
        }
    }

    if (i >= cmdlistLen) { // 未找到对应命令
        cons_printf(cons, "Unknown command.");
        return;
    }

    cmdlist[i].func(cons, cmd);

    return;
}

void cmd_mem(CONSOLE *cons, char *cmd)
{
    char s[30];
    MEMMAN *memman = (MEMMAN *) MEMMAN_ADDR;
    unsigned int memtotal = memtest(0x00400000, 0xbfffffff); // 0x00400000以前的内存已经被使用了，参考8.5节内存分布图
    sprintf(s, "total %dMB", memtotal / (1024 * 1024));
    putfonts8_asc_sht(cons->sht, 8, cons->cur_y, COL8_FFFFFF, COL8_000000, s, 30);
    cursor_move_down(cons);
    sprintf(s, "free %dKB", memman_total(memman) / 1024);
    putfonts8_asc_sht(cons->sht, 8, cons->cur_y, COL8_FFFFFF, COL8_000000, s, 30);
    cursor_move_down(cons);
    cursor_move_down(cons);
    return;
}

void cmd_cls(CONSOLE *cons, char *cmd)
{
    int x, y;
    SHEET *sheet = cons->sht;
    for (y = 28; y < 28 + 128; y++) {
        for (x = 8; x < 8 + 240; x++) {
            sheet->buf[x + y * sheet->bxsize] = COL8_000000;
        }
    }
    sheet_refresh(sheet, 8, 28, 8 + 240, 28 + 128);
    cons->cur_y = 28;
    return;
}

void cmd_dir(CONSOLE *cons, char *cmd)
{
    FILEINFO *finfo = (FILEINFO *) (ADR_DISKIMG + 0x002600);
    int i, j;
    char s[30];
    for (i = 0; i < 224; i++) {
        if (finfo[i].name[0] == 0x00) { // 后面没有再保存文件信息
            break;
        } else if (finfo[i].name[0] != 0xe5) {
            if ((finfo[i].type & 0x18) == 0) {
                sprintf(s, "filename.ext   %7d", finfo[i].size);
                for (j = 0; j < 8; j++) {
                    s[j] = finfo[i].name[j];
                }
                s[9]  = finfo[i].ext[0];
                s[10] = finfo[i].ext[1];
                s[11] = finfo[i].ext[2];
                putfonts8_asc_sht(cons->sht, 8, cons->cur_y, COL8_FFFFFF, COL8_000000, s, 30);
                cursor_move_down(cons);
            }
        }
    }
    cursor_move_down(cons);
}

void cmd_type(CONSOLE *cons, char *cmd)
{
    FILEINFO *finfo = file_search(cmd + 5);
    if (finfo == NULL) {
        putfonts8_asc_sht(cons->sht, 8, cons->cur_y, COL8_FFFFFF, COL8_000000, "File not found.", 15);
        cursor_move_down(cons);
        return; //未找到文件
    }

    MEMMAN *memman = (MEMMAN *) MEMMAN_ADDR;
    unsigned char *fat_img = (unsigned char *)(ADR_DISKIMG + 0x000200);
    char *img = (char *)(ADR_DISKIMG + 0x003e00);
    char *filebuf = (char *)memman_alloc_4k(memman, finfo->size);
    int i;

    // 从镜像文件中读取文件内容
    file_loadfile(finfo, fat_img, img, filebuf);

    // 输出文件内容
    for (i = 0; i < finfo->size; i++) {
        cons_putchar(cons, filebuf[i], 1);
    }
    cursor_move_down(cons);

    memman_free_4k(memman, (unsigned int)filebuf, finfo->size);
    return;
}

void cmd_hlt(CONSOLE *cons, char *cmd)
{
    FILEINFO *finfo = file_search("HLT.HRB");
    if (finfo == NULL) {
        putfonts8_asc_sht(cons->sht, 8, cons->cur_y, COL8_FFFFFF, COL8_000000, "File not found.", 15);
        cursor_move_down(cons);
        return; //未找到文件
    }

    MEMMAN *memman = (MEMMAN *) MEMMAN_ADDR;
    SEGMENT_DESCRIPTOR *gdt = (SEGMENT_DESCRIPTOR *)ADR_GDT;
    char *filebuf = (char *)memman_alloc_4k(memman, finfo->size);
    set_segmdesc(gdt + 1003, finfo->size - 1, (int)filebuf, AR_CODE32_ER);
    farjmp(0, 1003 * 8);
    memman_free_4k(memman, (unsigned int)filebuf, finfo->size);
    cursor_move_down(cons);
    return;
}
