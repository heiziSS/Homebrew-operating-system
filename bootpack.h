/* asmhead.nas */
#define ADR_BOOTINFO    0x00000ff0

typedef struct {    /* 0x0ff0-0x0fff */
    char cyls;      // 启动区读硬盘读到何处为止
    char leds;      // 启动时键盘LED的状态
    char vmode;     // 显卡模式为多少位彩色
    char reserve;   // 保证结构体四字节对齐，无意义
    short scrnx;    // 分辨率的X（screen x）
    short scrny;    // 分辨率的Y（screen y）
    char *vram;     // 图像缓冲区的开始地址，颜色写入该内存即可显示
} BOOTINFO;

/* naskfunc.nas */
void io_hlt(void);
void io_cli(void);
void io_sti(void);
void io_stihlt(void);
int io_in8(int port);
void io_out8(int port, int data);
int io_load_eflags(void);
void io_store_eflags(int eflags);
void load_gdtr(int limit, int addr);
void load_idtr(int limit, int addr);
int load_cr0(void);
void store_cr0(int cr0);
void asm_inthandler21(void);
void asm_inthandler27(void);
void asm_inthandler2c(void);
unsigned int memtest_sub(unsigned int start, unsigned int end);

/* fifo.c */
typedef struct {
    unsigned char *buf;
    int w, r, size, free, flags;
} FIFO8;

void fifo8_init(FIFO8 *fifo, int size, unsigned char *buf);
int fifo8_put(FIFO8 *fifo, unsigned char data);
int fifo8_get(FIFO8 *fifo);
int fifo8_status(FIFO8 *fifo);

/* graphic.c */
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

void init_palette(void);
void set_palette(int start, int end, unsigned char *rgb);
void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1);
void init_screen8(char *vram, int x, int y);
void putfont8(char *vram, int xsize, int x, int y, char c, char *font);
void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s);
void init_mouse_cursor8(char *mouse, char bc);
void putblock8_8(char *vram, int vxsize, int pxsize,
    int pysize, int px0, int py0, char *buf, int bxsize);

/* dsctbl.c */
#define ADR_IDT         0x0026f800
#define LIMIT_IDT       0x000007ff
#define ADR_GDT         0x00270000
#define LIMIT_GDT       0x0000ffff
#define ADT_BOTPAK      0x00280000
#define LIMIT_BOTPAK    0x0007ffff
#define AR_DATA32_RW    0x4092
#define AR_CODE32_ER    0x409a
#define AR_INTGATE32    0x008e

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

/* int.c */
#define PIC0_ICW1       0x0020
#define PIC0_OCW2       0x0020
#define PIC0_IMR        0x0021
#define PIC0_ICW2       0x0021
#define PIC0_ICW3       0x0021
#define PIC0_ICW4       0x0021
#define PIC1_ICW1       0x00a0
#define PIC1_OCW2       0x00a0
#define PIC1_IMR        0x00a1
#define PIC1_ICW2       0x00a1
#define PIC1_ICW3       0x00a1
#define PIC1_ICW4       0x00a1

void init_pic(void);
void inthandler21(int *esp);
void inthandler27(int *esp);
void inthandler2c(int *esp);

/* keyboard.c */
#define PORT_KEYDAT     0x0060
#define PORT_KEYCMD     0x0064

extern FIFO8 keyfifo;

void inthandler21(int *esp);
void wait_KBC_sendready(void);
void init_keyboard(void);

/* mouse.c */
typedef struct {
    unsigned char buf[3], phase;
    int x, y, btn;
} MOUSE_DEC;

extern FIFO8 mousefifo;

void inthandler2c(int *esp);
void enable_mouse(MOUSE_DEC *mdec);
int mouse_decode(MOUSE_DEC *mdec, unsigned char dat);

/* memory.c */
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
unsigned int memman_alloc_4k(MEMMAN *man, unsigned int size);
int memman_free_4k(MEMMAN *man, unsigned int addr, unsigned int size);

/* sheet.c */
#define MAX_SHEETS          256

typedef struct {
    unsigned char *buf;     // 记录图层上所描画内容
    int bxsize;             // 图层的整体大小
    int bysize;             // 图层的整体大小
    int vx0;                // 图层在画面上位置的左上角坐标，v是VRAM的略语
    int vy0;                // 图层在画面上位置的左上角坐标
    int col_inv;            // 透明色号
    int height;             // 图层高度
    int flags;              // 存放有关图层的各种设定信息
} SHEET;

typedef struct {
    unsigned char *vram;         // VRAM的地址
    int xsize;                   // 整个显示画面的大小
    int ysize;                   // 整个显示画面的大小
    int top;                     // 最上面图层的高度
    SHEET *DisplayedSheets[MAX_SHEETS];  // 存放所有图层的地址
    SHEET sheets[MAX_SHEETS];    // 存放所有图层的信息
} SHTCTL;

SHTCTL *shtctl_init(MEMMAN *memman, unsigned char *vram, int xsize, int ysize);
SHEET *sheet_alloc(SHTCTL *ctl);
void sheet_setbuf(SHEET *sht, unsigned char *buf, int xsize, int ysize, int col_inv);
void sheet_updown(SHTCTL *ctl, SHEET *sht, int height);
void sheet_refresh(SHTCTL *ctl, SHEET *sht, int bx0, int by0, int bx1, int by1);
void sheet_slide(SHTCTL *ctl, SHEET *sht, int vx0, int vy0);
void sheet_free(SHTCTL *ctl, SHEET *sht);