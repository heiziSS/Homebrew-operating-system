#include "bootpack.h"

#define EFLAGS_AC_BIT       0x00040000
#define CR0_CACHE_DISABLE   0x60000000

/*
    内存检查：往内存写值再马上读取，检查前后值是否相等来判断内存连接是否正常
    注：内存检查时需要将缓存设为OFF，否则写入读出的值可能不是内存而是缓存，结果所有内存都 “正常”
*/
unsigned int memtest(unsigned int start, unsigned int end)
{
    char flg486 = 0;
    unsigned int eflg, cr0, i;
    
    /* 确认CPU是386还是486以上的，486以上才有缓存 */
    eflg = io_load_eflags();
    eflg |= EFLAGS_AC_BIT;  // AC bit 为 1
    io_store_eflags(eflg);
    eflg = io_load_eflags();
    if ((eflg & EFLAGS_AC_BIT) != 0) { // 如果是386，即使设定AC=1，AC值还会自动回到0
        flg486 = 1;
    }
    eflg &= ~EFLAGS_AC_BIT; // AC bit设为 0
    io_store_eflags(eflg);

    if (flg486 != 0) {
        cr0 = load_cr0();
        cr0 |= CR0_CACHE_DISABLE;   // 关闭缓存
        store_cr0(cr0);
    }

    i = memtest_sub(start, end);

    if (flg486 !=  0) {
        cr0 = load_cr0();
        cr0 &= ~CR0_CACHE_DISABLE;  // 开启缓存
        store_cr0(cr0);
    }

    return i;
}

/* 初始化内存管理列表 */
void memman_init(MEMMAN *man)
{
    man->frees = 0;         // 可用信息数目
    man->maxfrees = 0;      // 用于观察可用状况：frees的最大值
    man->lostsize = 0;      // 释放失败的内存的大小总和
    man->losts = 0;         // 释放失败次数
    return;
}

/* 统计空余内存大小 */
unsigned int memman_total(MEMMAN *man)
{
    unsigned int i, t = 0;
    for (i = 0; i < man->frees; i++) {
        t += man->free[i].size;
    }
    return t;
}

/* 分配内存 */
unsigned int memman_alloc(MEMMAN *man, unsigned int size)
{
    unsigned int i, a;
    for (i = 0; i < man->frees; i++) {
        if (man->free[i].size >= size) {
            a = man->free[i].addr;
            man->free[i].addr += size;
            man->free[i].size -= size;
            if (man->free[i].size == 0) {   // 剩余空间为0，则删除该条信息
                man->frees--;
                for (; i < man->frees; i++) {
                    man->free[i] = man->free[i + 1];    // 内存管理列表中后面的信息全部前移一位
                }
            }
            return a;
        }
    }
    return NULL;   // 无可分配空间
}

/* 释放内存 */
int memman_free(MEMMAN *man, unsigned int addr, unsigned int size)
{
    int i, j;

    /* 为便于归纳内存，将free[]按照addr的顺序排列 */
    /* 所以，先决定应该放在哪里 */
    for (i = 0; i < man->frees; i++) {
        if (man->free[i].addr > addr) {
            break;
        }
    }
    // free[i-1].addr < addr < free[i].addr

    // 情况一
    if (i > 0) { // 前面有可用内存
        if (man->free[i-1].addr + man->free[i-1].size == addr) { // 可以和前面的可用内存归纳到一起
            man->free[i-1].size += size;
            if (i < man->frees) {   // 后面也有可用内存
                if (addr + size == man->free[i].addr) { // 也可以和后面的可用内存归纳到一起
                    man->free[i-1].size += man->free[i].size;
                    // 删除 man->free[i]
                    man->frees--;
                    for (; i < man->frees; i++) {
                        man->free[i] = man->free[i+1];  // 内存管理列表中后面的信息全部前移一位
                    }
                }
            }
            return 0;
        }
    }

    // 情况二：不能与前面的可用空间归纳到一起
    if (i < man->frees) { // 后面有可用内存
        if (addr + size == man->free[i].addr) { // 可以与后面的可用内存归纳到一起
            man->free[i].addr = addr;
            man->free[i].size += size;
            return 0;
        }
    }

    // 情况三：不能与前面的归纳到一起，也不能与后面的归纳到一起
    if (man->frees < MEMMAN_FREES) {
        // free[i]之后的全部后移一位
        for (j = man->frees; j > i; j--) {
            man->free[j] = man->free[j-1];
        }
        man->frees++;
        if (man->maxfrees < man->frees) {
            man->maxfrees = man->frees; //更新最大值
        }
        man->free[i].addr = addr;
        man->free[i].size = size;
        return 0;
    }

    // 内存释放失败，内存列表已满，直接放弃释放该空间，等列表空闲后再释放
    man->losts++;
    man->lostsize += size;
    return -1;
}

/* 以0x1000字节为单位分配空间 */
unsigned int memman_alloc_4k(MEMMAN *man, unsigned int size)
{
    unsigned int a;
    size = (size + 0xfff) & 0xfffff000; // 将地址按0x1000个字节为单位向上取整
    a = memman_alloc(man, size);
    return a;
}

/* 以0x1000字节为单位释放空间 */
int memman_free_4k(MEMMAN *man, unsigned int addr, unsigned int size)
{
    int i;
    size = (size + 0xfff) & 0xfffff000; // 将地址按0x1000个字节为单位向上取整
    i = memman_free(man, addr, size);
    return i;
}