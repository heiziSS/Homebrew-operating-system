#include "bootpack.h"

#define PIT_CTRL    0x0043
#define PIT_CNT0    0x0040

TIMERCTL timerctl;

#define TIMER_FLAGS_NOTUSE      0   // 未使用
#define TIMER_FLAGS_ALLOC       1   // 已配置状态
#define TIMER_FLAGS_USING       2   // 定时器运行中

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
    int i;
    io_out8(PIT_CTRL, 0x34);
    io_out8(PIT_CNT0, 0x9c);    // 中断周期的低8位
    io_out8(PIT_CNT0, 0x2e);    // 中断周期的高8位
    timerctl.count = 0;
    for (i = 0; i < MAX_TIMER; i++) {
        timerctl.timer[i].flags = TIMER_FLAGS_NOTUSE;
    }
    return;
}

/* 分配定时器 */
TIMER *timer_alloc(void)
{
    int i;
    for (i = 0; i < MAX_TIMER; i++) {
        if (timerctl.timer[i].flags == TIMER_FLAGS_NOTUSE) {
            timerctl.timer[i].flags = TIMER_FLAGS_ALLOC;
            return &timerctl.timer[i];
        }
    }
    return NULL;
}

/* 释放定时器 */
void timer_free(TIMER *timer)
{
    timer->flags = TIMER_FLAGS_NOTUSE;
    return;
}

/* 定时器初始化 */
void timer_init(TIMER *timer, FIFO8 *fifo, unsigned char data)
{
    timer->fifo = fifo;
    timer->data = data;
    return;
}

/* 设置定时器时间 */
void timer_settime(TIMER *timer, unsigned int timeout)
{
    timer->timeout = timeout;
    timer->flags = TIMER_FLAGS_USING;
    return;
}

void inthandler20(int *esp)
{
    int i;
    io_out8(PIC0_OCW2, 0x60);   // 把IRQ-0信号接收完了的信息通知给PIC
    timerctl.count++;
    for (i = 0; i < MAX_TIMER; i++) {
        if (timerctl.timer[i].flags == TIMER_FLAGS_USING) {
            timerctl.timer[i].timeout--;
            if (timerctl.timer[i].timeout == 0) {
                timerctl.timer[i].flags = TIMER_FLAGS_ALLOC;
                fifo8_put(timerctl.timer[i].fifo, timerctl.timer[i].data);
            }
        }
    }
    return;
}
