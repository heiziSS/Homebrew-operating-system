void io_hlt(void);
void io_cli(void);
void io_out8(int port, int data);
int io_load_eflags(void);
void io_store_eflags(int eflags);

void init_palette(void);
void set_palette(int start, int end, unsigned char *rgb);
void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1);
void init_screen(char *vram, int x, int y);
void putfont8(char *vram, int xsize, int x, int y, char c, char *font);
void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s);
void init_mouse_cursor8(char *mouse, char bc);
void putblock8_8(char *vram, int vxsize, int pxsize,
    int pysize, int px0, int py0, char *buf, int bxsize);

#define COL8_000000     0
#define COL8_FF0000     1
#define COL8_00FF00     2
#define COL8_FFFF00     3
#define COL8_0000FF     4
#define COL8_FF00FF     5
#define COL8_00FFFF     6
#define COL8_FFFFFF     7
#define COL8_C6C6C6     8
#define COL8_840000     9
#define COL8_008400     10
#define COL8_848400     11
#define COL8_000084     12
#define COL8_840084     13
#define COL8_008484     14
#define COL8_848484     15

typedef struct {
    char cyls;      // 设定启动区
    char leds;
    char vmode;     // 关于颜色数目的信息, 颜色的位数
    char reserve;   // 保证结构体四字节对齐，无意义
    short scrnx;    // 分辨率的X（screen x）
    short scrny;    // 分辨率的Y（screen y）
    char *vram;     // 图像缓冲区的开始地址，颜色写入该内存即可显示
} BOOTINFO;

typedef struct {
    short limit_low;
    short base_low;
    char base_mid;
    char access_right;
    char limit_high;
    char base_high;
} SEGMENT_DESCRIPTOR;

typedef struct {
    short offset_low;
    short selector;
    char dw_count;
    char access_right;
    short offset_high;
} GATE_DESCRIPTOR;

void init_gdtidt(void);
void set_segmdesc(SEGMENT_DESCRIPTOR *sd, unsigned limit, int base, int ar);
void set_gatedesc(GATE_DESCRIPTOR *ad, int offset, int selector, int ar);
void load_gdtr(int limit, int addr);
void load_idtr(int limit, int addr);

void HariMain(void)
{
    BOOTINFO *binfo = (BOOTINFO *) 0x0ff0;
    char s[40], mcursor[256];
    int mx, my;
    
    init_palette();
    init_screen(binfo->vram, binfo->scrnx, binfo->scrny);
    mx = (binfo->scrnx - 16) >> 1;   // 坐标计算，使其位于屏幕中心
    my = (binfo->scrny - 28 - 16) >> 1;
    init_mouse_cursor8(mcursor, COL8_008484);
    putblock8_8(binfo->vram, binfo->scrnx, 16, 16, mx, my, mcursor, 16);
    sprintf(s, "(%d, %d)", mx, my);
    putfonts8_asc(binfo->vram, binfo->scrnx, 0, 0, COL8_FFFFFF, s);

    for (;;) {
        io_hlt();
    }
}

void init_palette(void)
{
    static unsigned char table_rgb[16 * 3] = {
        0x00, 0x00, 0x00,   // 0:黑
        0xff, 0x00, 0x00,   // 1:亮红
        0x00, 0xff, 0x00,   // 2:亮绿
        0xff, 0xff, 0x00,   // 3:亮黄
        0x00, 0x00, 0xff,   // 4:亮蓝
        0xff, 0x00, 0xff,   // 5:亮紫
        0x00, 0xff, 0xff,   // 6:浅亮蓝
        0xff, 0xff, 0xff,   // 7:白
        0xc6, 0xc6, 0xc6,   // 8:亮灰
        0x84, 0x00, 0x00,   // 9:暗红
        0x00, 0x84, 0x00,   // 10:暗绿
        0x84, 0x84, 0x00,   // 11:暗黄
        0x00, 0x00, 0x84,   // 12:暗青
        0x84, 0x00, 0x84,   // 13:暗紫
        0x00, 0x84, 0x84,   // 14:浅暗蓝
        0x84, 0x84, 0x84    // 15:暗灰
    };

    set_palette(0, 15, table_rgb);
    return;
}

void set_palette(int start, int end, unsigned char *rgb)
{
    int i, eflags;
    eflags = io_load_eflags();      // 记录中断许可标志的值
    io_cli();                       // 将中断许可标志置为0，禁止中断
    io_out8(0x03c8, start);
    for (i = start; i <= end; i++) {
        io_out8(0x03c9, rgb[0] / 4);
        io_out8(0x03c9, rgb[1] / 4);
        io_out8(0x03c9, rgb[2] / 4);
        rgb += 3;
    }
    io_store_eflags(eflags);        // 复原中断许可标志
    return;
}

void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            vram[y * xsize + x] = c;
        }
    }
    return;
}

void init_screen(char *vram, int x, int y)
{
    boxfill8(vram, x, COL8_008484, 0, 0,      x - 1, y - 29);
    boxfill8(vram, x, COL8_C6C6C6, 0, y - 28, x - 1, y - 28);
    boxfill8(vram, x, COL8_FFFFFF, 0, y - 27, x - 1, y - 27);
    boxfill8(vram, x, COL8_C6C6C6, 0, y - 26, x - 1, y - 1);

    boxfill8(vram, x, COL8_FFFFFF, 3,  y - 24, 59, y - 24);
    boxfill8(vram, x, COL8_FFFFFF, 2,  y - 24, 2,  y - 4);
    boxfill8(vram, x, COL8_848484, 3,  y - 4,  59, y - 4);
    boxfill8(vram, x, COL8_848484, 59, y - 23, 59, y - 5);
    boxfill8(vram, x, COL8_000000, 2,  y - 3,  59, y - 3);
    boxfill8(vram, x, COL8_000000, 60, y - 24, 60, y - 3);

    boxfill8(vram, x, COL8_848484, x - 47, y - 24, x - 4,  y - 24);
    boxfill8(vram, x, COL8_848484, x - 47, y - 23, x - 47, y - 4);
    boxfill8(vram, x, COL8_FFFFFF, x - 47, y - 3,  x - 4,  y - 3);
    boxfill8(vram, x, COL8_FFFFFF, x - 3,  y - 24, x - 3,  y - 3);
    return;
}

