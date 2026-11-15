# Lab4 A 部分阶段交接

A 已完成物理内存初始化、Best-Fit 接入及 Sv39 页映射，实现提交为 `f66f380`。B、C 可以基于该提交继续接入进程创建和调度。

**已验证：** 在独立工作树中用 RISC-V QEMU 运行到 `vmm_init()` 返回，PMM、页表、`kmalloc` 和 VMA 的框架自检均通过。证据见 [a-isolated-qemu.log](../logs/a-isolated-qemu.log)。用于隔离验证的临时启动改动仅在独立工作树中，正式启动流程未改动。

**待联调：** B 接入 `proc_init()` 和内核线程创建，C 补齐 `cpu_idle()`、trap 和调度。合入共享 `lab4` 分支后，先执行 `make qemu`，确认内存自检通过且 `initproc` 实际运行；退出 QEMU 后执行 `make grade`，保存评分结果。目前只有 A 的独立验证结果，尚未进行整组验收。
