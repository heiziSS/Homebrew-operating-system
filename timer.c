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
    timerctl.runningTimersHead.next = NULL;
    for (i = 0; i < MAX_TIMER; i++) {
        timerctl.timers[i].flags = TIMER_FLAGS_NOTUSE;
    }
    return;
}

/* 分配定时器 */
TIMER *timer_alloc(void)
{
    int i;
    for (i = 0; i < MAX_TIMER; i++) {
        if (timerctl.timers[i].flags == TIMER_FLAGS_NOTUSE) {
            timerctl.timers[i].flags = TIMER_FLAGS_ALLOC;
            return &timerctl.timers[i];
        }
    }
    return NULL;
}

/* 释放定时器 */
void timer_free(TIMER *timer)
{
    TIMER *pre = &timerctl.runningTimersHead;
    TIMER *post = pre->next;
    // 若该定时器处于运行状态，则需要将其从运行定时器链表中删除
    if (timer->flags == TIMER_FLAGS_USING) {
        while (post != NULL) {
            if (post == timer) {
                pre->next = timer->next;
                break;
            }
            pre = post;
            post = post->next;
        }
    }
    timer->next = NULL;
    timer->flags = TIMER_FLAGS_NOTUSE;
    return;
}

/* 定时器初始化 */
void timer_init(TIMER *timer, FIFO *fifo, int data)
{
    timer->fifo = fifo;
    timer->data = data;
    timer->next = NULL;
    return;
}

/*
    设置定时器时间，单位：10ms
    timer注册到runningTimers中，注册时不能被中断打断，所以需要先关闭中断
*/
void timer_settime(TIMER *timer, unsigned int timeout)
{
    int e;
    TIMER *pre = &timerctl.runningTimersHead;
    TIMER *post = pre->next;
    timer->timeout = timeout + timerctl.count;
    timer->flags = TIMER_FLAGS_USING;

    e = io_load_eflags();
    io_cli();   // 关闭中断

    while (post != NULL) {
        if (post->timeout > timer->timeout) {
            break;
        }
        pre = post;
        post = post->next;
    }
    pre->next = timer;
    timer->next = post;

    io_store_eflags(e); //开启中断
    return;
}

void inthandler20(int *esp)
{
    TIMER *t = timerctl.runningTimersHead.next;

    io_out8(PIC0_OCW2, 0x60);   // 把IRQ-0信号接收完了的信息通知给PIC
    timerctl.count++;
    if ((t == NULL) || (t->timeout > timerctl.count)) {
        return;     // 还不到下一个时刻，或者当前没有定时器处于运行状态
    }

    while (t != NULL) {
        if (t->timeout > timerctl.count) {
            break;
        }
        // 处理超时定时器
        t->flags = TIMER_FLAGS_ALLOC;
        fifo_put(t->fifo, t->data);
        t = t->next;
    }
    timerctl.runningTimersHead.next = t;

    return;
}
