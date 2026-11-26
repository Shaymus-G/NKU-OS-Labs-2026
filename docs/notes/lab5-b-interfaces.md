# Lab5 B 阶段 0：接口与资源所有权

负责人：陈铭宇，2412559。基线：`872551b`。本文记录 B 已实现的约定，供 A/C 复核；没有替队友确认、没有代发群消息。

## 函数边界

| B 已补充 | C 继续实现 |
| --- | --- |
| alloc_proc、kernel_thread、copy_thread、proc_init、cpu_idle | proc_run、schedule、switch_to |
| init_main、user_main、kernel_execve、do_execve、load_icode | do_fork、copy_mm、dup_mmap、copy_range |
| do_wait | do_exit、do_yield、do_kill |

`setup_kstack/put_kstack/setup_pgdir/put_pgdir/get_pid/hash_proc/unhash_proc/set_links/remove_links` 保留当前文件内接口。C 在同一 proc.c 中调用静态函数即可，无需增加对外头文件。

## 创建和首次现场

1. C 的 do_fork 依次准备私有 PCB、内核栈、mm、trapframe，最后在临界区分配 PID、hash_proc、set_links、wakeup_proc。失败回滚未发布资源。set_links 已插入普通链表并递增 nr_process，不要重复操作。
2. copy_thread 把 tf 放在 `kstack + KSTACKSIZE - 288`；context.ra=forkret，context.sp=tf；子进程 a0=0。内核线程 SP=tf，使顶部 288 字节保留给首次返回现场；用户 fork 的 stack=0 保留原用户 SP，非零则覆盖。
3. 内核线程初始 SPP=1，用户装载初始 status=SPIE（SPP/SIE/SUM 均为 0），用户 SP=USTACKTOP。tf 与内核栈顶均满足 16 字节对齐。A 返回宏的 sscratch=tf+288 与此一致。
4. proc_run 必须使用 proc->pgdir 的物理地址设置 Sv39 satp，并刷新 TLB。idle 在普通链表和 PID 哈希之外，计入 nr_process；init 是 idle 的子进程。正式路径首个用户进程预期 PID=2。
5. kernel_execve 只接受尚无用户 mm 的内核线程，关中断调用 do_execve；成功后直接 forkrets，不再恢复 SIE 或返回 C 栈。失败恢复进入时中断状态并返回错误。不得把这条路径改成 S 态 ecall。

## exec 和 mm

- do_execve 保持头文件原签名。内核线程（mm=NULL 且 tf.SPP=1）可以提供 KERN_ACCESS 范围内的可信映像；用户进程只能提供自己当前 mm 内可读的 name 和 ELF 缓冲区，不能借 sys_exec 传入内核地址。len 接受 1..15 字节。
- 用户访问同时检查 VMA、地址/长度溢出以及实际 4 KiB 叶页的 V/U/R 或 W；通过 page2kva 内核别名分块复制。不要绕过为原始用户指针 memcpy；无需全局开启 SUM。
- 对 source mm 加锁，先在新 mm 中完成装载；失败释放新 mm，旧 mm、tf、名称、satp 均保留。成功才提交新 mm（count=1）、tf 和 satp，随后旧 mm_count_dec，降为 0 才 exit_mmap/put_pgdir/mm_destroy。共享旧 mm 只减引用。
- 这是成功后替换、失败保留旧映像的事务语义。C 不应在用户 exec 返回错误时强制销毁仍有效的旧 mm。用户 syscall 分发成功后写 a0=0，与清零后的新现场相容。
- 不支持按需缺页、用户大页或 COW。用户栈 VMA 为 USTACKSIZE，底页作为未映射 guard，其余 255 页预分配并清零。C 复制时应跳过 guard 的缺失 PTE。
- ELF 段在页边界取整后不允许重叠；不支持动态链接/解释器及 W-only 段。入口必须落在可执行 PT_LOAD 内。权限遵从 ELF，未强加 W^X。

## exit/wait

- pid=0 等待任一子进程，pid>0 等待指定子进程，负数/超范围/非子进程返回 -E_BAD_PROC。成功返回 0，通过可选 code_store 输出退出码；非法输出地址返回 -E_INVAL 且不回收子进程。
- C 的 do_exit 负责切离用户页表、减少/释放 mm，令 proc->mm=NULL，设置 exit_code 和 PROC_ZOMBIE，唤醒 WT_CHILD 父进程，完成孤儿转交并调度；不得释放自己仍在使用的内核栈/PCB，不得返回已退出现场。
- B 的 do_wait 在关中断区域检查子进程并登记 PROC_SLEEPING/WT_CHILD，然后保持关中断调用 schedule，返回后恢复原 SIE，重新检查条件。睡眠前释放 mm_lock。C 的调度器必须支持进入时 SIE=0，wakeup_proc 保持现有清 wait_state 的实现。
- wait 发现 PF_EXITING 时调用 do_exit(-E_KILLED)，不在 do_wait 内伪造退出成功。C 的 kill 应唤醒 WT_INTERRUPTED 的等待者。
- 僵尸只能由其父进程 wait 执行 unhash_proc/remove_links/put_kstack/kfree；B 断言被回收者 mm=NULL。init 不退出，并持续等待/接管孤儿。

## 共同内存辅助函数的必要修正

B 修改 pmm.c 的 exit_range，保留 A 映射函数和 C 的 function3 空位。原逻辑可能仅凭范围内的部分目录项释放仍有范围外活页表的上级目录；还可能在目录已空时因不存在的项而漏回收。现在仅清理请求范围内的空 PT，并检查上级的全部 512 项后决定是否释放。C 的失败回滚和 do_exit 应复用修正后的 exit_mmap/exit_range。

以上按当前单 hart、内核不可抢占约定实现，不是 SMP 锁设计。实际 fork、切换、退出与用户态返回仍需联调。
