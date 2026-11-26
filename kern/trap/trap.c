#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <clock.h>
#include <trap.h>
#include <riscv.h>
#include <stdio.h>
#include <assert.h>
#include <console.h>
#include <vmm.h>
#include <kdebug.h>
#include <unistd.h>
#include <syscall.h>
#include <error.h>
#include <sched.h>
#include <sync.h>
#include <sbi.h>

#define TICK_NUM 100

static void print_ticks(void)
{
    cprintf("%d ticks\n", TICK_NUM);
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S */
void idt_init(void)
{
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
     * presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
    /* Allow kernel to access user memory */
    set_csr(sstatus, SSTATUS_SUM);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf)
{
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf)
{
    cprintf("trapframe at %p\n", tf);
    // cprintf("trapframe at 0x%x\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  tval 0x%08x\n", tf->tval);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr)
{
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

extern struct mm_struct *check_mm_struct;

/*
 * Lab3 中断处理适配到 Lab5。
 *
 * scause 最高位表示中断，剩余位表示中断编号。
 */
void
interrupt_handler(struct trapframe *tf)
{
    const uintptr_t interrupt_bit =
        (uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1);

    uintptr_t cause = tf->cause & ~interrupt_bit;

    switch (cause) {
    case IRQ_S_TIMER:
        /*
         * 时钟中断后重新设置下一次时钟事件。
         */
        clock_set_next_event();
        ticks++;

        if (ticks % TICK_NUM == 0) {
            print_ticks();
        }

        /*
         * 请求在适当时机重新调度。
         *
         * trap() 会在返回用户态前检查 need_resched；
         * 内核态的后续调度时机还需要结合 C 的调度实现验证。
         */
        if (current != NULL) {
            current->need_resched = 1;
        }
        break;

    case IRQ_S_SOFT:
        /*
         * 如果后续测试使用 S 态软件中断，需要清除对应 pending 位。
         */
        clear_csr(sip, MIP_SSIP);
        break;

    case IRQ_S_EXT:
        /*
         * 保留外部中断入口。具体设备中断的识别与应答依赖当前平台
         * 和中断控制器实现，不能仅凭本分支认定外部中断已经完成。
         */
        cprintf("Supervisor external interrupt\n");
        break;

    default:
        cprintf("Unhandled interrupt: cause %lu\n",
                (unsigned long)cause);
        print_trapframe(tf);
        break;
    }
}

/*
 * Lab5 功能一：异常处理。
 *
 * 用户态 ecall 是 32 位指令，返回位置应前移 4 字节。
 */
// lab5 function1 2412082
void
exception_handler(struct trapframe *tf)
{
    switch (tf->cause) {
    case CAUSE_USER_ECALL:
        tf->epc += 4;
        syscall();
        return;
    case CAUSE_STORE_PAGE_FAULT:
        if (!trap_in_kernel(tf) && current != NULL && current->mm != NULL) {
            struct mm_struct *mm = current->mm;
            lock_mm(mm);
            int ret = resolve_cow_write(mm, tf->tval);
            unlock_mm(mm);
            if (ret == 0) return;  // Retry the same store at sepc.
        }
        // Fall through to the unhandled exception path.

    default:
        /*
         * 对未处理的异常，先输出发生异常时的现场。
         */
        print_trapframe(tf);

        if (trap_in_kernel(tf)) {
            panic("unhandled exception in kernel");
        }

        /*
         * 用户态异常不能一直返回到出错指令，否则会反复陷入。
         * 进程退出及资源释放由 C 的 do_exit 实现。
         */
        cprintf("user process %d killed by exception\n",
                current != NULL ? current->pid : -1);

        if (current != NULL) {
            do_exit(-E_KILLED);
        }

        panic("cannot handle user exception");
    }
}


static inline void trap_dispatch(struct trapframe *tf)
{
    if ((intptr_t)tf->cause < 0)
    {
        // interrupts
        interrupt_handler(tf);
    }
    else
    {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap() returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf)
{
    // dispatch based on what type of trap occurred
    //    cputs("some trap");
    if (current == NULL)
    {
        trap_dispatch(tf);
    }
    else
    {
        struct trapframe *otf = current->tf;
        current->tf = tf;

        bool in_kernel = trap_in_kernel(tf);

        trap_dispatch(tf);

        current->tf = otf;
        if (!in_kernel)
        {
            if (current->flags & PF_EXITING)
            {
                do_exit(-E_KILLED);
            }
            if (current->need_resched)
            {
                schedule();
            }
        }
    }
}
