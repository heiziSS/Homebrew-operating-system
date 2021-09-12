/* 
    关于GDT, IDT等 descriptor table的处理
*/

#include "bootpack.h"

/*
    初始化GDT和IDT
    GDT: global segment descriptor table 全局段号记录表 （8192x8=65536 BYTE （64KB））
        段寄存器是16位的，但是由于设计上的原因，段寄存器的低3位不能使用，故段号为0~8191
        用于存储段号和段信息，保存于内存中，内存起始地址和有效设定个数存放于特殊寄存器GDTR中
    IDT: interrupt descriptor table 中断记录表
        记录了0~255的中断号码与调用函数的对应关系
*/
void init_gdtidt(void)
{
    SEGMENT_DESCRIPTOR *gdt = (SEGMENT_DESCRIPTOR *) ADR_GDT;
    GATE_DESCRIPTOR    *idt = (GATE_DESCRIPTOR    *) ADR_IDT;
    int i;

    // GDT初始化
    for (i = 0; i <= LIMIT_GDT / 8; i++) {
        set_segmdesc(gdt + i, 0, 0, 0);
    }
    // 设定段号为1的段
    // 上限值为0xffffffff，即4GB，CPU能管理的全部内存
    // 地址是0x00000000, 属性值为0x4092
    set_segmdesc(gdt + 1, 0xffffffff, 0x00000000, AR_DATA32_RW);
    // 设定段号为2的段
    // 上限值为0x0007ffff，即5125B，为bootpack.hrb准备
    // 地址是0x00280000, 属性值为0x409a
	set_segmdesc(gdt + 2, LIMIT_BOTPAK, ADR_BOTPAK, AR_CODE32_ER);
    load_gdtr(LIMIT_GDT, ADR_GDT); // 给GDTR赋值

    // IDT初始化
    for (i = 0; i <= LIMIT_IDT / 8; i++) {
        set_gatedesc(idt + i, 0, 0, 0);
    }
    load_idtr(LIMIT_IDT, ADR_IDT);

    // 设定IDT
    set_gatedesc(idt + 0x20, (int) asm_inthandler20, 2 * 8, AR_INTGATE32);
    set_gatedesc(idt + 0x21, (int) asm_inthandler21, 2 * 8, AR_INTGATE32);
    set_gatedesc(idt + 0x27, (int) asm_inthandler27, 2 * 8, AR_INTGATE32);
    set_gatedesc(idt + 0x2c, (int) asm_inthandler2c, 2 * 8, AR_INTGATE32);

    return;
}

/*
    设置段属性：
        limit(20位): 段的大小；
                     Gbit为 1 时，表示页page（每页4KB）可以指定4GB的段，
                     Gbit为 0 时，表示字节byte，可以指定1MB的段
        base (32位): 段的起始地址；base_low(2), base_mid(1), base_high(1), 总共4字节32位地址，此处分三段便于兼容80286和386
        ar   (12位): 
                    bit15:指定Gbit
                    bit14:指定段模式，1是32位模式，0是16位模式
                    bit11-bit0:指定段的管理权限
                        段的管理权限（禁止写入、禁止执行、系统专用等）；
                            00000000（0x00）:未使用的记录表
                            10010010（0x92）:系统专用，可读写的段，不可执行
                            10011010（0x9a）:系统专用，可执行的段，可读不可写
                            11110010（0xf2）:应用程序用，可读写的段，不可执行
                            11111010（0xfa）:应用程序用，可执行的段，可读不可写
                     注：CPU处于系统模式或者应用模式，取决于执行中的应用程序位于0x9a的段或0xfa的段
        CPU用8个字节的数据表示这些信息
*/
void set_segmdesc(SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar)
{
    if (limit > 0xfffff) {
        ar |= 0x8000;   // G_bit = 1
        limit /= 0x1000;
    }
    sd->limit_low    = limit & 0xffff;
    sd->base_low     = base & 0xffff;
    sd->base_mid     = (base >> 16) & 0xff;
    sd->access_right = ar & 0xff;
    sd->limit_high   = ((limit >> 16) & 0x0f) | ((ar >> 8) & 0xf0);
    sd->base_high    = (base >> 24) & 0xff;
	return;
}

void set_gatedesc(GATE_DESCRIPTOR *gd, int offset, int selector, int ar)
{
    gd->offset_low   = offset & 0xffff;
    gd->selector     = selector;
    gd->dw_count     = (ar >> 8) & 0xff;
    gd->access_right = ar & 0xff;
    gd->offset_high  = (offset >> 16) & 0xffff;
    return;
}