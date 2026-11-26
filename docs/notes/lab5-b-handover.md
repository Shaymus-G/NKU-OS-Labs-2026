# Lab5 B 阶段 0～4 交接

负责人：陈铭宇，2412559；2026-11-26（Asia/Shanghai）。

## 版本与范围

开发基线：lab5/872551b。B 的实现与独立验证证据提交：`240b6b93c93f269fe2cb1235017f1894a07efe51`（简称 `240b6b9`）。本文和个人报告由后续文档提交补充，随本次交付一起发布到共同分支 `lab5`。本文件未代发微信群。

[查看 B 实现及证据提交](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/240b6b93c93f269fe2cb1235017f1894a07efe51)。C 应先 fetch 并检查本地状态；工作区干净且可快进时执行 `git pull --ff-only origin lab5`，确认历史包含 `240b6b9` 及后续 B 文档提交，再继续开发。有本地修改或分叉时先保存、审查差异，不强行覆盖。

正式源码改动：

- kern/process/proc.c：alloc_proc、kernel_thread、copy_thread、proc_init、cpu_idle；ELF/exec/首次用户返回路径；do_wait；init_main/user_main；安全用户复制辅助函数；set_proc_name 短字符串修正。
- kern/mm/pmm.c：修正 exit_range 的上级目录释放判定。没有覆盖 A 的映射实现，C 的 copy_range 空位保留。

## C 接入前必读

完整约定见 [lab5-b-interfaces.md](lab5-b-interfaces.md)，这是 B 侧交付，待 A/C 复核。

1. do_fork 使用 B 的 copy_thread；用户 stack=0 保留 SP。发布使用 hash_proc/set_links/wakeup_proc；set_links 已登记普通链表和计数。
2. B 已补 cpu_idle，C 不要重复定义。proc_run/schedule/switch_to 仍由 C 完成；切换需写入 proc->pgdir 并 flush TLB。
3. copy_mm 支持 mm=NULL 内核线程、CLONE_VM 共享和普通复制；mm_count 与 pgdir 必须一致。用户栈底一页未映射，复制应跳过缺失 PTE。
4. do_exit 释放/减少 mm 并置 proc->mm=NULL，记录退出码和僵尸状态，唤醒 WT_CHILD 父进程、转交孤儿、调度且不返回。B 的 do_wait 最终释放子 PCB/内核栈，不可重复释放。
5. wait 调用 schedule 时 SIE=0、状态为 SLEEPING/WT_CHILD、mm_lock 已释放。保留 wakeup_proc 清 wait_state 及 kill 对可中断等待者的唤醒。
6. exec 失败保留旧映像；成功新 mm_count=1，旧 mm 仅在引用归零时释放。内核首次 exec 成功直接 forkrets，不需要 S 态 ecall。

## 实际验证

正式 B 模块强制重编译返回 0：[日志](../logs/b-modules-final.log)。

最终隔离副本构建/QEMU 均返回 0：[运行日志](../logs/b-isolated-20261126-211915/qemu.log)、[版本与命令](../logs/b-isolated-20261126-211915/manifest.json)。覆盖 PCB/现场、15 类非法 ELF、跨页/BSS/权限/栈、16 次回收、268 个故障注入位置、真实页耗尽恢复、局部页表回收、实际 hello/testbss/exit ELF 装载、wait/用户指针与共享/非共享旧 mm 的 exec。

复现：WSL 配置本机现有工具 PATH 后执行 `python3 tools/b-isolated/run.py`，详见 [B 阶段报告](../parts/b-user-loader.md)。测试替身和启动替换只注入 /tmp 副本，不混入正式路径。

## 尚未验证与下一步

本轮没有真实 fork、调度切换、退出、U 态运行、ecall/sret 往返、孤儿转交、教师评分或 GDB 证据。临时测试通过不能当作 C 功能或 Lab5 总体验收通过。

C 接入最小内核线程创建、调度/切换和退出后，A/B/C 先验证正式 hello 启动与 U→S→U，再联测 testbss、badarg、exit、yield、spin、forktest、资源回收与完整 grade。root report.md 仍是 Lab4 报告，30/30 不属于 Lab5。
