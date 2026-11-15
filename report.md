# 实验四：进程管理与内核线程切换

- 仓库分支：`lab4`
- 报告日期：2026 年 11 月 15 日
- 联合验收对应的源码提交：`36412b6b221b2677602bbbb9ac29dd13a92ce7d9`
- 教师评分：**30/30**（PMM、Process Create、Schedule 均通过）

## 1. 实验目标与分工

本实验从 Lab2 的物理内存管理和 Lab3 的 trap 机制出发，完成 uCore 在 RV64/Sv39 环境下的内核线程创建与调度。最终验证的是一条完整路径：内核能分配物理页和内核栈，能为新线程构造第一次运行的现场，也能通过调度切换到该现场并让线程函数真正执行。

| 成员 | 负责内容 | 主要实现提交 | 个人记录 |
| --- | --- | --- | --- |
| A 国峻赫（2412082） | 功能一：物理内存初始化、Best-Fit、Sv39 基本页映射；联合验收与映射取证 | `f66f380`，`36412b6` 补功能一学号标记 | [A：物理内存与页映射](docs/parts/a-page-mapping.md) |
| B 陈铭宇（2412559） | 功能二：PCB 初始化、内核线程创建、首次现场构造、失败回滚 | `ebe4c84` | [B：创建阶段记录](docs/parts/b-process-create.md) |
| C 尚文轩（2412456） | 功能三：调度、上下文切换、必要 trap 迁移；多线程回归与首次切换取证 | `aa93cbe`；验收提交 `3e92591` | [C：调度与联合验收](docs/parts/c-scheduling.md) |

按 **内存基础 → 创建 → 调度与首次执行 → 联合验证** 的顺序接入。A 先验证能否分配页和维护映射，B 再验证 PCB 和新线程现场是否完整，C 接上真正的切换。B 的第一阶段记录保留了当时尚未运行的部分；本报告的整合结果来自后续测试。

## 2. 内核启动与模块接口

启动汇编建立 Sv39 启动映射并进入 `kern_init()`。内核解析 DTB、初始化内存管理和中断入口，然后运行 `vmm_init()`、`proc_init()`；接着初始化时钟、打开 S 模式中断，进入 `cpu_idle()`。此时 `current` 是 idle，init 已经可运行。idle 根据 `need_resched` 进入调度器，init 才获得 CPU。这说明**创建成功与开始运行是两件事**：`do_fork()` 只准备和登记线程，执行权由调度器交给它。

| 模块接口 | 跨模块约定 |
| --- | --- |
| `boot_pgdir_va` / `boot_pgdir_pa` | 前者供软件访问根页表；后者是物理基址，由 PCB 的 `pgdir` 保存，用于编码 `satp`。 |
| `proc_list` / PID 哈希表 | 普通线程加入两者。idle 不登记在其中，但计入 `nr_process`，由调度器单独兜底。 |
| `context` / `trapframe` | `context` 是调度函数调用边界的现场；`trapframe` 保存 trap 所需的通用寄存器与 CSR，也被用于构造新线程首次执行现场。 |
| `current` / 内核栈 | 发布新 `current` 后切换上下文，`switch_to()` 恢复目标的 `sp`。普通线程各有两页内核栈，共享内核地址空间。 |

## 3. 功能一：物理内存初始化和 Sv39 页映射

A 主要修改了 [pmm.c](kern/mm/pmm.c) 和 [default_pmm.c](kern/mm/default_pmm.c)。

### 3.1 从 DTB 到空闲页

`page_init()` 检查 DTB 报告的起点、容量以及地址计算是否溢出。实际管理末端受内核直接映射范围限制，并向下对齐到页边界。`npage` 是末端的绝对物理页号；描述符数量为 `npage - nbase`，访问某一页的描述符用 `pages[PPN - nbase]`。

描述符数组放在内核 `end` 向上页对齐的位置。先将描述符初始化为保留，并清理引用计数、标志和链表等字段，再把数组之后剩余的完整物理页交给 `init_memmap()`。这样启动区域、内核本体及 `struct Page` 数组不会被再次分配。

