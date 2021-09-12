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
    return;
}

void inthandler20(int *esp)
{
    io_out8(PIC0_OCW2, 0x60);   // 把IRQ-0信号接收完了的信息通知给PIC
    timerctl.count++;
    return;
}
