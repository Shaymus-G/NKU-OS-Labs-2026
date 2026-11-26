#include <defs.h>
#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <console.h>
#include <error.h>

/*
 * 系统调用包装函数。
 *
 * arg[0] 对应用户态 a1；
 * arg[1] 对应用户态 a2；
 * 以此类推。
 */

static int
sys_exit(uint64_t arg[])
{
    return do_exit((int)arg[0]);
}

static int
sys_fork(uint64_t arg[])
{
    /*
     * 普通 fork 不指定 CLONE_VM；
     * stack 为 0，实际子进程现场由 do_fork/copy_thread 处理。
     */
    return do_fork(0, 0, current->tf);
}

static int
sys_wait(uint64_t arg[])
{
    int pid = (int)arg[0];
    int *store = (int *)(uintptr_t)arg[1];

    /*
     * 用户指针是否合法、退出码如何复制回用户空间，
     * 需要 do_wait 按 B 的实现完成检查。
     */
    return do_wait(pid, store);
}

static int
sys_exec(uint64_t arg[])
{
    const char *name = (const char *)(uintptr_t)arg[0];
    size_t len = (size_t)arg[1];
    unsigned char *binary = (unsigned char *)(uintptr_t)arg[2];
    size_t size = (size_t)arg[3];

    /*
     * 用户地址及程序映像参数的检查和装载由 do_execve 负责。
     * 后续与 B 确认 kernel_execve 的参数顺序一致。
     */
    return do_execve(name, len, binary, size);
}

static int
sys_yield(uint64_t arg[])
{
    return do_yield();
}

static int
sys_kill(uint64_t arg[])
{
    return do_kill((int)arg[0]);
}

static int
sys_getpid(uint64_t arg[])
{
    return current->pid;
}

static int
sys_putc(uint64_t arg[])
{
    cons_putc((int)arg[0]);
    return 0;
}

static int
sys_pgdir(uint64_t arg[])
{
    print_pgdir();
    return 0;
}

/*
 * 系统调用分发表。
 *
 * 这里使用指定下标初始化，因此编号未使用的位置为 NULL。
 */
static int (*syscalls[])(uint64_t arg[]) = {
    [SYS_exit]   = sys_exit,
    [SYS_fork]   = sys_fork,
    [SYS_wait]   = sys_wait,
    [SYS_exec]   = sys_exec,
    [SYS_yield]  = sys_yield,
    [SYS_kill]   = sys_kill,
    [SYS_getpid] = sys_getpid,
    [SYS_putc]   = sys_putc,
    [SYS_pgdir]  = sys_pgdir,
};

#define NUM_SYSCALLS (sizeof(syscalls) / sizeof(syscalls[0]))

// lab5 function1 2412082
void
syscall(void)
{
    struct trapframe *tf = current->tf;

    uintptr_t num = tf->gpr.a0;

    uint64_t args[5] = {
        tf->gpr.a1,
        tf->gpr.a2,
        tf->gpr.a3,
        tf->gpr.a4,
        tf->gpr.a5,
    };

    if (num < NUM_SYSCALLS && syscalls[num] != NULL) {
        tf->gpr.a0 = syscalls[num](args);
    } else {
        /*
         * 对非法编号返回 -E_INVAL。
         * 这与当前老师提供的 test_lab5_syscall() 预期一致。
         */
        tf->gpr.a0 = -E_INVAL;
    }
}