分配器沿用框架的 `default_pmm_manager` 接口，实际策略是 Best-Fit，串口显示 `best_fit_pmm_manager`。申请连续页时选择最小的足够块，多余部分留在空闲链表；释放按物理地址插入，与连续的前后空闲块合并。保留 `basic_check()`、`best_fit_check()`。SLOB 内核堆和 VMA 属于框架，用自检验证其初始化依赖已满足。

| 实际环境（128 MiB DRAM） | 描述符 | 初始空闲区（右端不含） | 初始空闲页数 |
| --- | --- | --- | ---: |
| A 独立版本 `f66f380` | 32768 × 64 字节 | `[0x80409000, 0x88000000)` | 31735 |
| 联合版本 `36412b6` | 32768 × 64 字节 | `[0x8040e000, 0x88000000)` | 31730 |

两次 DRAM 都是 `[0x80000000, 0x88000000)`，描述符均占 2 MiB。空闲起点相差 `0x5000`，与整合后的链接布局变化一致；这里比较的是初始化计数，不能用它判断线程运行后的内存泄漏。

### 3.2 查找、建立、替换和撤销

| 函数 | 实现与正确性条件 |
| --- | --- |
| `get_pte(pgdir, va, create)` | 用 `PDX1/PDX0/PTX` 查三级页表，返回末级项地址，调用方仍需检查 `PTE_V`。只读模式不分配。创建模式按需申请并清零下级表；若两层均缺而第二次申请失败，释放第一张临时表，根路径不发布。拒绝把高层大页叶项当成下级指针。 |
| `page_insert()` | 增加新页引用、更新叶 PTE、同步翻译，再减少旧页引用，归零才释放；同一物理页仅更新映射权限，不增减引用。 |
| `page_remove()` | 无映射时直接返回；有映射时清除 PTE、同步翻译，再减少引用并按需释放数据页。此接口不自动回收空的中间页表。 |
| `tlb_invalidate()` | 叶项更新用 `sfence.vma va, zero`，新增非叶路径后用全地址范围的本地 `sfence.vma`。页表写入不能代替本地地址翻译同步；本实验不涉及跨 hart TLB 同步。 |

本实现令 `perm=0` 生成可读叶项，含 `W` 时也补 `R`，并由 `pte_create()` 设置 `V`。课程自检对非叶项检查 `PTE_U`，实现保留这一课程约定；[RISC-V 特权架构规范][riscv]要求软件清零非叶项保留的 `U` 位，因此不能从课程环境通过推断对所有实现都可移植。当前 `page_insert()` 把 `get_pte()` 返回空统一当成 `-E_NO_MEM`；拒绝进入大页也可能走这一返回路径，错误码并不单指分配失败。

### 3.3 映射替换的 GDB 证据

在 `check_pgdir()` 的同一次运行中，分别停在 **替换 `0x1000` 前** 与 **替换后、第一次删除前**（该构建的 `pmm.c:383`、`pmm.c:390`）。两地址上两级 Sv39 索引均为 0，末级索引分别为 0、1。通过末级 PTE 取出 PPN（右移 10 位、保留 44 位），再用保存下来的原 PPN 读取原物理页描述符。

| 观测项 | 替换前 | 替换后 |
| --- | --- | --- |
| VA `0x0` 的 PTE | `0x20103803` | `0x20103803` |
| VA `0x1000` 的 PTE | `0x20104417` | `0x20103803` |
| VA `0x1000` 指向的物理页 | `p2: 0x80411000` | `p1: 0x8040e000` |
| VA `0x1000` 的 PTE 权限/有效位 | `V/R/W/U` | `V/R` |
| `p1.ref` / 原 `p2.ref` | 1 / 1 | 2 / 0 |

替换后两个 VA 指向同一个 `p1`，旧映射的 W、U 位没有继承。停点实测证明了 PTE 和引用变化；空闲页恢复由完整的 `check_pgdir()` 自检检查。本次未单独在 GDB 中测量 TLB 命中或强制触发页表第二次申请失败。独立运行中的 `check_boot_pgdir()` 也通过了低地址别名读写检查（`0x100` 与 `0x1100`），不据此声称高地址映射已逐页检查。

