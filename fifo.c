/*
    写入FIFO的数值
    0~1:光标闪烁用定时器
    3：3秒定时器
    10：10秒定时器
    256~512：键盘输入（从键盘控制器读入的值再加上256）
    512~767：鼠标输入（从键盘控制器读入的值再加上512）
*/


#include "bootpack.h"

#define FLAGS_OVERRUN       0x0001

/* 初始化FIFO缓冲区 */
void fifo_init(FIFO *fifo, int size, int *buf, TASK *task)
{
    fifo->size = size;
    fifo->buf = buf;
    fifo->free = size; // 缓冲区的大小
    fifo->flags = 0;
    fifo->w = 0; // 下一个数据写入位置
    fifo->r = 0; // 下一个数据读出位置
    fifo->task = task; //有数据写入时需要唤醒的任务
    return;
}

/* 向FIFO发送数据并保存 */
int fifo_put(FIFO *fifo, int data)
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
    if (fifo->task != 0) {
        if (fifo->task->flags != TASK_RUNNING) { //如果该任务处于休眠状态
            task_run(fifo->task);
        }
    }
    return 0;
}

/* 从FIFO取得一个数据 */
int fifo_get(FIFO *fifo)
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
int fifo_status(FIFO *fifo)
{
    return fifo->size - fifo->free;
}