# Lab5 B 阶段报告：用户装载、exec 与 wait

> 2026-11-27 更新索引：本文保留阶段 0～4 的实现与隔离证据。阶段 5～6 已在集成版本完成，当前支持 COW、真实用户 exec 与 U→S→U，见 [B 联合验收](b-integration.md)及 [总报告](../../report.md)。下文未支持/待验证项均为当时状态，不覆盖后续结果。

负责人：陈铭宇，2412559。日期：2026-11-26，Asia/Shanghai。

本报告覆盖本次约定的阶段 0～4：接口说明、B 的实现及独立验证。基线为 lab5 分支 872551b；实现与测试证据提交为 `240b6b93c93f269fe2cb1235017f1894a07efe51`，本报告由后续文档提交补充，交付版本见 [B 交接文档](../notes/lab5-b-handover.md)。正式完整内核仍依赖 C 的缺失实现，本报告不包含真实用户态运行、完整 fork/exit 调度、教师评分或双重 GDB 通过结论。report.md 中的 Lab4 30/30 与本阶段无关。

## 1. 实现与职责

正式源码仅修改 kern/process/proc.c 和 kern/mm/pmm.c：

| 部分 | 本次实现 |
| --- | --- |
| 创建基础 | alloc_proc、kernel_thread、copy_thread、proc_init、cpu_idle；初始化 Lab5 新字段，保留父子链辅助函数的登记语义 |
| 用户启动 | init_main、user_main、kernel_execve；默认 hello，支持 TEST/TESTSTART/TESTSIZE；init 等待回收并保持存活 |
| ELF 与 exec | check_elf、load_icode、do_execve，以及受检地址复制/清理辅助函数 |
| 等待 | do_wait：指定/任意子进程、输出指针、睡眠重试、退出标志和最终回收 |
| 必要修正 | set_proc_name 避免读取短字符串结尾之外；exit_range 完整检查上级目录存活项 |

A 的 trap、系统调用、分配器及页映射保持原实现。C 的 copy_mm、do_fork、do_exit/do_yield/do_kill、copy_range、dup_mmap、schedule 和 switch_to 仍待实现。本次没有把测试桩写入这些正式位置。

详细交叉接口见 [阶段 0 接口说明](../notes/lab5-b-interfaces.md)。这份说明是 B 侧交付的契约，尚需 A/C 联合复核，没有声称已经收到队友确认。

## 2. 启动与首次现场

proc_init 创建 idle，再调用 kernel_thread 创建 init。idle 不进入普通进程链表/哈希，仍计入 nr_process；其余进程通过 C 的 do_fork 使用 set_links 登记。

copy_thread 将 288 字节的 trapframe 保留在子内核栈顶部；首次 context.ra 指向 forkret，context.sp 指向 tf。内核线程恢复到 kernel_thread_entry 时 SP=tf，后续 C 调用栈向下增长，因此顶部 tf 不会与 kernel_execve 的当前调用栈重叠。用户 fork 不指定新栈时继承原 SP，子 a0=0。

kernel_execve 在关中断状态下完成 do_execve，成功直接 forkrets(current->tf)，最终复用 A 的 __trapret/sret；失败恢复原 SIE 并返回错误。用户初始 tf 的 EPC=ELF e_entry，SP=USTACKTOP，status=SPIE。代码中已经连接成功路径，但本轮没有执行它进入 U 态。

## 3. ELF64 装载和原子替换

装载器接受 ELF64、小端、RISC-V、ET_EXEC 静态映像，先检查头及程序段表大小、段的文件范围和虚拟地址范围、溢出、filesz/memsz、对齐、权限和可执行入口。页边界取整后重叠的段被拒绝，避免原 mm_map 在重叠输入上触发断言。当前不支持动态链接、用户大页和 W-only 段。

对每个 PT_LOAD 建立 VMA，按 ELF 标志设置 U/R/W/X；先清零整个物理页，再复制该页对应的文件部分，因此页首、页尾和 BSS 都不会泄露旧内容。栈 VMA 大小为 USTACKSIZE，底部一页不映射，其余 255 页提前分配且无执行权限，不依赖尚未实现的按需缺页。