## 4. 功能二：内核线程创建

B 在 [proc.c](kern/process/proc.c) 的功能二区域实现六个函数。

| 函数 | 关键操作 |
| --- | --- |
| `alloc_proc()` | 用 `kmalloc()` 分配 PCB，清零后显式设置 `PROC_UNINIT`、PID=-1、共享根页表物理地址和两个自环链表节点。 |
| `copy_mm()` | 断言当前和子线程的 `mm` 均为空；内核线程共享内核地址空间，本实验不复制用户虚拟内存。 |
| `copy_thread()` | 在两页内核栈顶部放置 288 字节 trapframe 并复制模板。设子线程 `a0=0`；若调用方给出非零 `stack`，以它为恢复后的 `sp`，否则 `sp=tf`。令 `context.ra=forkret`、`context.sp=tf`。 |
| `do_fork()` | 检查上限、分配 PCB/内核栈、处理 `mm`、准备现场。全部完成后才分配 PID，加入哈希和链表，更新计数并唤醒。失败按已取得资源的逆序释放；成功返回正 PID。 |
| `kernel_thread()` | 清零模板，将函数和参数分别放在 `s0`、`s1`，令 `epc=kernel_thread_entry`；设置 SPP、SPIE，清 SIE，再调用 `do_fork()`。 |
| `proc_init()` | 初始化进程链表与 PID 哈希表；使 PID 0 的 idle 复用启动栈、成为 `current` 并标记待调度。再创建以 idle 为父、PID 1 的 init。 |

创建时在单 hart 上保存并关闭本地中断，避免状态发布到一半被观察到；无论成功失败均恢复进入时的中断状态。超过 `MAX_PROCESS` 返回 `-E_NO_FREE_PROC`，PCB 或栈不足返回 `-E_NO_MEM`，`copy_mm()` 的错误原样传递。框架的 `set_proc_name()` 固定复制 15 字节，因此 B 用定长名字数组作为来源。已有 `setup_kstack()/put_kstack()` 管理栈页，`get_pid()/hash_proc()/find_proc()` 分别负责编号、登记和查询，并不由本次重写。

B 在临时副本复用教师创建断言，还验证了填入 `0xa5` 的 PCB 是否被初始化、16 个线程的 PID/父指针/私有栈和登记、指定栈的现场，以及 PCB/栈/mm 失败注入各 64 次。另用逐页占用真实空闲页触发栈分配失败，恢复后核对资源；`copy_mm()` 注入和 `MAX_PROCESS` 达上限是合成测试，不代表实现了用户态内存复制或实际创建了上限数量的线程。详细数据见 [B 测试结果](docs/logs/b-create-20261113-094247/result.json) 和 [B GDB 日志](docs/logs/b-create-20261113-094247/gdb-session.log)。

## 5. 功能三：调度、上下文切换和 trap

C 的主要修改在 [sched.c](kern/schedule/sched.c)、[proc.c](kern/process/proc.c)、[switch.S](kern/process/switch.S) 和 [trap 模块](kern/trap)。

1. `cpu_idle()` 检查 `current->need_resched`。进入 `schedule()` 后保存并关本地中断，清当前线程的标记。从当前线程后继开始环形扫描；从 idle 出发则从链表头开始。选到第一个 `PROC_RUNNABLE`，递增其 `runs`；没有可运行线程就选 idle。当前线程是唯一可运行者时可以选回自己。
2. `proc_run()` 对相同目标不切换。目标不同时，保存旧 `current`，发布新 `current`，用 PCB 中的物理根页表基址右移 12 位得到 PPN，与 `8ULL << 60` 合成 RV64 的 Sv39 `satp`，然后 `flush_tlb()`，最后调用 `switch_to()`。没有借用框架中面向 Sv32 的旧 `lsatp()` 实现。
3. `switch_to()` 用 `a0`、`a1` 接收旧和新 `context` 指针。保存并恢复 `ra/sp/s0–s11` 共 14 个 8 字节槽（112 字节），随后 `ret` 使用新 `ra`。这是 C 函数调用边界的上下文，调用者按 ABI 管理易失寄存器。Trap 可以在非调用边界发生，需保存更完整的现场：本实验 32 个通用寄存器槽及 `sstatus/sepc/stval/scause` 四个槽，共 288 字节。

