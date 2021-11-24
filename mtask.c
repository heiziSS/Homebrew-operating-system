#include "bootpack.h"
#include <stdio.h>
TASKCTL *g_taskCtl;
TIMER *g_taskTimer;

TASK *task_now(void)
{
    return g_taskCtl->curTask;
}

/* 获取下一个任务 */
static TASK *get_next_task(TASK *t)
{
    if ((t == NULL) || (t->next == NULL)) {
        return g_taskCtl->levels[g_taskCtl->curLevel].tasksHead.next; // 切到第一个任务
    }
    return t->next; 
}

/* task_level切换 */
static void tasklevel_switch(void)
{
    int i;
    // 寻找最上层的LEVEL
    for (i = 0; i < MAX_TASKLEVELS; i++) {
        if (g_taskCtl->levels[i].taskNum > 0) {
            g_taskCtl->curLevel = i;
            break;
        }
    }
    return;
}

/* 向task_level中添加任务 */
static void task_add(TASK *task)
{
    TASKLEVEL *tl = &g_taskCtl->levels[task->level];
    if ((task->status != TASK_RUNNING) && (tl->taskNum < MAX_TASKS_PER_LEVEL)) {
        task->status = TASK_RUNNING;
        tl->pTasksTail->next = task;
        tl->pTasksTail = task;
        tl->taskNum++;
        tasklevel_switch();
    }
    return;
}

/* 从task_level中删除任务 */
static void task_remove(TASK *task)
{
    TASKLEVEL *tl = &g_taskCtl->levels[task->level];
    TASK *pret = &tl->tasksHead;
    TASK *t = pret->next;

    // 寻找task的位置
    while ((t != NULL) && (t != task)) {
        pret = t;
        t = t->next;
    }
    if (t == NULL) {
        return;
    }

    //删除任务
    pret->next = t->next;
    t->next = NULL;
    t->status = TASK_ALLOC;
    tl->taskNum--;
    tasklevel_switch();

    //删除的是尾节点
    if (tl->pTasksTail == t) {
        tl->pTasksTail = pret;
    }

    return;
}

/* idle线程，用于操作系统中无线程运行时 */
static void task_idle(void)
{
    for (;;) {
        io_hlt();
    }
}

/*
    初始化任务管理列表，同时申请一个默认任务
*/
TASK *task_init(MEMMAN *memman)
{
    int i;
    TASK *task, *idle;
    SEGMENT_DESCRIPTOR *gdt = (SEGMENT_DESCRIPTOR *) ADR_GDT;
    g_taskCtl = (TASKCTL *)memman_alloc_4k(memman, sizeof(TASKCTL));
    // 初始化TASK列表
    for (i = 0; i < MAX_TASKS; i++) {
        g_taskCtl->tasks[i].status = TASK_NOTUSE;
        g_taskCtl->tasks[i].sel = (TASK_GDT0 + i) * 8;
        set_segmdesc(gdt + TASK_GDT0 + i, 103, (int) &g_taskCtl->tasks[i].tss, AR_TSS32);
    }
    // 初始化TASKLEVEL列表
    for (i = 0; i < MAX_TASKLEVELS; i++) {
        g_taskCtl->levels[i].taskNum = 0;
        g_taskCtl->levels[i].pTasksTail = &g_taskCtl->levels[i].tasksHead;
        g_taskCtl->levels[i].pTasksTail->next = NULL;
    }
    task = task_alloc();
    task->priority = 2; // 0.02秒
    task->level = 0; //最高level
    task_add(task);
    load_tr(task->sel);
    g_taskTimer = timer_alloc();
    timer_settime(g_taskTimer, task->priority);
    g_taskCtl->curTask = task;

    // 设置idle线程
    idle = task_alloc();
    idle->tss.esp = memman_alloc_4k(memman, 64 * 1024) + 64 * 1024;
    idle->tss.eip = (int) &task_idle;
    idle->tss.es = 1 * 8;
    idle->tss.cs = 2 * 8;
    idle->tss.ss = 1 * 8;
    idle->tss.ds = 1 * 8;
    idle->tss.fs = 1 * 8;
    idle->tss.gs = 1 * 8;
    task_run(idle, MAX_TASKLEVELS - 1, 1);

    return task;
}

/* 任务申请 */
TASK *task_alloc(void)
{
    int i;
    TASK *task;
    for (i = 0; i < MAX_TASKS; i++) {
        if (g_taskCtl->tasks[i].status == TASK_NOTUSE) {
            task = &g_taskCtl->tasks[i];
            task->status = TASK_ALLOC;
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

/*
    激活已申请的任务
    task：需要激活的任务
    level：设定的level，小于0则不改变
    priority：设定的priority，0则使用默认值
*/
void task_run(TASK *task, int level, int priority)
{
    if (level < 0) {
        level = task->level;
    }

    if (priority > 0) {
        task->priority = priority;
    }

    if ((task->status == TASK_RUNNING) && (task->level != level)) {
        task_remove(task);   // 改变任务的level，需先删除原level中注册的task
    }

    if (task->status != TASK_RUNNING) {
        task->level = level;
        task_add(task); // 从休眠状态唤醒
    }

    return;
}

/* 任务切换 */
void task_switch(void)
{
    TASK *lastTask = g_taskCtl->curTask;

    // 寻找下一个任务
    g_taskCtl->curTask = get_next_task(lastTask);

    // 时间设置必须放在farjmp前面，若定时器未设置完就切换线程会宕机
    timer_settime(g_taskTimer, g_taskCtl->curTask->priority);

    // 当只有一个任务时执行farjmp命令，CPU会拒绝执行，导致程序运行混乱
    if (lastTask != g_taskCtl->curTask) {
        farjmp(0, g_taskCtl->curTask->sel);
    }

    return;
}

/* 
    任务休眠：当某个任务比较空闲，则可以休眠该任务，从而保证更多的资源分配给其他繁忙任务
*/
void task_sleep(TASK *task)
{
    if (task->status != TASK_RUNNING) {
        return;     //该任务不处于活动状态则直接返回
    }

    // 删除任务
    task_remove(task);

    if (task == g_taskCtl->curTask) { //休眠的任务是当前的任务，则需要进行任务切换
        g_taskCtl->curTask = get_next_task(g_taskCtl->curTask);
        farjmp(0, g_taskCtl->curTask->sel);
    }

    return;
}
