#include "bootpack.h"

#define FLAGS_OVERRUN       0x0001

/* 初始化FIFO缓冲区 */
void fifo8_init(FIFO8 *fifo, int size, unsigned char *buf)
{
    fifo->size = size;
    fifo->buf = buf;
    fifo->free = size; // 缓冲区的大小
    fifo->flags = 0;
    fifo->w = 0; // 下一个数据写入位置
    fifo->r = 0; // 下一个数据读出位置
    return;
}

/* 向FIFO发送数据并保存 */
int fifo8_put(FIFO8 *fifo, unsigned char data)
{
    if (fifo->free == 0) { // 已没有空余，溢出
        fifo->flags |= FLAGS_OVERRUN;
        return -1;
    }
    fifo->buf[fifo->w] = data;
    fifo->w++;
    if (fifo->w == fifo->size) {
        fifo->w = 0;
    }
    fifo->free--;
    return 0;
}

/* 从FIFO取得一个数据 */
int fifo8_get(FIFO8 *fifo)
{
    int data;
    if (fifo->free == fifo->size) { // 缓冲区为空，返回-1
        return -1;
    }
    data = fifo->buf[fifo->r];
    fifo->r++;
    if (fifo->r == fifo->size) {
        fifo->r = 0;
    }
    fifo->free++;
    return data;
}

/* 查询FIFO保存的数据量 */
int fifo8_status(FIFO8 *fifo)
{
    return fifo->size - fifo->free;
}