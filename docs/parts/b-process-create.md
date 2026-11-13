# B：内核线程创建与初始化（第一阶段记录）

负责人：陈铭宇，2412559。日期：2026-11-13。

状态：六个必做函数及独立创建验证已完成；首次执行、调度联调和完整评分待第二阶段。
基线：`e49d3c12c8fa4fea533167e58f32748c778052d8`，分支 `lab4`。代码与测试证据已提交为 `ebe4c84`。本文件记录该版本的第一阶段结果。
正式 `proc.c` 的 SHA-256：`6e551db627c75adb42a3315ed620927e4a81a64ce01a050ba7f9a7319dea3731`。

## 实现范围

正式内核仅修改 `kern/process/proc.c` 的 `LAB4 function2 2412559` 区域，保留 C 的 function3 空位。
没有修改 A 的 PMM、公共头文件、Makefile、教师测试或评分脚本。新增工具位于 `tools/`，不会被普通 Makefile 自动编入内核。

| 函数 | 实现 |
| --- | --- |
| alloc_proc | kmalloc 分配 PCB；先清零，再设置 PROC_UNINIT、PID=-1、boot_pgdir_pa 和两个自环链表节点。 |
| copy_mm | 断言当前和子线程 mm 均为空，返回成功；本阶段无用户地址空间复制。 |
| copy_thread | 在两页内核栈顶部放置 288 字节 trapframe；复制模板，设置 a0=0；stack=0 时 sp 指向 trapframe，否则使用指定 stack；context.ra=forkret，context.sp=tf。 |
| do_fork | 检查 MAX_PROCESS，分配 PCB/栈，处理 mm，构建现场；完成后分配 PID，加入哈希表和链表、增加计数、wakeup_proc；失败按相反顺序清理。 |
| kernel_thread | 清零临时 trapframe，s0=函数，s1=参数，epc=kernel_thread_entry；设置 SPP/SPIE 并清 SIE，传 CLONE_VM 调 do_fork。 |
| proc_init | 初始化链表/哈希表；idle PID=0、启动栈、need_resched=1，设置 current；创建 PID=1、名字 init 的 initproc。 |

当前单 hart 教学环境中，do_fork 的整个创建过程保存并关闭本地 S 中断，成功和失败均按进入时状态恢复。这样数量检查、父线程、PID 分配、登记和唤醒保持一致。分配器为非阻塞接口并支持嵌套保存中断。本实现未提供多核并发锁。

`set_proc_name()` 是框架已有实现，固定 memcpy 15 字节；本次传入 16 字节的名字数组，避免短字符串字面量被固定长度读取。没有改动公共辅助函数。

## 与 C 的接口

- idleproc 不加入 proc_list 或 PID 哈希表，nr_process 计数包含 idle；find_proc(0) 按现有接口返回 NULL。
- 普通线程用 list_add_before 追加到 proc_list 尾部；C 应按链表扫描，找不到 runnable 时使用 idle 兜底。
- 每个新线程独占 KSTACKPAGE=2 页，mm=NULL，共享物理页表地址 boot_pgdir_pa。pgdir 不是虚拟地址，也不是已编码的 SATP。
- context 为 ra、sp、s0-s11，共 112 字节；trapframe 为 36 个机器字，共 288 字节。
- 首次运行预期为 switch_to → forkret → forkrets → __trapret → sret → kernel_thread_entry → 线程函数。
- 此链路目前只验证了内存中的现场与符号地址，尚未实际执行。
- do_fork 成功返回正 PID；数量上限返回 -E_NO_FREE_PROC；PCB 或栈不足返回 -E_NO_MEM；copy_mm 返回的错误原样传播。

## 已执行验证

环境：WSL Ubuntu；GCC 10.2.0，GDB 10.1，QEMU 4.1.1，-O2 -g，RV64，单 hart；时区 Asia/Shanghai。运行时固件版本见 QEMU 原始日志。

实际命令：

```bash
wsl.exe -d Ubuntu -- env -u QEMU TZ=Asia/Shanghai PATH=/mnt/d/3nd_grade/OS/environment/riscv-toolchain/bin:/mnt/d/3nd_grade/OS/environment/qemu/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin python3 /mnt/d/3nd_grade/OS/lab/lab4/github/tools/b-create-check.py
```

脚本先在正式树执行 make clean、make -j2，再复制到全新 /tmp 目录执行独立构建及 QEMU/GDB。详细 QEMU 命令和源文件哈希保存在 result.json。

