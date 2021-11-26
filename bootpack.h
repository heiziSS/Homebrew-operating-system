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
void io_hlt(void);                      // CPU暂停指令
void io_cli(void);                      // CLI(Clear Interrupt) 中断标志置0指令 使 IF=0 禁止中断发生
void io_sti(void);                      // STI(Set Interrupt) 中断标志置1指令 使 IF=1 允许中断发生
void io_stihlt(void);
int io_in8(int port);
void io_out8(int port, int data);
int io_load_eflags(void);               // 寄存器EFLAGS由16位寄存器FLAGS扩展而来的32位寄存器，存储进位标志和中断标志等标志
void io_store_eflags(int eflags);
void load_gdtr(int limit, int addr);    // 将指定的段上限（limit）和地址值赋值给名为GDTR的48位寄存器
void load_idtr(int limit, int addr);
int load_cr0(void);                     // 为了禁止缓存，需要对CR0寄存器的某一标志位进行操作
void store_cr0(int cr0);
void load_tr(int tr);
void asm_inthandler20(void);
void asm_inthandler21(void);
void asm_inthandler27(void);
void asm_inthandler2c(void);
unsigned int memtest_sub(unsigned int start, unsigned int end);
void farjmp(int eip, int cs); // far模式跳转指令，地址段（冒号前面的4*8的部分）要指向TSS这一点比较重要，而偏移量（冒号后面的0的部分）并没有什么实际作用，会被忽略掉，一般写0

/* fifo.c */
typedef struct {
    int *buf;
    int w, r, size, free, flags;
    struct task *task;     //有数据写入时需要唤醒的任务
} FIFO;

void fifo_init(FIFO *fifo, int size, int *buf, struct task *task);
int fifo_put(FIFO *fifo, int data);
int fifo_get(FIFO *fifo);
int fifo_status(FIFO *fifo);

/* graphic.c */
#define COL8_000000     0       // 黑
#define COL8_FF0000     1       // 亮红
#define COL8_00FF00     2       // 亮绿
#define COL8_FFFF00     3       // 亮黄
#define COL8_0000FF     4       // 亮蓝
#define COL8_FF00FF     5       // 亮紫
#define COL8_00FFFF     6       // 浅亮蓝
#define COL8_FFFFFF     7       // 白
#define COL8_C6C6C6     8       // 亮灰
#define COL8_840000     9       // 暗红
#define COL8_008400     10      // 暗绿
#define COL8_848400     11      // 暗黄
#define COL8_000084     12      // 暗青
#define COL8_840084     13      // 暗紫
#define COL8_008484     14      // 浅暗蓝
#define COL8_848484     15      // 暗灰

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
#define ADR_IDT         0x0026f800      // 存放IDT的起始地址
#define LIMIT_IDT       0x000007ff      // 存放IDT的空间大小
#define ADR_GDT         0x00270000      // 存放GDT的起始地址
#define LIMIT_GDT       0x0000ffff      // 存放GDT的空间大小
#define ADR_BOTPAK      0x00280000      // 存放bootpack.hrb的起始地址
#define LIMIT_BOTPAK    0x0007ffff      // 存放bootpack.hrb的空间大小512Kb
#define AR_DATA32_RW    0x4092
#define AR_CODE32_ER    0x409a
#define AR_TSS32        0x0089
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
void inthandler27(int *esp);

/* keyboard.c */
#define PORT_KEYDAT     0x0060
#define PORT_KEYCMD     0x0064

void inthandler21(int *esp);
void wait_KBC_sendready(void);
void init_keyboard(FIFO *fifo, int data0);

/* mouse.c */
typedef struct {
    unsigned char buf[3], phase;
    int x, y, btn;
} MOUSE_DEC;

void inthandler2c(int *esp);
void enable_mouse(FIFO *fifo, int data0, MOUSE_DEC *mdec);
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
    struct stSHTCTL* ctl;
} SHEET;

typedef struct stSHTCTL {
    unsigned char *vram;         // VRAM的地址
    unsigned char *map;          // 标记图像上每个像素点所属层
    int xsize;                   // 整个显示画面的大小
    int ysize;                   // 整个显示画面的大小
    int top;                     // 最上面图层的高度
    SHEET *DisplayedSheets[MAX_SHEETS];  // 存放所有图层的地址
    SHEET sheets[MAX_SHEETS];    // 存放所有图层的信息
} SHTCTL;

SHTCTL *shtctl_init(MEMMAN *memman, unsigned char *vram, int xsize, int ysize);
SHEET *sheet_alloc(SHTCTL *ctl);
void sheet_setbuf(SHEET *sht, unsigned char *buf, int xsize, int ysize, int col_inv);
void sheet_updown(SHEET *sht, int height);
void sheet_refresh(SHEET *sht, int bx0, int by0, int bx1, int by1);
void sheet_slide(SHEET *sht, int vx0, int vy0);
void sheet_free(SHEET *sht);

/* public */
#define MAX(a, b)   (((a) > (b)) ? (a) : (b))
#define MIN(a, b)   (((a) < (b)) ? (a) : (b))
#define NULL        0
#define UINT_MAX    0xffffffff
#define TRUE        1
#define FALSE       0

/* timer.c */
#define MAX_TIMER       500

typedef struct timer {
    unsigned int timeout;   // 定时时间
    unsigned int flags;     // 记录定时器的状态
    FIFO *fifo;
    int data;               // 定时时间到达后向fifo发送的数据
    struct timer *next;
} TIMER;

typedef struct {
    unsigned int count;         // 计数器
    TIMER runningTimersHead;    // 运行定时器链表的头部
    TIMER timers[MAX_TIMER];
} TIMERCTL;

extern TIMERCTL timerctl;
void init_pit(void);
TIMER *timer_alloc(void);
void timer_free(TIMER *timer);
void timer_init(TIMER *timer, FIFO *fifo, int data);
void timer_settime(TIMER *timer, unsigned int timeout);
void inthandler20(int *esp);

/* mtask.c */
#define MAX_TASKS       1000    // 最大任务数量
#define MAX_TASKS_PER_LEVEL     100 //每个level中最多的任务数
#define MAX_TASKLEVELS          10
#define TASK_GDT0       3       // 定义从GDT的几号开始分配给TSS

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

typedef struct task {
    int sel;        //存放GDT的编号
    int status;     //任务当前的状态
    int priority;   //任务优先级
    int level;      //所属level号
    FIFO fifo;
    TSS32 tss;
    struct task *next;
} TASK;

typedef struct {
    TASK tasksHead;      // 正在运行的任务链表的头部
    TASK *pTasksTail;    // 运行任务链表尾指针
    int taskNum;
} TASKLEVEL;

typedef struct {
    int curLevel;   //当前活动中的level
    TASK *curTask;  //当前正在运行的任务
    TASKLEVEL levels[MAX_TASKLEVELS];
    TASK tasks[MAX_TASKS];
} TASKCTL;

enum {
    TASK_NOTUSE,
    TASK_ALLOC,
    TASK_RUNNING,
    TASK_MAX,
};

extern TIMER *g_taskTimer;

TASK *task_init(MEMMAN *memman);
TASK *task_alloc(void);
void task_run(TASK *task, int level, int priority);
void task_switch(void);
void task_sleep(TASK *task);
TASK *task_now(void);