新线程没有过去的 `switch_to()` 调用可以返回，因此 B 构造的 `context.ra` 将它引到 `forkret()`：

```text
idle → schedule → proc_run → switch_to → forkret → forkrets
     → __trapret → RESTORE_ALL → sret → kernel_thread_entry → init_main
```

`forkrets` 让 `sp` 指向目标 trapframe；恢复代码从中取回 CSR 和通用寄存器，最后恢复 `sp`。`sret` 按 `sepc` 转到 `kernel_thread_entry`，从 SPP 恢复特权级，从 SPIE 恢复 SIE。框架线程入口执行 `a0 = s1`，再调用 `s0` 指向的线程函数。时钟中断重新设置下次事件并累加 `ticks`，没有迁入 Lab3 的固定次数关机，也没有加入时间片抢占。断点和非法指令处理按实验中的 16/32 位指令推进 EPC；其他异常保留诊断路径，并未实现通用缺页恢复。

联合版本的 GDB 选取了四个停点，用实际执行把 B 的构造与 C 的恢复连起来：

| 停点 | A 环境的实际观察 |
| --- | --- |
| `switch_to` | `current->pid=1`；`satp=0x8000000000080208`，即 MODE=8、ASID=0、根页表物理地址 `0x80208000`。 |
| `forkret` | PID=1；`tf->epc=0xffffffffc0203194`。 |
| `kernel_thread_entry` | 地址等于前一停点读出的 EPC；`sp=current->tf=0xffffffffc0411ee0`，`s1` 指向 `Hello world!!`。 |
| `init_main` | PID=1；`a0` 指向同一个实参。 |

地址只对该构建有效。C 也用支持 Python 的 GDB 进行了独立断言，见 [首次切换记录](docs/logs/c-first-switch-gdb.log) 与 [对应命令](docs/logs/c-first-switch-commands.txt)。路径中的其余步骤由源码衔接解释，不当作 GDB 逐指令观察结果。

## 6. Challenge

### 6.1 保存和恢复本地中断

`local_intr_save(flag)` 读取 `sstatus.SIE`：原本开启就关闭并记录 true；原本关闭就记录 false。`local_intr_restore(flag)` 只在 true 时重新开启。这样 `schedule()` 已关中断后再进入 `proc_run()`，内层不会误开外层的中断。若真的切走，旧线程要等将来被切回才从 `switch_to()` 返回并继续恢复；新线程没有旧调用栈，通过构造的 SPIE 和 `sret` 建立首次中断状态。独立测试核对了进入时 SIE 的两种状态。这里保护的是本 hart 的中断临界区，不屏蔽同步异常，也不是多核互斥锁。

### 6.2 Sv32、Sv39、Sv48 及 `get_pte()` 的结构

| 模式 | 页表层数 | 每层 VPN 索引位数 | 每张表条目数 × PTE 大小 | 基本页偏移 |
| --- | ---: | ---: | --- | ---: |
| Sv32 | 2 | 10 | 1024 × 4 字节 | 12 位 |
| Sv39 | 3 | 9 | 512 × 8 字节 | 12 位 |
| Sv48 | 4 | 9 | 512 × 8 字节 | 12 位 |

三种模式的基本页和单张页表都是 4 KiB。普通页查找每到一层都要以 VPN 作索引，判断项是否有效、是叶项还是下级指针。Sv39 的 `get_pte()` 有两段相似的中间层处理；Sv48 多一层，Sv32 虽少一层，但索引宽度和 PTE 宽度也不同。大页在上层即可命中叶项，不能继续按下级指针解引用。

查找与分配合在 `get_pte(create)` 中能复用遍历，同一接口读写都方便；代价是 `create=1` 有分配与修改页表的副作用，失败时还要撤销中间资源。若扩展，可用公共 walker，把纯查找与确保路径存在做成两个入口，由后者在全部分配成功后发布、失败时回滚。本次保留框架接口，保证 `create=0` 不分配。