旧 mm 保持有效时构建完整的新 mm。任何失败释放新 mm、页表与已分配页面，旧 mm、tf、satp 和名称不变。成功才关中断提交 mm/tf/satp、刷新 TLB 与指令缓存，再递减旧 mm 的引用；只有最后引用消失才释放它。这也保证用户传入的 ELF 缓冲区不会在复制之前随旧 mm 销毁。

do_execve 接受 1..15 字节的名称。可信内核线程使用 KERN_ACCESS 范围的嵌入映像；用户调用只能使用自身 mm 中可读的名称与 ELF。B 的复制辅助函数同时检查 VMA 和实际叶页的 U/R/W 权限，然后通过物理页的内核别名访问，不依赖全局 SUM，也不直接解引用用户指针。

## 4. wait 与资源回收

本框架 pid=0 表示任意子进程；pid>0 表示指定子进程；负值、超范围和无匹配子进程返回 -E_BAD_PROC。成功返回 0，非空 code_store 接收退出码。输出在任何写入前进行完整跨页可写检查，非法地址不会导致部分写回或提前回收僵尸。

等待者在关中断区检查子进程并登记 PROC_SLEEPING/WT_CHILD，释放 mm_lock 后保持关中断调用 schedule；醒来恢复原 SIE，重新检查子进程和 PF_EXITING。测试覆盖伪唤醒后重新睡眠，以及被 kill 后转交 do_exit(-E_KILLED)。

C 的 do_exit 应负责用户 mm、退出码、僵尸状态、唤醒父进程、孤儿转交和离开 CPU，最终令 mm=NULL；B 在 wait 中移除哈希和父子链，再释放子进程内核栈与 PCB。不能由退出者释放仍在使用的自身内核栈。

## 5. 已实际执行的验证

正式模块强制重编译：`make -B obj/kern/process/proc.o obj/kern/mm/pmm.o`，返回 0，见 [模块日志](../logs/b-modules-final.log)。

最终隔离测试记录：

- [QEMU 原始日志](../logs/b-isolated-20261126-211915/qemu.log)
- [完整隔离构建日志](../logs/b-isolated-20261126-211915/build.log)
- [基线、源码哈希、工具版本、命令和退出码](../logs/b-isolated-20261126-211915/manifest.json)

测试使用 GCC 10.2.0、QEMU 4.1.1、OpenSBI v0.4、virt 128 MiB。临时副本完整构建返回 0，QEMU 返回 0，日志含 ALL CHECKS PASS，结尾由测试调用 SBI shutdown 自动关闭。测试运行于 RV64 S 态；没有执行用户入口。

| 验证 | 实际结果与边界 |
| --- | --- |
| PCB 与创建 | 初始化 idle/init、父子关系、栈对齐、两种 copy_thread SP、子 a0、PCB/内核栈分配失败及 kernel_thread 错误透传通过；do_fork 为测试替身 |
| kernel_execve 失败 | 使用栈顶保留 tf，非法映像返回错误，并恢复进入时的 SIE；成功 sret 路径未执行 |
| 合成 ELF | 非页对齐段、跨页数据、逐字节 BSS、代码/数据 PTE、栈清零、guard、tf 和 satp 通过；连续 16 次装载/释放后空闲页恢复 |
| 非法输入 | 15 类错误 ELF（头、架构、截断、表范围、段长度/溢出、用户范围、栈冲突、重叠、入口、对齐、W-only）及无效 exec 参数被拒绝 |
| 逐分配故障 | 268 个分配位置分别注入失败，均返回 -E_NO_MEM，并保留旧状态、恢复空闲页；这是注入，不是真实耗尽 |
| 真实页耗尽 | 持有 31556 页直到 nr_free_pages=0，装载失败；释放持有页后恢复基线。数字依赖本次镜像 |
| 回收范围 | 清理某 PT 时保留同一上级目录中范围外的存活 PT，最终清空后回收上级页表，通过 |
| 实际用户 ELF | 当前构建的 hello/testbss/exit ELF 装载通过；没有执行其中用户指令 |
| wait | 指定/任意、非子进程、非法 PID、NULL、正负退出码、重复 wait、摘链及计数、两次模拟调度含伪唤醒、SIE 0/1 恢复、kill 打断通过 |
| 用户地址 | 拒绝内核/只读/缺失页输出；合法跨页退出码写回通过；无效跨页输出保持原数据、不回收僵尸 |
| 用户 exec | 使用旧用户 mm 中的 ELF 与名称；失败保留 tf/mm；共享旧 mm 减引用但保留；最后引用旧 mm 在替换时释放；最终资源恢复，通过 |