| 检查 | 预期与实际 |
| --- | --- |
| 正式 make -j2 | B 的 proc.c 编译通过；整体链接失败，undefined reference to cpu_idle，退出码 2。C 尚未完成，不能记为整体构建通过。 |
| 临时副本 make -j2 | 退出码 0。 |
| 教师 test_lab4_process_create | 原文件原断言，PASS；没有调用评分流程，没有获得正式分数。 |
| PCB 初始化 | 分配返回区先填 0xa5，检查整个结构及链表，PASS。 |
| 16 个新线程 | PID 唯一、父指针、独立栈、首次帧、链表/哈希一致性，PASS。 |
| 指定 stack 的 do_fork | 子线程 a0=0、指定 sp、源模板未被覆盖，PASS；未实际使用该合成栈执行。 |
| PCB/栈/mm 失败注入 | 各 64 次；计数、登记状态、活跃 PCB/栈、空闲页数恢复，PASS。 |
| 真实物理页耗尽 | 逐页保留所有剩余空闲页，真实触发内核栈分配失败，再释放保留页，PASS。 |
| 数量上限 | 临时将计数设为 MAX_PROCESS，确认在分配前返回 -E_NO_FREE_PROC，然后恢复计数，PASS。没有实际创建 4096 个线程。 |
| 中断状态 | 进入 SIE=0/1 的创建成功/失败均保持进入时状态，PASS；未设置定时器或执行调度。 |
| GDB | proc_init 返回后的断点观察与地址检查 PASS，退出码 0。 |
| QEMU | 输出 ALL PASS 后测试驱动调用 SBI 关机，实际退出码 0。不是正式启动路径自然退出的证据。 |

失败注入、计数上限和测试清理均只发生在临时副本。copy_mm 正式实现本阶段不会返回分配错误，mm 失败注入用于验证 do_fork 预留的 bad_kstack 回滚分支。SLOB 可能缓存空闲块；同时检查 PCB 分配/释放配对和栈页数，避免仅凭空闲物理页数就断言没有 PCB 泄漏。

测试桩对 schedule、proc_run、switch_to、cpu_idle 及中断/异常处理均设置 panic，任何意外调用都会失败；现有 forkrets 和未完成恢复代码未执行。所有测试修改保存在 isolation.patch，正式源文件测试前后 SHA-256 一致，教师测试副本与原文件 SHA-256 一致。

编译警告如实保留：现有 trap 缺少处理函数声明，kmalloc.c 的指针转换，以及教师调度测试中 void 返回函数指针与 kernel_thread 的 int 返回类型不匹配；未修改教师测试来消除警告。B 的正式六函数没有新增编译警告。

## GDB 实测摘要

以下地址来自本次隔离 ELF，不是正式整合内核的固定地址：

| 项目 | 实测值 |
| --- | --- |
| current = idleproc | 0xffffffffc0410010 |
| initproc | 0xffffffffc0410110 |
| nr_process | 2 |
| sizeof(proc_struct/context/trapframe) | 232 / 112 / 288 字节 |
| initproc 内核栈底 | 0xffffffffc0412000 |
| initproc tf = context.sp | 0xffffffffc0413ee0 |
| context.ra = forkret | 0xffffffffc0202fb0 |
| tf.epc = kernel_thread_entry | 0xffffffffc0202f8c |
| pgdir = boot_pgdir_pa | 0x8020a000 |
| tf.status | 0x8000000000046120，SPP=1、SPIE=1、SIE=0 |

证据目录：`../logs/b-create-20261113-094247/`。
主要证据为 result.json、isolated-qemu.log、gdb-session.log、gdb-commands.txt、normal-build.log、isolated-build.log、isolation.patch。GDB 证据是原始文本记录，本阶段未制作截图。isolated-kernel.elf 为本机辅助调试产物，受 .gitignore 忽略；可用测试脚本重建。

## 尚未验证和下一阶段

等待 C 完成 cpu_idle、schedule、proc_run、switch_to、trap 恢复与 C 调用，以及 RV64/Sv39 SATP 路径。随后验证 init_main 的真实执行、函数参数、多线程反复切换、时钟运行，最后运行正式 make / make qemu / make grade。不能保留 Lab3 无条件 1000 ticks 关机。

未验证 PID 用尽后的长期回绕、多核、真正用户 mm 复制、完整退出/资源回收。保留框架 do_exit 的占位 panic，首次运行输出后需核对调用路径。现有创建检查通过不代表调度、正式评分或完整报告完成。

报告后续补充 OS 原理对应分析和联合回归；本文件仅记录第一阶段真实进度。