课堂延伸：`get_pid()` 用 `last_pid` 与 `next_safe` 维护一段可递增的安全窗口，触到边界或回绕时扫描已登记线程，避开仍在使用的 PID。它防止与活线程重号，不保证 PID 永不复用。本组做了小规模唯一性检查，未做长期回绕压力测试。

## 7. 验证、复现与原始证据

A 在 WSL Ubuntu 20.04 上用 RISC-V 交叉工具链、QEMU 4.1.1 / OpenSBI v0.4 进行最终复测；手动调试用 SiFive GDB 10.1。B 的独立创建验证使用 GCC 10.2.0、GDB 10.1、QEMU 4.1.1；C 的另一套环境是 WSL Ubuntu 22.04、GCC 10.2、QEMU 6.2 / OpenSBI 1.3。下列命令在已配置 RISC-V 工具链和 QEMU 的仓库根目录执行：

```bash
git rev-parse HEAD
command -v riscv64-unknown-elf-gcc
command -v qemu-system-riscv64
make clean
make
make qemu
# 看到 init 输出和 monitor 后退出 QEMU，再运行下面的命令。
make grade
make
python3 tools/c-schedule-check.py
# 可选：GDB 启用了 Python 时运行自动取证；否则用 make debug / make gdb 手动验证。
python3 tools/c-first-switch.py
```

| 证据阶段 | 实际结果 | 记录 |
| --- | --- | --- |
| A 独立启动 | 物理页、页表、SLOB 和 VMM 自检通过，一直运行到 `vmm_init()` 返回标记；随后手动退出。 | [构建](docs/logs/a-isolated-build.log) · [串口](docs/logs/a-isolated-qemu.log) · [隔离补丁](docs/logs/a-isolated-init.patch) |
| B 独立创建 | 临时副本创建断言、回滚和 GDB 通过；正式树在该阶段缺 `cpu_idle`，尚未链接，不以隔离结果替代整组评分。 | [B 原始串口](docs/logs/b-create-20261113-094247/isolated-qemu.log) · [隔离补丁](docs/logs/b-create-20261113-094247/isolation.patch) |
| C 调度回归 | 两个真实线程各 100 轮，`s0–s11` 与私有栈哨兵保持，`satp` 匹配当前线程；通过睡眠跳过、回绕自选、SIE 和 idle 兜底检查。兜底用例核对了选择目标，没有验证从该次 idle 切换再唤醒。 | [C 回归输出](docs/logs/c-schedule-20261114-150209/run.log) |
| C 联合评分 | 保留教师评分逻辑，在 C 环境转接真实控制台输出后，PMM、创建、调度共 30/30。 | [C 评分](docs/logs/c-grade.log) |
| A 联合复测 | 普通构建和启动成功，直接运行教师 `make grade` 得 30/30，调度回归脚本返回 PASS，另有页映射与首次切换的手动取证。 | A 联合日志目录见下表。 |

A 在 `36412b6` 上的后续记录位于 `docs/logs/a-integrated-20261115-124341/`。该目录来自后续 WSL 复测，不是前一阶段的交接压缩包内容。

| 文件 | 用途 |
| --- | --- |
| `commit.log`、`qemu-version.log` | 记录测试源码版本和 QEMU 版本 |
| `build.log`、`run.log`、`restored-build.log` | 普通构建、启动原始输出及评分后恢复普通镜像 |
| `grade.log`、`schedule-check.log` | 直接教师评分与调度回归结果 |
| `a-pte-ref-gdb.log` | 两个断点处的 PTE、PPN 及引用计数变化 |
| `first-switch-run.log` | 自动脚本命中 `switch_to` 后因该 GDB 没有 Python 支持中止；成功结果来自后续手动 GDB 会话 |

普通启动中的关键输出：