void putfont8(char *vram, int xsize, int x, int y, char c, char *font)
{
    int i;
    char *p, d;
    for (i = 0; i < 16; i++) {
        p = vram + (y + i) * xsize + x;
        d = font[i];
        if ((d & 0x80) != 0)    p[0] = c;
        if ((d & 0x40) != 0)    p[1] = c;
        if ((d & 0x20) != 0)    p[2] = c;
        if ((d & 0x10) != 0)    p[3] = c;
        if ((d & 0x08) != 0)    p[4] = c;
        if ((d & 0x04) != 0)    p[5] = c;
        if ((d & 0x02) != 0)    p[6] = c;
        if ((d & 0x01) != 0)    p[7] = c;
    }
    return;
}

void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s)
{
    extern char hankaku[4096];
    for (; *s != 0x00; s++) {
        putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
        x += 8;
    }
    return;
}

/* 准备鼠标指针（16x16） */
void init_mouse_cursor8(char *mouse, char bc)
{
	static char cursor[16][16] = {
		"**************..",
		"*OOOOOOOOOOO*...",
		"*OOOOOOOOOO*....",
		"*OOOOOOOOO*.....",
		"*OOOOOOOO*......",
		"*OOOOOOO*.......",
		"*OOOOOOO*.......",
		"*OOOOOOOO*......",
		"*OOOO**OOO*.....",
		"*OOO*..*OOO*....",
		"*OO*....*OOO*...",
		"*O*......*OOO*..",
		"**........*OOO*.",
		"*..........*OOO*",
		"............*OO*",
		".............***"
	};
    int x, y;

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            if (cursor[y][x] == '*') {
                mouse[y * 16 + x] = COL8_000000;
            } else if (cursor[y][x] == 'O') {
                mouse[y * 16 + x] = COL8_FFFFFF;
            } else {
                mouse[y * 16 + x] = bc;
            }
        }
    }
    return;
}

void putblock8_8(char *vram, int vxsize, int pxsize,
    int pysize, int px0, int py0, char *buf, int bxsize)
{
    int x, y;
    for (y = 0; y < pysize; y++) {
        for (x = 0; x < pxsize; x++) {
            vram[(py0 + y) * vxsize + (px0 + x)] = buf[y * bxsize + x];
        }
    }
    return;
}

/*
    初始化GDT和IDT
    GDT: global segment descriptor table 全局段号记录表 （8192x8=65536 BYTE （64KB））
        段寄存器是16位的，但是由于设计上的原因，段寄存器的低3位不能使用，故段号为0~8191
        用于存储段号和段信息，保存于内存中，内存起始地址和有效设定个数存放于特殊寄存器GDTR中
    IDT: interrupt descriptor table 中断记录表
        记录了0~255的中断号码与调用函数的对应关系
*/
void init_gdtidt(void)
{
    SEGMENT_DESCRIPTOR *gdt = (SEGMENT_DESCRIPTOR *) 0x00270000;
    GATE_DESCRIPTOR    *idt = (GATE_DESCRIPTOR    *) 0x0026f800;
    int i;

    // GDT初始化
    for (i = 0; i < 8192; i++) {
        set_segmdesc(gdt + i, 0, 0, 0);
    }
    // 设定段号为1的段
    // 上限值为0xffffffff，即4GB，CPU能管理的全部内存
    // 地址是0x00000000, 属性值为0x4092
    set_segmdesc(gdt + 1, 0xffffffff, 0x00000000, 0x4092);
    // 设定段号为2的段
    // 上限值为0x0007ffff，即5125B，为bootpack.hrb准备
    // 地址是0x00280000, 属性值为0x409a
    set_segmdesc(gdt + 2, 0x0007ffff, 0x00280000, 0x409a);
    load_gdtr(0xffff, 0x00270000); // 给GDTR赋值

    // IDT初始化
    for (i = 0; i < 256; i++) {
        set_gatedesc(idt + i, 0, 0, 0);
    }
    load_idtr(0x7ff, 0x0026f800);

    return;
}

/*
    设置段属性：
        limit: 段的大小；
        base : 段的起始地址；
        ar   : 段的管理权限（禁止写入、禁止执行、系统专用等）；
        CPU用8个字节的数据表示这些信息
*/
void set_segmdesc(SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar)
{
    if (limit > 0xfffff) {
        ar |= 0x8000;   // G_bit = 1
        limit /= 0x1000;
    }
    sd->limit_low    = limit & 0xffff;
    sd->base_low     = base & 0xffff;
    sd->base_mid     = (base >> 16) & 0xff;
    sd->access_right = ar & 0xff;
    sd->limit_high   = ((limit >> 16) & 0x0f) | ((ar >> 8) & 0xf0);
    sd->base_high    = (base >> 24) & 0xff;
}

void set_gatedesc(GATE_DESCRIPTOR *gd, int offset, int selector, int ar)
{
    gd->offset_low   = offset & 0xffff;
    gd->selector     = selector;
    gd->dw_count     = (ar >> 8) & 0xff;
    gd->access_right = ar & 0xff;
    gd->offset_high  = (offset >> 16) & 0xffff;
    return;
}