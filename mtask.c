#include "bootpack.h"

TASKCTL *gTaskCtl;
TIMER *gTaskTimer;

enum {
    TASK_NOTUSE,
    TASK_ALLOC,
    TASK_RUNNING,
    TASK_MAX,
};

/*
    初始化任务管理列表，同时申请一个默认任务
*/
TASK *task_init(MEMMAN *memman)
{
    int i;
    TASK *task;
    SEGMENT_DESCRIPTOR *gdt = (SEGMENT_DESCRIPTOR *) ADR_GDT;
    gTaskCtl = (TASKCTL *)memman_alloc_4k(memman, sizeof(TASKCTL));
    for (i = 0; i < MAX_TASKS; i++) {
        gTaskCtl->tasks[i].flags = TASK_NOTUSE;
        gTaskCtl->tasks[i].sel = (TASK_GDT0 + i) * 8;
        set_segmdesc(gdt + TASK_GDT0 + i, 103, (int) &gTaskCtl->tasks[i].tss, AR_TSS32);
    }
    task = task_alloc();
    task->flags = TASK_RUNNING;
    load_tr(task->sel);
    gTaskCtl->runningtasksHead.next = task;
    gTaskCtl->pCurTask = task;
    gTaskCtl->pTask = task;
    gTaskTimer = timer_alloc();
    timer_settime(gTaskTimer, 2);
    return task;
}

/* 任务申请 */
TASK *task_alloc(void)
{
    int i;
    TASK *task;
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTaskCtl->tasks[i].flags == TASK_NOTUSE) {
            task = &gTaskCtl->tasks[i];
            task->flags = TASK_ALLOC;
            task->tss.eflags = 0x00000202;  // IF = 1
            task->tss.eax = 0;
            task->tss.ecx = 0;
            task->tss.edx = 0;
            task->tss.ebx = 0;
            task->tss.ebp = 0;
            task->tss.esi = 0;
            task->tss.edi = 0;
            task->tss.es  = 0;
            task->tss.ds  = 0;
            task->tss.fs  = 0;
            task->tss.gs  = 0;
            task->tss.ldtr = 0;
            task->tss.iomap = 0x40000000;
            task->next = NULL;
            return task;
        }
    }
    return NULL;
}

/* 激活已申请的任务 */
void task_run(TASK *task)
{
    task->flags = TASK_RUNNING;
    gTaskCtl->pTask->next = task;
    gTaskCtl->pTask = task;
    return;
}

/* 任务切换 */
void task_switch(void)
{
    timer_settime(gTaskTimer, 2);
    if (gTaskCtl->pCurTask->next != NULL) { //下一个任务不为空则切到下个任务
        gTaskCtl->pCurTask = gTaskCtl->pCurTask->next;
        farjmp(0, gTaskCtl->pCurTask->sel);
    } else if (gTaskCtl->pCurTask != gTaskCtl->runningtasksHead.next) {  // 下一个任务为空则切到第一个任务
        gTaskCtl->pCurTask = gTaskCtl->runningtasksHead.next;
        farjmp(0, gTaskCtl->pCurTask->sel);
    }
    return;
}
