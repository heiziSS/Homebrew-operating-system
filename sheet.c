#include "bootpack.h"

#define SHEET_USE       1
#define SHEET_NOT_USE   0

/* 
    初始化图层管理列表 
    memman: 内存管理器，用于给图层开辟内存
    vram:   图像缓冲区的地址
    xsize:  整个显示图像的宽
    ysize:  整个显示图像的高
*/
SHTCTL *shtctl_init(MEMMAN *memman, unsigned char *vram, int xsize, int ysize)
{
    SHTCTL *ctl;
    int i;
    ctl = (SHTCTL *) memman_alloc_4k(memman, sizeof(SHTCTL));
    if (ctl == 0) {
        return ctl;
    }
    ctl->vram = vram;
    ctl->xsize = xsize;
    ctl->ysize = ysize;
    ctl->top = -1;  // 一个SHEET都没有
    for(i = 0; i < MAX_SHEETS; i++) {
        ctl->sheets[i].flags = SHEET_NOT_USE;  // 标记为未使用
    }
    return ctl;
}

/*
    分配图层，从图层管理器中查询可用的图层
*/
SHEET *sheet_alloc(SHTCTL *ctl)
{
    SHEET *sht;
    int i;
    for (i = 0; i < MAX_SHEETS; i++) {
        if (ctl->sheets[i].flags == 0) {
            sht = &(ctl->sheets[i]);
            sht->flags = SHEET_USE;     // 标记为正在使用
            sht->height = -1;   // 隐藏
            return sht;
        }
    }
    return 0;   // 所有的SHEET都处于正在使用状态
}

/* 
    设置图层所描绘的内容 
    sht:      需要设置的图层
    buf:      描绘图像的缓冲
    xsize:    描绘图像的宽
    ysize:    描绘图像的高
    col_inv:  隐藏颜色
*/
void sheet_setbuf(SHEET *sht, unsigned char *buf, int xsize, int ysize, int col_inv)
{
    sht->buf = buf;
    sht->bxsize = xsize;
    sht->bysize = ysize;
    sht->col_inv = col_inv;
    return;
}

/* 
    设置图层高度 
    ctl:    图层管理器
    sht:    需要重新设定高度的图层
    height: 图层被设定的新高度
*/
void sheet_updown(SHTCTL *ctl, SHEET *sht, int height)
{
    int h;
    int preHeight = sht->height;    // 保存修改前的高度

    // 若指定的高度过高或过低，则进行修正
    if (height > ctl->top + 1) {
        height = ctl->top + 1;
    }
    if (height < -1) {
        height = -1;
    }
    sht->height = height; // 设定高度

    // PSheets[]重新排列
    if (preHeight > height) { // 图层高度需降低
        if (height >= 0) { // 中间图层上移一层
            for (h = preHeight; h > height; h--) {
                ctl->PSheets[h] = ctl->PSheets[h - 1];
                ctl->PSheets[h]->height = h;
            }
            ctl->PSheets[height] = sht;
        } else { // 该图层需隐藏
            // 该图层以上的图层需要下降一层
            for (h = preHeight; h < ctl->top; h++) {
                ctl->PSheets[h] = ctl->PSheets[h+1];
                ctl->PSheets[h] = h;
            }
            ctl->top--; //显示中的图层减少了一个，图层高度减一
        }
        sheet_refresh(ctl); // 按新图层的信息重新绘制画面
    } else if (preHeight < height) { // 图层高度需上升
        if (preHeight >= 0) {
            //中间图层需下移一层
            for (h = preHeight; h < height; h++) {
                ctl->PSheets[h] = ctl->PSheets[h + 1];
                ctl->PSheets[h]->height = h;
            }
            ctl->PSheets[height] = sht;
        } else { // 由隐藏转为显示状态
            // 将height以上的图层上移一层
            for (h = ctl->top; h >= height; h--) {
                ctl->PSheets[h + 1] = ctl->PSheets[h];
                ctl->PSheets[h + 1] = h + 1;
            }
            ctl->PSheets[height] = sht;
            ctl->top++; //显示图层增加了一层，图层高度加一
        }
        sheet_refresh(ctl); // 按新图层的信息重新绘制画面
    }
    return;
}

/* 刷新图层 */
void sheet_refresh(SHTCTL *ctl)
{
    int h, bx, by, vx, vy;
    unsigned char *buf, c, *vram = ctl->vram;
    SHEET *sht;
    for (h = 0; h <= ctl->top; h++) {
        sht = ctl->PSheets[h];
        buf = sht->buf;
        for (by = 0; by < sht->bysize; by++) {
            vy = sht->vy0 + by;
            for (bx = 0; bx < sht->bxsize; bx++) {
                vx = sht->vx0 + bx;
                c = buf[by * sht->bxsize + bx];
                if (c != sht->col_inv) {
                    vram[vy * ctl->xsize + vx] = c;
                }
            }
        }
    }
    return;
}

/*
    不改变图层高度，仅上下左右移动图层
    ctl: 图层管理器
    sht: 需要移动的图层
    vx0: 图层移动后在图像上的新坐标
    vy0: 图层移动后在图像上的新坐标
*/
void sheet_slide(SHTCTL *ctl, SHEET *sht, int vx0, int vy0)
{
    sht->vx0 = vx0;
    sht->vy0 = vy0;
    // 该图层如果正在显示，则需刷新画面
    if (sht->height >= 0) {
        sheet_refresh(ctl);
    }
    return;
}

/*
    释放图层
    ctl: 图层管理器
    sht: 需要被释放的图层
*/
void sheet_free(SHTCTL *ctl, SHEET *sht)
{
    if (sht->height >= 0) {
        sheet_updown(ctl, sht, -1); 
    }
    sht->flags = SHEET_NOT_USE;
}