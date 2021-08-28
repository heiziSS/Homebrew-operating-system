
#include "bootpack.h"
#include <stdio.h>

/*
    可编程中断控制器（programmable interrupt controller）
    监控输入管脚的8个中断信号，任意一个中断信号进来，通过唯一的输出管脚通知给CPU
    电脑里有两个PIC，主PIC与CPU直连，从PIC与主PIC 2号IRQ相连
    PIC0:IRQ0-IRQ7   PIC1:IRQ8-IRQ15

    IMR 中断屏蔽寄存器 interrupt mask regisiter 8位分别对应8路IRQ信号，某一位置1则该位对应的IRQ信号被屏蔽
    ICW 初始化控制数据 initial control word 编号1-4 共四个字节
        ICW1和ICW4：与PIC主板配线方式、中断信号的电气特性相关
        ICW3：有关主从连接的设定
        ICW2：决定IRQ以哪一号中断通知CPU
        OCW2：将 "0x60+IRQ号" 输出给OCW2，即可通知PIC已经发生了该IRQ号指定的中断，之后PIC才会继续监控该IRQ号的中断
*/
void init_pic(void)
{
    io_out8(PIC0_IMR, 0xff);    // 禁止所有中断
    io_out8(PIC1_IMR, 0xff);    // 禁止所有中断

    io_out8(PIC0_ICW1, 0x11);   // 边沿触发模式（edge trigger mode）
    io_out8(PIC0_ICW2, 0x20);   // IRQ0-7由INT20-27接收，INT0x00-0x1f用于CPU操作系统异常处理
    io_out8(PIC0_ICW3, 1 << 2); // PIC1由IRQ2连接
    io_out8(PIC0_ICW4, 0x01);   // 无缓冲区模式

    io_out8(PIC1_ICW1, 0x11);   // 边沿触发模式（edge trigger mode）
    io_out8(PIC1_ICW2, 0x28);   // IRQ8-15由INT28-2f接收
    io_out8(PIC1_ICW3, 2);      // PIC1由IRQ2连接
    io_out8(PIC1_ICW4, 0x01);   // 无缓冲区模式

    io_out8(PIC0_IMR, 0xfb);    // 11111011 PIC1以外全部禁止
    io_out8(PIC1_IMR, 0xff);    // 11111111 禁止所有中断

    return;
}

/*
    来自PIC0的不完全中断对策
    在Athlon64X2机等中，由于芯片组方便，在PIC初始化时，该中断只发生一次
    此中断处理函数在不执行任何操作的情况下浪费中断。
    我为什么不做任何事情？
        由于此中断是由 PIC 初始化时的电气噪声引起的，因此无需认真处理任何操作。
*/
void inthandler27(int *esp)
{
    io_out8(PIC0_OCW2, 0x67);   // 通知图片受理完成IRQ-07（参照7-1）
    return;
}