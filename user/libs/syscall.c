#include <defs.h>
#include <unistd.h>
#include <syscall.h>

/*
 * 本实验的系统调用约定：
 *
 * a0：系统调用编号，同时保存返回值
 * a1～a5：最多五个参数
 *
 * 所有参数由调用者明确提供，未使用的参数填 0。
 */
static inline int
syscall5(uint64_t num,
         uint64_t arg1,
         uint64_t arg2,
         uint64_t arg3,
         uint64_t arg4,
         uint64_t arg5)
{
    register uint64_t a0 asm("a0") = num;
    register uint64_t a1 asm("a1") = arg1;
    register uint64_t a2 asm("a2") = arg2;
    register uint64_t a3 asm("a3") = arg3;
    register uint64_t a4 asm("a4") = arg4;
    register uint64_t a5 asm("a5") = arg5;

    /*
     * 系统调用入口保存用户寄存器，并将返回值放入 a0。
     * memory 表示编译器不能随意将内存访问跨越 ecall 重排。
     */
    asm volatile (
        "ecall"
        : "+r" (a0)
        : "r" (a1), "r" (a2), "r" (a3),
          "r" (a4), "r" (a5)
        : "memory"
    );

    return (int)a0;
}

int
sys_exit(int64_t error_code)
{
    return syscall5(SYS_exit, (uint64_t)error_code, 0, 0, 0, 0);
}

int
sys_fork(void)
{
    return syscall5(SYS_fork, 0, 0, 0, 0, 0);
}

int
sys_wait(int64_t pid, int *store)
{
    return syscall5(SYS_wait,
                    (uint64_t)pid,
                    (uint64_t)(uintptr_t)store,
                    0, 0, 0);
}

int
sys_yield(void)
{
    return syscall5(SYS_yield, 0, 0, 0, 0, 0);
}

int
sys_kill(int64_t pid)
{
    return syscall5(SYS_kill, (uint64_t)pid, 0, 0, 0, 0);
}

int
sys_getpid(void)
{
    return syscall5(SYS_getpid, 0, 0, 0, 0, 0);
}

int
sys_putc(int64_t c)
{
    return syscall5(SYS_putc, (uint64_t)c, 0, 0, 0, 0);
}

int
sys_pgdir(void)
{
    return syscall5(SYS_pgdir, 0, 0, 0, 0, 0);
}
