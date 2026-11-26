# Lab5 联调与 C 部分交接

尚文轩（2412456），2026-11-27。基于共同分支 `lab5` 的 `6ec1964`，包含国峻赫的内存与 trap 基础、陈铭宇的 ELF、exec 与 wait。

## 交付内容

| 工作 | 主要入口 |
| --- | --- |
| 调度和上下文切换 | `kern/schedule/sched.c`、`kern/process/switch.S`、`proc_run()` |
| 功能三，进程复制 | `do_fork()` → `copy_mm()` → `dup_mmap()` → `copy_range()`，代码标记为 `2412456` |
| 功能四，进程控制 | `do_exit()`、`do_yield()`、`do_kill()`，回收接口沿用陈铭宇的 `do_wait()` |
| Challenge 1，COW | `PTE_COW`、`resolve_cow_write()`、用户 store fault 和内核写回；普通 fork 默认 COW |
| 联合回归修复 | 评分构建收尾进入 `readline`；SLOB 整页归还；First-Fit 尾部插入只进行一次 |

实现分析见 [调度、复制与生命周期](../parts/c-fork-lifecycle.md)、[COW 与状态转换](../parts/c-copy-on-write.md) 和 [规格及迭代](../prompts/c-lab5.md)。第二项 Challenge 的分析已在[陈铭宇的第 8 节](../parts/b-user-loader.md#8-challenge-2用户程序何时进入内存)。`hello` 的 ELF 入口和随镜像嵌入的符号见 [ELF 记录](../logs/lab5-c/hello-elf.log)。

## 验证与复现

在仓库根目录并激活各自工具链，从干净构建开始。尚文轩的 WSL 使用 GCC 10.2、QEMU 6.2、OpenSBI 1.3、默认 128 MiB。[工具版本与代码版本](../logs/lab5-c/environment.log)

```bash
make clean
make -j4
make qemu
# 结束 QEMU 后运行专项测试
python3 tools/check-lab5-c.py --log-dir /tmp/lab5-c-regression
```

- [联合测试日志](../logs/lab5-c/suite.log)记录 COW 与逐页复制各八项通过。[spin](../logs/lab5-c/cow-spin.log)和 [waitkill](../logs/lab5-c/cow-waitkill.log)分别覆盖忙循环被抢占，以及 kill 唤醒处于子进程等待状态的目标。[默认启动日志](../logs/lab5-c/hello-qemu.log)可见 `hello pass.` 和 `init check memory pass.`。
- [COW 专项日志](../logs/lab5-c/cow-c_lifecycle.log)和[逐页复制专项日志](../logs/lab5-c/eager-c_lifecycle.log)记录了真实耗尽下的回滚、共享 `mm`、物理页引用、写隔离、跨页退出码、重复等待和孤儿接管。两次专项的空闲页均恢复到 31548。该页数属于包含测试程序和内核侧断言的隔离构建。
- [陈铭宇的隔离用例复测](../logs/b-isolated-20261127-012904/qemu.log)通过，覆盖 15 类非法 ELF、268 个分配失败位置、地址空间替换及 `wait`。该工具继续在临时副本加载测试替身，真实用户态执行以联合测试为准。
- 默认启用 COW。需手工切换至逐页复制时，先执行 `make clean`，再用 `make DEFS=-DLAB5_EAGER_FORK`，并带相同 `DEFS` 运行 `make qemu`。专项脚本会分别建立两套隔离副本。

教师评分在能直接采集串口的环境运行 `make grade`。尚文轩的 OpenSBI 1.3 环境使用下列命令，适配器仅为各 QEMU 进程分配不同的本机 GDB 端口，并将 GDB 转发的控制台输出交给教师脚本。

```bash
make grade QEMU=./tools/c-grade-qemu GDB=./tools/c-grade-gdb
```

本轮教师脚本的 13 个用户程序及其注入检查全部通过，初测和复测均为 **130/130**。[评分日志](../logs/lab5-c/grade.log) · [复测日志](../logs/lab5-c/grade-confirm.log)

评分命令会清理普通镜像。客体侧证据可在评分后重新 `make`，终端一运行 `make debug`，终端二运行以下命令。

```bash
riscv64-unknown-elf-gdb -q -nx -batch -x tools/lab5-guest-roundtrip.gdb
```

客体 GDB 在 `hello` 的首条指令 `0x800020` 停住。首次 `ecall` 的 `scause=8`，`sepc=0x8000aa`。分发时保存的返回地址已推进到 `0x8000ae`，命中 `sret` 后也在该用户地址再次停住，`a0=0`。宿主 QEMU 的 TCG、异常入口与 `sret` 实现还需用带匹配源码和符号的 QEMU 补齐。[客体 GDB 日志](../logs/lab5-c/guest-gdb.log)

| 本地提交 | 内容 |
| --- | --- |
| `f37c281` | 调度和上下文切换 |
| `c3c0cc9`、`eef1238` | fork、退出、让出和终止 |
| `2b3f5e9`、`c89f7e2`、`77c265c` | 评分停点与空闲页回收修复 |
| `06c3503` | COW 映射、写缺页、内核写回 |
| `d90492e` | 轮转时间片与恢复用户态前的退出检查 |
| `a9bb8a1` | 专项测试、评分适配与客体 GDB |

## 合入后的分工

- **国峻赫**在 QEMU 4.1.1 环境确认普通评分与 COW 分支，补齐宿主 QEMU 和客体 GDB 的同时观察。可从本次客体断点和真实 `ecall` 现场开始，对照 QEMU 的译码及特权状态更新。
- **陈铭宇**把功能一至四、Challenge 1、已有的 Challenge 2 和调试观察汇入 Lab5 总报告。当前仓库根目录 `report.md` 是 Lab4 报告，可用本页和上述证据作为 Lab5 汇总索引。
- **尚文轩**同步个人提交后，参与核对共同版本的运行、评分、报告链接和三位同学的源码学号。群内和远端发布由组员按实际验收版本操作。
