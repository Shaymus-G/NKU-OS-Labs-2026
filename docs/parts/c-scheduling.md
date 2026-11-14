# 调度、上下文切换与联合验收

尚文轩，2412456；2026-11-14。内核实现提交aa93cbe，依赖国峻赫f66f380及陈铭宇ebe4c84/514346c。正文仅归纳本次真实结果，不把前阶段隔离测试写成整体验收。

## 1. 调度与首次运行

cpu_idle持续观察当前线程need_resched；schedule在关闭本地中断后清标记，从当前线程后继开始环形扫描，idle则从proc_list头开始。只选PROC_RUNNABLE，没有候选回idle。空链表可立即终止；当前是唯一runnable时扫描回自身，允许自选。选中者runs递增，随后调用proc_run。

proc_run对相同目标不切换；不同目标在临界区保存旧current，发布新current，将PCB中物理pgdir右移12位形成PPN，与RV64的Sv39 MODE=8合成satp，再flush_tlb，最后switch_to。未使用libs/riscv.h中Sv32遗留lsatp，也未改变公共头文件。

switch_to按context中ra、sp、s0～s11顺序存储旧值并读取新值，共14个8字节槽、112字节。因为切换发生在C调用边界，保存的是ABI要求的被调用保存现场及返回地址；异步Trap则需要更完整的trapframe。ret加载的新ra使执行流转到目标线程的恢复位置。

新线程没有过去真正执行过的switch_to调用，陈铭宇预设context.ra=forkret、context.sp=tf。首次链为：

```text
idle → schedule → proc_run → switch_to
 → forkret → forkrets(current->tf) → __trapret
 → RESTORE_ALL → sret → kernel_thread_entry
 → a0=s1，调用s0指向的init_main
 → 函数返回后进入框架do_exit占位panic
```

真实GDB按当前ELF断点确认switch_to、forkret、kernel_thread_entry和init_main；检查PID=1、首次sp=tf及实际参数Hello world!!。详见 [GDB原始记录](../logs/c-first-switch-gdb.log)、[命令](../logs/c-first-switch-commands.txt)。参数和入口不是另写测试字符串模拟出来的。

## 2. Trap迁移边界

只补充Lab4恢复宏和C传参，原SAVE_ALL、forkrets、SUM设置和原trap分发保持。时钟重设下一事件、累加ticks、每100次输出统计；不带入Lab3的1000 ticks关机，也不在时钟中加入未约定的抢占调度。已知非法指令/断点按16/32位编码推进EPC，其余异常只作框架诊断，不以跳过故障指令代替映射修复。

## 3. 测试结果与判据

| 项目 | 实际结果 | 证据 |
|---|---|---|
| 普通clean构建 | 成功；保留框架kmalloc转换警告 | [构建](../logs/c-build.log) |
| 普通启动 | PMM/页表/SLOB/VMA通过，init三行输出后到do_exit占位panic | [运行](../logs/c-run.log) |
| 教师评分 | PMM10、创建10、调度10，共30/30 | [评分](../logs/c-grade.log) |
| 多线程重复切换 | 两个真实线程各100轮；s0～s11探针及私有栈哨兵保持；satp匹配current | [独立运行](../logs/c-schedule-20261114-150209/run.log) |
| 调度边界 | 睡眠线程跳过，唯一runnable回绕自选；原SIE开/关均保持；无候选选idle | 同上 |
| 首次运行 | GDB真实断点和参数断言通过 | [GDB](../logs/c-first-switch-gdb.log) |

独立测试仅在临时副本将init_main交给验收驱动，不修改正式启动。两worker结束后标为sleeping；最后所有普通线程均sleep，观察真正schedule选择的proc_run目标为idle，再由测试驱动SBI关机。该最后断言验证的是**兜底选择**，并没有在这个用例中实际恢复到idle后再唤醒线程。普通首次idle→init则由默认运行和GDB验证。

独立测试自然退出0；普通框架运行进入panic/monitor，不宣称自然成功关机。当前do_exit不回收线程资源，本实验也不声称完整线程退出生命周期已实现。

