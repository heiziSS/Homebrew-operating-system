#include "bootpack.h"

TASKCTL *gTaskCtl;
TIMER *gTaskTimer;

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
    task->priority = 2; // 0.02秒
    load_tr(task->sel);
    gTaskCtl->runningtasksHead.next = task;
    gTaskCtl->pCurTask = task;
    gTaskCtl->pTask = task;
    gTaskTimer = timer_alloc();
    timer_settime(gTaskTimer, task->priority);
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
void task_run(TASK *task, int priority)
{
    if (priority > 0) {
        task->priority = priority;
    }
    if (task->flags != TASK_RUNNING) {
        task->flags = TASK_RUNNING;
        gTaskCtl->pTask->next = task;
        gTaskCtl->pTask = task;
    }
    return;
}

/* 任务切换 */
void task_switch(void)
{
    TASK *task = gTaskCtl->pCurTask;
    if (gTaskCtl->pCurTask->next != NULL) { //下一个任务不为空则切到下个任务
        gTaskCtl->pCurTask = gTaskCtl->pCurTask->next;   
    } else if (gTaskCtl->pCurTask != gTaskCtl->runningtasksHead.next) {  // 下一个任务为空则切到第一个任务
        gTaskCtl->pCurTask = gTaskCtl->runningtasksHead.next;
    }

    // 时间设置必须放在farjmp前面，若定时器未设置完就切换线程会宕机
    timer_settime(gTaskTimer, gTaskCtl->pCurTask->priority);

    // 当只有一个任务时执行farjmp命令，CPU会拒绝执行，导致程序运行混乱
    if (task != gTaskCtl->pCurTask) {
        farjmp(0, gTaskCtl->pCurTask->sel);
    }

    return;
}

/* 
    任务休眠：当某个任务比较空闲，则可以休眠该任务，从而保证更多的资源分配给其他繁忙任务
*/
void task_sleep(TASK *task)
{
    TASK *pret, *t;
    if (task->flags != TASK_RUNNING) {
        return;     //该任务不处于活动状态则直接返回
    }

    // 在运行列表中寻找task的位置
    pret = &gTaskCtl->runningtasksHead;
    t = gTaskCtl->runningtasksHead.next;
    while (t != NULL && t != task) {
        pret = t;
        t = t->next;
    }

    if (t == NULL) {    //未找到该任务
        return;
    }

    // 将需要休眠的任务从运行列表中删除
    pret->next = t->next;
    t->next = NULL;
    t->flags = TASK_ALLOC;  // 不工作状态

    if (t == gTaskCtl->pCurTask) { //休眠的任务是当前的任务，则需要进行任务切换
        if (pret->next != NULL) {
            gTaskCtl->pCurTask = pret->next;
        } else {
            gTaskCtl->pCurTask = gTaskCtl->runningtasksHead.next;
            gTaskCtl->pTask = pret; //最后一个任务被删除，链表尾指针前移
        }
        farjmp(0, gTaskCtl->pCurTask->sel);
    }

    return;
}