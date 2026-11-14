# Lab4 C交接：调度、切换与联合验收

负责人：尚文轩，2412456。日期：2026-11-14。分支lab4。

## 提交与当前状态

- 依赖A的f66f380及B的ebe4c84、514346c，已在当前集成版本完成真实首次运行。
- 规格21df62c；实现aa93cbe；测试及证据3e92591；本文与个人章节另行提交，以git log为准。
- 本轮未push；提交作者/提交者仅shangwenxuan，无AI共同作者。用户审阅后自行push。
- make clean后普通构建成功；原始init_main实际执行并输出三行，随后进入框架do_exit占位panic，未删除该逻辑。
- 教师三项各10分，最终30/30。两worker各100次真实切换及首次链GDB通过。评分后默认镜像已重建。

## 修改范围和约定

正式代码：kern/process/proc.c仅function3、kern/process/switch.S、kern/schedule/sched.c、kern/trap/trap.c和trapentry.S指定迁移位置。保留B的创建函数、A的内存实现、forkrets、SAVE_ALL、SUM、公共结构和教师测试；未修改正式Makefile或init.c。

调度是显式协作式，不是时钟时间片抢占。idle不在proc_list，作为兜底；普通线程从当前后继循环查runnable。唯一runnable可以选自己，runs仍递增。无候选选择idle。current、Sv39 satp与context切换处于关本地中断的临界区，恢复进入时状态。

pgdir仍是物理根表地址，以MODE=8与pgdir>>12编码RV64 satp，随后sfence.vma；没有调用Sv32遗留lsatp。context为ra/sp/s0-s11，112字节。新线程context和tf契约遵照B交接，首次链保持forkret→forkrets→__trapret→sret→kernel_thread_entry。

## 运行环境与复现

环境：WSL Ubuntu22.04，GCC10.2、GDB12.1、QEMU6.2/OpenSBI1.3，单hart、128MiB。每个新shell激活工具链，在仓库根目录：

```bash
source ~/OSopt/env.sh
make clean
make
make qemu
python3 tools/c-schedule-check.py
python3 tools/c-first-switch.py
python3 tools/c-grade.py
make
```

A/B的QEMU4.1.1环境可先普通make grade；C环境使用c-grade.py临时PATH适配，因为OpenSBI semihosting连接调试器后通过GDB远程控制台输出，原评分丢弃GDB输出会造成空文件。适配只采集真实日志，不修改教师评分/测试和OSopt包装器。GDB remote closed诊断可能来自monitor读EOF后模拟器结束，不能把该诊断本身当作完整断点命中成功；评分结论来自实际三个教师PASS及30/30。

## 证据

| 内容 | 路径 |
|---|---|
| clean后正常构建 | [c-build](../logs/c-build.log) |
| 正常init执行及占位退出 | [c-run](../logs/c-run.log) |
| 教师30/30 | [c-grade](../logs/c-grade.log) |
| GDB实际首次链 | [日志](../logs/c-first-switch-gdb.log)、[命令](../logs/c-first-switch-commands.txt) |
| 独立200轮切换与边界 | [运行](../logs/c-schedule-20261114-150209/run.log)、[清单](../logs/c-schedule-20261114-150209/manifest.log) |
| 默认重建 | [恢复构建](../logs/c-restored-build.log) |

没有新增截图，以原始文本记录为证。跨分支旧依赖构建失败、评分控制台失败均保留独立日志，不倒改历史结果。原始CRLF日志可能触发diff空白提示，不为消除该提示篡改记录。

独立测试在临时副本创建两个worker，每个100轮，汇编哨兵检查s0～s11、C局部数组检查私有栈、每轮检查satp与current。还验证sleeping跳过、链表回绕、仅当前runnable、SIE开关和idle兜底。最后兜底探针在proc_run入口确认目标idle后由测试驱动关机：验证了选择，不声称在该用例完成idle恢复后再次唤醒。正式idle→init首次实际切换由另一GDB会话证明。

## 报告与扩展

个人章节：[c-scheduling](../parts/c-scheduling.md)，规格/过程：[c-scheduling](../prompts/c-scheduling.md)。包括Challenge1中断保护完整解释，以及供A汇总的Sv32/Sv39/Sv48、get_pte查找/分配拆分讨论、课堂PID回绕分析。

本次总报告由A负责，C没有越权重写report.md。请A汇入C章节并复核页表/Challenge2分析。PID分析说明只避开活PID，不保证永久不复用；本轮未做长期PID回绕实测。真实板运行、用户mm复制、完整退出回收、抢占、多核和按需分页不是已完成成果。

## 接下来各成员需要做什么

国峻赫：复核Sv39 satp地址约定、页表/TLB及Challenge2；确认pmm.c中自己的LAB4 function1 YOUR CODE标记和个人规格/证据，C没有替你更改学号。汇总报告，并补充需要的映射前后PTE/ref证据。

陈铭宇：复核首次实际运行链，特别是context.sp=tf、s0函数、s1参数、子a0=0与SPP/SPIE约定；先前“创建未执行”已由C联合版补证，不改写你原阶段日志。

全组：在共同QEMU4.1.1环境重新运行普通构建、评分和C独立回归，使用新日志；A/B/C本人审阅章节及AI整理记录，确认最终提交号和课程提交要求。C本机30/30不冒充其他成员已复测该新版本。

## 可发群消息

实际push成功后补入最新提交号再发：

> A、B，我完成C的调度、上下文切换、Trap移植和Sv39 SATP接入，已推送lab4，最新提交【实际HEAD】，请同步整个分支。保留了你们的内存和创建实现、forkrets及教师测试。普通启动已实际进入init_main并输出三行，之后到框架do_exit占位panic；本机评分30/30。两个worker各100次切换、寄存器/栈/SATP、中断状态与选择边界通过，首次运行链另有真实GDB证据。请看docs/notes/c-handover.md。A请复核页表与SATP约定、补齐自己的标记及材料，并汇总C章节和Challenge；B请复核创建到首次执行的接口。请在原环境做一轮联合回归保存新日志，之后我们三人确认报告和最终版本。
