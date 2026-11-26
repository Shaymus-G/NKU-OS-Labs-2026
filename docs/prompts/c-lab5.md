# Lab5 C 部分规格与交互记录

本页根据用户提出的 Lab5 目标、分工、实验手册和实际联调结果整理。四段式内容是复核后的功能规格。

## 调度、复制与进程控制

**Prompt**　完成 C 的调度和上下文切换，以及实验功能三、四。保留 A 的 trap/系统调用接口、B 的用户装载和等待实现，按函数分工接入。实际运行用户 ELF，验证父子返回值和资源回收。

**Rely**　`proc_struct`、`context`、`trapframe`、`set_links()`、`wakeup_proc()`、`copy_thread()`、`mm_struct`、VMA、`Page` 引用计数、`page_insert()/exit_mmap()`，以及单 hart 的 SIE 临界区。

**Guarantee**　交付 `proc_run/schedule/switch_to`、`do_fork/copy_mm/dup_mmap/copy_range` 和 `do_exit/do_yield/do_kill`，遵守 `do_wait()` 的退出码与最终回收约定。

**Specification**　新进程只有在 PCB、栈、页表和 trapframe 可用后才加入进程树。页分配失败清理已有资源。普通 fork 的子进程从零返回，父进程得到子 PID。退出进程交出 CPU，父进程读取退出码后清除 PCB；孤儿由 `initproc` 接管。以实际用户程序和页数比较检查这些后置条件。

## 写时复制与测试

**Prompt**　完成手册 COW 扩展并保留逐页复制构建，提供状态转换说明。用真实用户程序检查父子写入隔离，用内核侧测试观察页表、页引用和实际耗尽行为。

**Rely**　Sv39 `PTE_W`、RSW、`scause`、`tval`、`sepc`、页引用、`mm_lock`，以及 `wait` 的内核别名写回。

**Guarantee**　逐页复制与 COW 两种构建均可启动并通过生命周期回归。COW 覆盖共享、首次写入、单引用恢复、跨页写回、无内存和回收。

**Specification**　父页权限只在子映射完整后改变；只有可写 VMA 内已标 COW 的叶页可恢复写权限；硬件 store fault 成功分流后重试原指令；通过内核写回时先解析所有涉及的 COW 映射。测试检查真实耗尽后页数恢复，并保留未映射页与原生只读权限。

## 实际迭代

1. 用户明确要求手册基础项和 Challenge，并确认最新 `lab5` 已含 A/B 实现。核对本地 `6ec1964` 后按调度、复制、控制依次提交，首个 `hello` 实际运行并恢复内存。
2. 教师调试脚本在 `readline` 等候。在评分构建的成功回收处进入现有监视器，随后用客体 GDB 验证 `ecall` 分发和返回位置。
3. `forktest` 在用户层通过后仍触发空闲页比较。检查 SLOB 后补齐整页归还。真实耗尽复测进一步复现 First-Fit 尾部重复插入，修正边界后 COW 与逐页复制的两组专项测试均通过。
4. 本机 OpenSBI 在调试会话中把控制台字节交给 GDB。用本机转接保存原始字符，给各评分用例分配独立端口，再以固定四 tick 轮转时间片降低频繁切换带来的输出交错。实际复测包含抢占与原样教师评分。