## 6. 复现方式与测试替身

在 WSL 中将工具链及 QEMU 目录加入 PATH，执行：

```bash
env -u QEMU TZ=Asia/Shanghai \
  PATH=/mnt/d/3nd_grade/OS/environment/riscv-toolchain/bin:/mnt/d/3nd_grade/OS/environment/qemu/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  python3 tools/b-isolated/run.py
```

[run.py](../../tools/b-isolated/run.py) 新建 /tmp/lab5-b-isolated-*，排除 .git/obj/bin/docs 后复制源码，将 [harness.c](../../tools/b-isolated/harness.c) 追加到临时 proc.c，并只在临时 init.c 改启动入口。它不修改正式启动链、C 的空位或教师测试。

临时 do_fork 仅创建内核测试对象，不复制用户 mm；schedule 仅模拟状态和唤醒，不切换上下文；do_exit 只通过测试跳转记录调用；proc_run/switch_to 如被意外调用会 panic。do_yield/do_kill 也有显式占位，未作为 C 已实现的证据。临时分配拦截用于故障注入。VMA、实际页表、物理页分配和 B 的待测函数使用真实实现。

前两次运行在故障注入测试自身的覆盖断言处失败，日志保存在 210922、211021 两个目录。原因与修正见 [规格及协作记录](../prompts/b-user-loader.md)。211156、211359、211727 为后续通过的中间版本，最终证据以 211915 及其哈希为准。原始日志均保留。

## 7. 已知限制与联合阶段

- 阶段 0 的交付是接口文档，仍需 A/C 确认；未代发消息。
- 未真实运行 init_main/user_main 的完整启动收尾，未进入 U 态、未实测 ecall/sret 往返、抢占或孤儿转交；没有运行 make grade、GDB 或双重 GDB。这些是阶段 5～6 的工作。
- 正式树缺 C 函数，不能据临时副本链接成功声称正式完整构建成功。
- 隔离构建仍含框架 kmalloc 的整数到指针警告，以及 testbss 的故意越界访问警告。本次没有修改这些非 B 源码，需联合验收时复核。
- 用户页采用普通 4 KiB 页，无 COW、按需缺页、动态链接、argc/argv 栈构造或 SMP 同步。当前代码是普通 eager loader，不是 COW 实现。
- init 的空闲页比较需要在真实创建/退出路径下复核，不能把隔离装载回收的页数当成该断言已实际通过。

## 8. Challenge 2：用户程序何时进入内存

Makefile 先使用 tools/user.ld 将 user/*.c 与用户库链接成 obj/__user_<name>.out；内核链接命令的 --format=binary 把这些 ELF 文件作为数据嵌入内核。引导加载整个内核镜像时，这些 ELF 字节已经随内核进入物理内存，KERNEL_EXECVE 宏引用链接器生成的 start/size 符号。

运行到 user_main/kernel_execve 后，load_icode 才解析所选 ELF，给该进程创建用户页表、分配用户页、复制 PT_LOAD、清零 BSS 并设置入口。因此“文件字节已嵌入内存”和“进程地址空间已装载”发生在不同阶段。

常用操作系统通常从文件系统取得可执行文件，并可通过文件映射与按需缺页装入页面；本实验尚无完整的可执行文件装载基础设施，把 ELF 嵌入内核可以把重点放在特权级、地址空间和进程生命周期。代价是内核镜像更大、程序集合固定，更新程序通常需要重建镜像。本段为源码分析，不是已经做过磁盘装载对比实验。