环境为WSL Ubuntu22.04、GCC10.2、QEMU6.2/OpenSBI1.3。首次构建因跨分支旧.d文件引用已删除best_fit头失败，clean后解决，见 [失败](../logs/c-stale-dependency-failure.log)。评分最初0/30来自控制台采集：semihosting输出在连接GDB后由remote console交给GDB，而教师脚本丢弃其stdout。独立tools/c-grade.py只把真实输出送回评分文件，未修改教师逻辑或OSopt，保留 [首次失败](../logs/c-grade-first-failure.log)、[诊断失败](../logs/c-grade-gdb-console-failure.log)、[GDB输出](../logs/c-grade-gdb.log)。

## 4. Challenge 1：保存和恢复中断

local_intr_save读取sstatus.SIE。原先开启时关中断并返回true；原先关闭时返回false。local_intr_restore只在保存值为true时重新开启。因此内部临界区不会误开外层已经关闭的中断。schedule和proc_run嵌套时，内层保存到false，外层最终恢复进入时状态。

切换出去的旧线程暂不执行local_intr_restore；它将来被切回，才从switch_to之后继续并恢复旧状态。新线程首次没有旧调用栈，靠B构造的trapframe.SPIE在sret时恢复开启状态。当前实现限定单hart，不把关中断说成多核互斥；它也不屏蔽同步异常。

## 5. 交给汇总负责人的扩展分析

### Sv32、Sv39、Sv48及get_pte

| 模式 | 页表级数 | 每级索引 | PTE大小 | 单表大小 | 基本页 |
|---|---:|---|---:|---:|---:|
| Sv32 | 2 | 10+10 | 4字节 | 4KiB | 4KiB |
| Sv39 | 3 | 9+9+9 | 8字节 | 4KiB | 4KiB |
| Sv48 | 4 | 9+9+9+9 | 8字节 | 4KiB | 4KiB |

当前get_pte用两段类似代码走过Sv39的两个非叶层，最后定位第三级叶项。每一段都要查有效性、区分大页叶子与下级指针、必要时分配清零新表并登记，所以结构相像；Sv48还需增加一个层级，Sv32的索引宽度和PTE宽度不同。不能直接照搬循环次数或把有效大页当作下一张表。大页也意味着不一定每次遍历所有层级。

查找/分配合在get_pte(create)里能复用遍历代码、接口简洁，但隐藏分配副作用，并使中途失败回滚更复杂。可拆为只读查找与ensure/create接口，底层复用同一walker，并记录本次创建页以回滚。当前实验不必为答思考题重构公共接口，使用create=false的只读调用必须不分配。A已经实现中间页失败回滚和大页识别，具体结论由A章节及额外证据支持，不冒充C已穷举它的所有失败路径。

### 课堂PID回绕问题

get_pid维护last_pid及next_safe，在安全窗口内快速递增；到边界或回绕后扫描proc_list，跳过仍被占用的PID，并更新下一安全边界。idle的0号不会被普通分配返回。该算法避免与**现存**线程冲突，不保证PID永不复用，也不能独自解决持有已退出旧PID的外部句柄歧义；完整系统可借助生命周期管理、引用或代数标识。当前未进行长期回绕压力测试，不能将源码分析写成实测。

## 6. AI迭代、复现及未实现机制

规格见 [C提示词](../prompts/c-scheduling.md)，先明确接口、禁止改动和可验证结果，再实际写入文件。旧依赖与评分采集失败通过日志区分为环境/工具问题，没有删除教师检查或改动A/B实现迁就结果。记录是会话整理，不是虚构人工追加指令或逐字导出。

在根目录激活本机环境后执行：

```bash
source ~/OSopt/env.sh
make clean
make
make qemu
python3 tools/c-schedule-check.py
python3 tools/c-first-switch.py
python3 tools/c-grade.py       # C环境；A/B原环境可普通make grade
make                          # 评分后恢复默认镜像
```

默认保留共享地址空间的内核线程、协作式显式调度；没有用户进程复制、时间片抢占、多核锁、按需分页、页面置换和完整退出回收。课堂真实板体验不属于已验证硬件成果。A负责总报告，本文及Challenge分析供其汇总；不直接覆盖A的报告组织。
