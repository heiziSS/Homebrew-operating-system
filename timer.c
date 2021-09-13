#include "bootpack.h"

TIMERCTL timerctl;

#define PIT_CTRL    0x0043
#define PIT_CNT0    0x0040

/*
    管理定时器
    PIT Progammable Interval Timer 可编程的间隔型定时器
    PIT 连接着IRQ的0号
    中断频率 = 单位时间时钟周期数（即主频）/设定的数值
    设定值 = 1000，中断频率 = 1.19318KHz
    设定值 = 10000，中断频率 = 119.318Hz
    此处设定值为11932（0x2e9c），中断频率大约是100Hz，即每10ms产生一次中断
*/
void init_pit(void)
{
    io_out8(PIT_CTRL, 0x34);
    io_out8(PIT_CNT0, 0x9c);    // 中断周期的低8位
    io_out8(PIT_CNT0, 0x2e);    // 中断周期的高8位
    timerctl.count = 0;
    timerctl.timeout = 0;
    return;
}

void inthandler20(int *esp)
{
    io_out8(PIC0_OCW2, 0x60);   // 把IRQ-0信号接收完了的信息通知给PIC
    timerctl.count++;
    if (timerctl.timeout > 0) {
        timerctl.timeout--;
        if (timerctl.timeout == 0) {
            fifo8_put(timerctl.fifo, timerctl.data);
        }
    }
    return;
}

/* 设置计时器 */
void settimer(unsigned int timeout, FIFO8 *fifo, unsigned char data)
{
    int eflags;
    eflags = io_load_eflags();
    io_cli();   // 如果设定还没有完全结束IRQ0的中断就进来的话，会引起混乱，所以我们先禁止中断
    timerctl.timeout = timeout;
    timerctl.fifo = fifo;
    timerctl.data = data;
    io_store_eflags(eflags);    // 把中断状态复原
    return;
}