```text
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.
++ setup timer interrupts
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

随后进入 `do_exit()` 的 `process exit!!.` 占位 panic 和调试监控器。这是线程函数已经返回并走完本实验目标路径的证据，**不表示普通启动自然关机或线程资源已回收**。独立测试驱动调用 SBI 关机、教师评分完成及普通内核进入 monitor 是不同结果。

## 8. 功能规格、AI 辅助与实际迭代

下表归纳实验中的规格与提示词约束，不作为原始对话逐字稿。B、C 的完整整理分别保存在 [B 创建规格](docs/prompts/b-process-create.md) 和 [C 调度规格](docs/prompts/c-scheduling.md)，A 的提示词整理在个人报告中。

| 模块 | 编码前要求与验收重点 | 实际处理 |
| --- | --- | --- |
| A：内存与页表 | 保留原分配器接口和教师自检，移植 Best-Fit；新增合法 4 KiB 映射，规定替换、撤销、引用计数与回滚。 | AI 提供参考补丁与检查步骤；A 在 WSL 检查、应用和验证。遇到 `proc_init`、`cpu_idle` 未实现的链接阻塞，用独立工作树跑到 `vmm_init()` 返回，正式分支不加空实现。 |
| B：线程创建 | 限定功能二区域；现场完整后才登记和唤醒；每个错误分支保持资源、计数和中断状态一致。 | 在临时副本使用教师原断言和自建探针，区分真实页耗尽与合成失败。阶段内六个函数的独立运行通过；没有虚构一次“先运行失败、再修复”的代码问题。 |
| C：调度与切换 | 按已有 PCB/现场布局实现扫描和切换，保留 A/B 的实现及教师脚本，验证首次进入、重复切换与边界。 | 跨分支残留的依赖文件引用已移除头文件，`make clean` 后恢复。C 环境初次评分丢失输出，确认是 GDB remote console 的采集问题；仅转接真实输出后重跑教师评分。A 的 QEMU 4.1.1 环境无需适配。 |

还保留了框架中 `kmalloc.c` 的地址参数类型警告，以及教师调度测试中的函数指针类型警告；没有通过改动教师断言来消除警告或获取评分。对于自动 GDB 脚本的失败，A 换用同一 ELF 的 GDB 原生命令重新取证，避免把工具能力不足误判为切换失败。

三项可复用的提示词摘要（根据实际任务整理）：

> **A：** 对照 Lab4 的头文件和自检迁移 Lab2 内存代码。说明 `create=0` 的无副作用要求、同页更新与换页时的引用差异。若未接入进程和调度，给出隔离验证步骤，并标明哪些结果仍需联合复测。

> **B：** 在现有 PCB、`context`、`trapframe` 布局下补齐功能二。说明线程何时能被调度器看到，以及 PCB、栈或 `mm` 阶段失败后分别归还什么。把注入测试放在临时副本，保留教师断言与功能三区域。

> **C：** 在共享根页表和单 hart 的前提下实现选线程、写 `satp` 与切换现场。用真实切换核对首次入口、参数和重复恢复，区分控制台采集失败与内核断言失败；保留框架退出占位与教师评分逻辑。

## 9. 与操作系统原理的联系及实验边界

本实验中的 PMM 管理物理页，PTE 管理 VA 到 PA 的翻译和权限，VMA 则是软件对一段虚拟地址范围的描述。新增一张页表与为用户按需分配一个数据页是不同的操作。线程可以共享同一页表，却各有 PCB、内核栈和执行现场；调度切换的是执行现场，处理 trap 保存的是可能在任意指令边界被打断的现场，这也解释了两个结构为何不同。

最终实测覆盖了内存初始化、映射替换、创建与失败回滚、首次运行及多次协作切换，教师的三个检查项均通过。当前实现针对单 hart、共享内核地址空间的教学环境，未实现用户进程地址空间复制或 COW、按需分页与置换、时间片抢占、多核同步、完整退出和资源回收。QEMU 中的验证不代表已在真实开发板上验收这些路径。

[riscv]: https://docs.riscv.org/reference/isa/v20240411/_attachments/riscv-privileged.pdf "RISC-V Privileged Architecture：Supervisor-Level ISA、Sv32、Sv39、Sv48"
