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
    if (ctl == NULL) {
        return NULL;   // 返回无效地址
    }
    ctl->map = (unsigned char *) memman_alloc_4k(memman, xsize * ysize);
    if (ctl->map == NULL) {
        memman_free_4k(memman, (unsigned int) ctl, sizeof(SHTCTL));
        return NULL;
    }
    ctl->vram = vram;
    ctl->xsize = xsize;
    ctl->ysize = ysize;
    ctl->top = -1;  // 一个SHEET都没有
    for(i = 0; i < MAX_SHEETS; i++) {
        ctl->sheets[i].flags = SHEET_NOT_USE;  // 标记为未使用
        ctl->sheets[i].ctl = ctl;
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
    return NULL;   // 所有的SHEET都处于正在使用状态
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
    刷新图层地图
    (vx0, vy0):指定区域的左上角点
    (vx1, vy1):指定区域的右下角点
    h0:刷新 h0 到 最高层的图层
*/
void sheet_refreshmap(SHTCTL *ctl, int vx0, int vy0, int vx1, int vy1, int h0)
{
    int h, bx, by, vx, vy, bx0, by0, bx1, by1;
    unsigned char *buf, sheetId, *map = ctl->map;
    SHEET *sht;

    // 如果refresh的范围超出了画面则修正
    vx0 = MAX(vx0, 0);
    vy0 = MAX(vy0, 0);
    vx1 = MIN(vx1, ctl->xsize);
    vy1 = MIN(vy1, ctl->ysize);

    for (h = h0; h <= ctl->top; h++) {
        sht = ctl->DisplayedSheets[h];
        sheetId = sht - ctl->sheets;    // 将该图层地址与第一个图层的地址的差值作为该图层的ID号
        buf = sht->buf;
        bx0 = MAX(vx0 - sht->vx0, 0);
        by0 = MAX(vy0 - sht->vy0, 0);
        bx1 = MIN(vx1 - sht->vx0, sht->bxsize);
        by1 = MIN(vy1 - sht->vy0, sht->bysize);
        for (by = by0; by < by1; by++) {
            vy = sht->vy0 + by;
            for (bx = bx0; bx < bx1; bx++) {
                vx = sht->vx0 + bx;
                if (buf[by * sht->bxsize + bx] != sht->col_inv) {
                    map[vy * ctl->xsize + vx] = sheetId;
                }
            }
        }
    }
    return;
}

/*
    刷新所有图层中的指定区域
    (vx0, vy0):指定区域的左上角点
    (vx1, vy1):指定区域的右下角点
    h0, h1:刷新 h0 到 h1 的图层
*/
void sheet_refreshsub(SHTCTL *ctl, int vx0, int vy0, int vx1, int vy1, int h0, int h1)
{
    int bx, by, vx, vy;
    unsigned char *vram = ctl->vram, *map = ctl->map;
    SHEET *sht;

    // 如果refresh的范围超出了画面则修正
    vx0 = MAX(vx0, 0);
    vy0 = MAX(vy0, 0);
    vx1 = MIN(vx1, ctl->xsize);
    vy1 = MIN(vy1, ctl->ysize);

    for (vy = vy0; vy < vy1; vy++) {
        for (vx = vx0; vx < vx1; vx++) {
            sht = ctl->sheets + map[vy * ctl->xsize + vx];
            if (h0 <= sht->height && sht->height <= h1) {
                by = vy - sht->vy0;
                bx = vx - sht->vx0;
                vram[vy * ctl->xsize + vx] = sht->buf[by * sht->bxsize + bx];
            }
        }
    }
    return;
}

/* 
    设置图层高度 
    ctl:    图层管理器
    sht:    需要重新设定高度的图层
    height: 图层被设定的新高度
*/
void sheet_updown(SHEET *sht, int height)
{
    SHTCTL *ctl = sht->ctl;
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

    // DisplayedSheets[]重新排列
    if (preHeight > height) { // 图层高度需降低
        if (height >= 0) { // 中间图层上移一层
            for (h = preHeight; h > height; h--) {
                ctl->DisplayedSheets[h] = ctl->DisplayedSheets[h - 1];
                ctl->DisplayedSheets[h]->height = h;
            }
            ctl->DisplayedSheets[height] = sht;
            // 按新图层的信息重新绘制画面
            sheet_refreshmap(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, height + 1);
            sheet_refreshsub(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, height + 1, preHeight);
        } else { // 该图层需隐藏
            // 该图层以上的图层需要下降一层
            for (h = preHeight; h < ctl->top; h++) {
                ctl->DisplayedSheets[h] = ctl->DisplayedSheets[h+1];
                ctl->DisplayedSheets[h]->height = h;
            }
            ctl->top--; //显示中的图层减少了一个，图层高度减一
            // 按新图层的信息重新绘制画面
            sheet_refreshmap(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, 0);
            sheet_refreshsub(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, 0, preHeight - 1);
        }
    } else if (preHeight < height) { // 图层高度需上升
        if (preHeight >= 0) {
            //中间图层需下移一层
            for (h = preHeight; h < height; h++) {
                ctl->DisplayedSheets[h] = ctl->DisplayedSheets[h + 1];
                ctl->DisplayedSheets[h]->height = h;
            }
            ctl->DisplayedSheets[height] = sht;
        } else { // 由隐藏转为显示状态
            // 将height以上的图层上移一层
            for (h = ctl->top; h >= height; h--) {
                ctl->DisplayedSheets[h + 1] = ctl->DisplayedSheets[h];
                ctl->DisplayedSheets[h + 1]->height = h + 1;
            }
            ctl->DisplayedSheets[height] = sht;
            ctl->top++; //显示图层增加了一层，图层高度加一
        }
        // 按新图层的信息重新绘制画面
        sheet_refreshmap(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, height);
        sheet_refreshsub(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, height, height);
    }
    return;
}

/*
    刷新某个图层的指定区域
    sht:需要刷新的图层
    (bx0, by0):图层中需要刷新的区域在该图层缓冲中的左上角
    (bx1, by1):图层中需要刷新的区域在该图层缓冲中的右下角
*/
void sheet_refresh(SHEET *sht, int bx0, int by0, int bx1, int by1)
{
    if (sht->height >= 0) {
        sheet_refreshsub(sht->ctl, sht->vx0 + bx0, sht->vy0 + by0, sht->vx0 + bx1, sht->vy0 + by1, 
                        sht->height, sht->height);
    }
    return;
}

/*
    不改变图层高度，仅上下左右移动图层
    sht: 需要移动的图层
    vx0: 图层移动后在图像上的新坐标
    vy0: 图层移动后在图像上的新坐标
*/
void sheet_slide(SHEET *sht, int vx0, int vy0)
{
	SHTCTL *ctl = sht->ctl;
    int old_vx0 = sht->vx0, old_vy0 = sht->vy0;
    sht->vx0 = vx0;
    sht->vy0 = vy0;
    // 该图层如果正在显示，则需刷新画面
    if (sht->height >= 0) {
		sheet_refreshmap(ctl, old_vx0, old_vy0, old_vx0 + sht->bxsize, old_vy0 + sht->bysize, 0);
		sheet_refreshmap(ctl, vx0, vy0, vx0 + sht->bxsize, vy0 + sht->bysize, sht->height);
		sheet_refreshsub(ctl, old_vx0, old_vy0, old_vx0 + sht->bxsize, old_vy0 + sht->bysize, 0, sht->height - 1);
		sheet_refreshsub(ctl, vx0, vy0, vx0 + sht->bxsize, vy0 + sht->bysize, sht->height, sht->height);
    }
    return;
}

/*
    释放图层
    ctl: 图层管理器
    sht: 需要被释放的图层
*/
void sheet_free(SHEET *sht)
{
    if (sht->height >= 0) {
        sheet_updown(sht, -1);
    }
    sht->flags = SHEET_NOT_USE;
	return;
}
