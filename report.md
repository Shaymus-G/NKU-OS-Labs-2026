# 实验五：用户程序与进程生命周期

- 仓库分支：`lab5`
- 报告日期：2026 年 11 月 27 日（Asia/Shanghai）
- 联合验收源码基线：`a7295ce9872cde2dc5dc0964b006b703a167aa54`
- 本机教师评分：**130/130**；COW 与逐页复制各 8 项回归通过，真实用户 exec/非法系统调用补充用例两种模式均通过。
- 已完成本机真实 U→S→U、客体 GDB，以及同一 QEMU 进程上的宿主/客体双重 GDB 与 TCG 观察。

本文以当前 Lab5 源码和原始证据为依据。参考了其他分支报告的章节形式，没有沿用其测试结论。旧根目录 Lab4 报告归档为 [lab4-report.md](docs/archive/lab4-report.md)，其中 30/30 不属于本实验。A/B 早期文档是阶段快照，最终联合结果以本报告与所链接的本轮日志为准。

## 1. 实验目标、分工与环境

Lab5 把前序的内存、中断和内核线程连接起来：系统建立用户地址空间，装载 ELF，第一次从 S 态进入 U 态；用户通过 ecall 请求服务，内核完成 fork、exec、wait、exit、yield、kill，再返回用户或切换进程。

| 成员 | 负责内容 | 个人资料 |
| --- | --- | --- |
| 国峻赫，2412082（A） | 内存基础、trap/系统调用、用户库接口，双重 GDB 任务牵头 | [A 阶段报告](docs/parts/a-syscall-trap.md) |
| 陈铭宇，2412559（B） | PCB/首次现场、ELF/exec、wait、Challenge 2、总报告；本轮补齐本机联合验收和双重 GDB | [装载器阶段报告](docs/parts/b-user-loader.md)、[联合验收](docs/parts/b-integration.md) |
| 尚文轩，2412456（C） | 调度、fork/复制、退出控制、COW、联合回归修复 | [生命周期](docs/parts/c-fork-lifecycle.md)、[COW](docs/parts/c-copy-on-write.md) |

三人共享 proc.c，按函数增量接入。B 的 [接口说明](docs/notes/lab5-b-interfaces.md)记录了 copy_thread/do_fork、mm 引用和 exit/wait 的所有权；C 的 [交接](docs/notes/lab5-c-handover.md)说明了后续接入与修正。早期接口文档中“不支持 COW”等措辞只描述当时版本，当前默认支持 COW。

| 项目 | B 本机本轮验收 | C 已提交的交叉环境证据 |
| --- | --- | --- |
| GCC | SiFive GCC 10.2.0 | GCC 10.2 |
| QEMU / OpenSBI | 4.1.1 / v0.4 | 6.2 / 1.3 |
| 客体 / 宿主 GDB | 10.1 / 15.1 | 见 C 环境记录 |
| 平台 | WSL Ubuntu，virt，单 hart，128 MiB | WSL，virt，128 MiB |
| 教师评分 | 直接 make grade，130/130 | 串口适配器运行，初测/复测 130/130 |

本轮 [版本及源码哈希](docs/logs/b-integration-20261127/manifest.json)与 C 的 [环境记录](docs/logs/lab5-c/environment.log)分别保留，不把两台环境的结果混成一次运行。

## 2. 整体实现顺序与运行链

先导入教师 Lab5 框架，A 补内存、页映射与用户态 trap 基础；B 和 C 固定创建/现场、地址空间、退出/等待接口后分别实现。B 先在隔离副本验证装载与回滚，C 接入真实创建、调度和生命周期后联通用户程序，再实现 COW 和集中验收。本机最后复测并汇总报告。

```text
kern_init → PMM/VMM/trap → proc_init
  → idleproc → schedule/proc_run/switch_to → initproc
  → kernel_thread(user_main) → kernel_execve
  → do_execve/load_icode → 新页表、用户栈和 trapframe
  → forkrets → __trapret → sret → 用户 _start/umain/main
  → 用户库 ecall → __alltraps → trap/syscall → do_*
  → 恢复现场返回 U 态，或调度其他进程
  → do_exit 置僵尸 → 父进程 do_wait 最终回收
```

三个“现场”承担不同职责：context 只保存内核切换需要的 ra/sp/s0～s11；trapframe 保存用户/内核异常返回需要的完整寄存器和 CSR；mm/页表决定地址与权限。切换 context 不自动等于替换 mm，也不自动完成用户态返回。

## 3. 功能一：内存基础、trap 与系统调用

### 3.1 用户地址空间的基础

page_init 从 DTB 得到物理内存范围，在内核映像之后建立 Page 描述符数组，再将剩余页交给真实 First-Fit 管理器。保留教师 basic_check/default_check，并使用 default_pmm_manager；没有把 Best-Fit 仅改名成 First-Fit。

page_insert/page_remove 负责映射与页引用：同页重映射不能重复增加引用，替换旧映射需要减少旧页引用，最后引用消失才释放。exit_mmap 先撤销 VMA 中的数据页映射，再通过 exit_range 回收空页表。B 修正后，只有上级目录全部 512 项都空才释放它，避免局部范围清理误伤其他存活页表或留下空目录。

VMA 表示内核认可的合法区间与语义权限，PTE 表示实际映射与硬件权限，两者都需要检查。print_pgdir 能区分 Sv39 大页叶项与下级表，并限制输出长度；它是实验调试接口，不是通用安全信息查询接口。

### 3.2 保存与恢复：两套栈

每个用户进程有用户栈和私有内核栈。sscratch 在用户执行期间保存内核栈地址，内核执行期间为 0。SAVE_ALL 首先交换 sp/sscratch，判断原来来自 U 还是 S；将现场保存为 36 个 8 字节槽位，共 288 字节，并将 sscratch 清零以支持内核嵌套 trap。

RESTORE_ALL 从保存的 SPP 判断返回模式。返回 U 态前将 tf+288 写入 sscratch，恢复 sstatus/sepc 和其他寄存器，最后恢复 sp，再执行 sret。sp 必须最后恢复，因为此前仍以它索引当前 trapframe。

### 3.3 请求分发与返回

本框架将调用号放在 a0，参数放在 a1～a5，返回值仍放在 a0。用户库使用固定五参数，避免无参数调用仍读可变参数，以及把 64 位结果写进 32 位 int 对象的旧问题。

exception_handler 对用户 ecall 先将 epc 加 4，再进入 syscall；分发表检查编号范围与空表项，非法号返回 -E_INVAL。sys_* 仅转换统一参数并转交相应 do_*，用户地址安全和资源语义由底层负责。成功 syscall 返回后，用户应从 ecall 的下一条指令继续。

COW store fault 与 ecall 不同：写缺页处理成功必须保留 sepc，返回后重试原 store。时钟中断重新设置时钟事件，并由 sched_tick 使用四 tick 时间片请求轮转；trap 在返回用户态前调度并重新检查 PF_EXITING，避免已被 kill 的进程恢复用户执行。

## 4. 功能二：第一个用户进程、ELF 与 wait

### 4.1 PCB、启动链与首次返回

alloc_proc 将 PCB 清零，设置 UNINIT、pid=-1 和启动页表；exit_code/wait_state/父子兄弟指针均初始化。proc_init 建立 idle 和 init，idle 不进入普通链表/哈希但计入进程数。普通启动中 idle=0、init=1、首个用户进程=2。

kernel_thread 将入口/参数放入 s0/s1，通过 do_fork 复用创建流程。copy_thread 将 tf 放在内核栈顶下方 288 字节，设置 context.ra=forkret、context.sp=tf。内核线程初始 sp=tf，顶部返回现场得以保留；用户 fork 在 stack=0 时继承原用户 sp，子返回 a0=0。

init_main 创建 user_main 并等待全部后代；user_main 通过嵌入符号选取 ELF。kernel_execve 在关中断状态调用 do_execve，成功后直接 forkrets(current->tf)，不再返回旧 C 调用栈。只调用 do_execve 并不能使 CPU 自动进入 U 态，必须实际走到 sret。本机已在用户 _start 停住并验证特权级。

### 4.2 ELF64 装载

load_icode 接受 ELF64、小端、RISC-V 静态可执行映像。装载前检查头与程序段表、文件与地址边界、整数溢出、filesz≤memsz、对齐、权限、页取整后的段重叠，以及入口是否落入可执行 PT_LOAD。

每个段先建立 VMA，再分配页、清零整页，最后复制对应文件字节。因此未被文件覆盖的 BSS、页首/页尾不会残留旧数据。代码/数据页按 ELF 标志设置 U/R/W/X。用户栈 VMA 为 USTACKSIZE，底部保留一个未映射保护页，其余 255 页提前分配且不可执行；本实验没有通用按需缺页，不能只声明栈区却假定缺页会自动补齐。

首次用户 tf 的 EPC 来自 e_entry，SP=USTACKTOP，SPP=0、SPIE=1、SIE=0。入口取 ELF 而非固定猜测；本次 hello 的实际入口为 0x800020。

### 4.3 exec 的所有权与用户输入

do_execve 区分可信内核线程参数和用户缓冲区，名称长度为 1～15。用户输入同时检查 VMA 和实际叶页权限，再通过页的内核别名复制，不依赖全局开启 SUM，也不直接解引用未经检查的用户指针。

旧 mm 保持有效时先构建完整新 mm。失败释放新资源，保留旧 mm/tf/satp/名称；成功才提交新现场与新页表，刷新 TLB/指令缓存，再递减旧 mm 引用。共享旧 mm 仍有引用时保留，归零才销毁。这也防止用户 ELF 缓冲区在尚未复制时随旧地址空间失效。

本轮增加了真实用户 exec：fork 后子进程先提交损坏映像并验证失败返回，再从用户缓冲区提交一份最小合法 ELF，新程序实际执行 SYS_exit(42)，父进程 wait 得到 42。此证据比单独调用内核装载函数更进一步。

### 4.4 wait 与最终回收

pid=0 等待任意子进程，正 PID 等待指定子进程，负 PID/非子进程/无匹配子进程返回 -E_BAD_PROC；这是当前用户库和教师测试的约定。成功返回 0，退出码通过可选指针返回。

等待者在关中断区完成“检查子进程→登记 SLEEPING/WT_CHILD”，释放 mm_lock 后保持关中断进入 schedule，醒来恢复原中断状态并重查条件。这样避免检查与睡眠之间丢失唤醒。PF_EXITING 使等待者转入 do_exit。

僵尸只由其父进程在 do_wait 中 unhash/remove_links，并释放内核栈和 PCB。输出地址先检查整个范围，跨页存储不会写一半才发现后页无效；COW 页先完成相应私有化，再复制退出码。任何失败都保留僵尸供重试。

## 5. 功能三、四：复制、调度与退出控制

### 5.1 fork 与内存复制

do_fork 依次获得 PCB、内核栈、mm 和首次现场，资源齐备后才分配 PID、登记哈希/父子关系并唤醒；失败回滚尚未发布的资源。set_links 已维护普通链表与 nr_process，不能再重复登记。

copy_mm 对 mm=NULL 的内核线程复用启动页表；CLONE_VM 共享同一个 mm 并增加 mm_count；普通用户 fork 创建新 mm/页表/VMA。mm_count 统计共用同一地址空间的进程数，Page.ref 统计映射同一物理页的引用，二者不能混用。

dup_mmap 先登记 VMA，以便中途失败时完整清理部分映射。copy_range 遍历页，跳过未映射空洞和栈保护页；默认 COW，也可通过 LAB5_EAGER_FORK 构建成逐页复制。

### 5.2 轮转与退出

schedule 从当前进程之后环形寻找 RUNNABLE，没有可运行者时选择 idle。proc_run 更换 current，在地址空间变化时写 satp 并刷新 TLB；switch_to 保存/恢复内核上下文，恢复后还原调用时的 SIE。四 tick 时间片使忙循环也能被抢占。

do_exit 切离用户页表，减少并按需释放 mm，记录退出码和 ZOMBIE，唤醒等待父进程，把子进程转交 init，最后调度且不再返回。自己的内核栈仍在使用，必须留给父进程 wait。do_yield 请求调度；do_kill 设置退出意图并唤醒可中断等待者，禁止终止 idle/init。init 始终保持存活，以接管孤儿。

### 5.3 联合回归修复

C 的运行发现 forktest 虽通过用户断言，但 init 的页数比较失败：SLOB 留存整页空闲堆内存。修复为只在页内合并空闲块，整页空闲后移出链表归还 PMM。真实耗尽测试又暴露 First-Fit 尾部插入后未退出、重复处理新节点的问题；初始化和释放路径均补齐终止条件。

教师脚本在 readline 停机，故仅 DEBUG_GRADE 构建在成功回收后进入现有监视器。普通镜像继续调度，没有把“出现预期文字”作为主动关闭内核的实现。原教师测试及评分匹配条件没有修改。

## 6. Challenge

### 6.1 COW 写时复制及状态转换

普通 fork 创建独立页表和 VMA，对原可写页共享物理页、清 W、置 PTE_COW。只有完整建立子映射后才统一撤销父写权限，故失败清理不会留下半提交的父权限。原生只读页可以共享，但不能借 COW 变为可写。

| 状态 | 事件 | 结果 |
| --- | --- | --- |
| 私有可写 | fork 成功 | 父子共享，W=0/COW=1，引用增加 |
| COW 多引用 | 再次 fork | 增加映射引用，继续只读共享 |
| COW 多引用 | 用户写或内核输出写回 | 分配/复制页，写方私有可写，旧引用减少 |
| COW 单引用 | 写入 | 原页恢复 W、清 COW，无需新页 |
| COW 多引用 | 分配失败 | 原映射及引用不变；用户异常退出，wait 输出失败可重试 |
| 原生只读/未映射 | 写入 | 拒绝，不授予写权限 |
| 任意映射 | 解除/退出 | 引用减少，最后引用消失时释放 |

resolve_cow_write 在 mm_lock 下同时检查 VMA 和 PTE，用户 store fault 成功后重试原指令。内核别名写绕过用户 PTE 的 W 位，所以 do_wait 的复制也必须显式处理 COW；本机跨页退出码用例已验证父子隔离。真实耗尽下还验证了多引用拆页失败保持原状、恢复一页后拆分成功、单引用无可用页仍能原地恢复，以及最终引用与页数恢复。

Dirty COW 是 Linux 私有只读映射 COW 处理中的竞争条件，可让本地非特权用户获得不应有的写访问。[Red Hat 原厂说明](https://www.redhat.com/en/blog/understanding-and-mitigating-dirty-cow-vulnerability)支持这一背景。本实验缺少相应文件映射、页缓存和页固定接口，也不是 Linux 的并发路径，因此不能直接复现该 CVE。我们验证的是相近的错误条件：原生只读页不能被升级、内核别名写必须先 COW、失败不能破坏映射、引用释放一致。解决思路是权限复核、正确持有页引用，并对映射/失效过程同步；没有声称复现或修复 Linux Dirty COW。

### 6.2 用户程序何时被预先加载

Makefile 先用 tools/user.ld 链接用户 ELF，再通过内核链接命令的 --format=binary 把 ELF 文件字节嵌入内核镜像；KERNEL_EXECVE 使用链接器 start/size 符号。因此引导装入整个内核镜像时，用户 ELF 字节已经随镜像进入物理内存。

直到 user_main/kernel_execve 选择程序后，load_icode 才解析 ELF，分配该进程的用户页、建立页表、复制 PT_LOAD、清零 BSS 并设置入口。“文件字节在内存中”和“用户地址空间已经装载”是两个阶段。

常用操作系统通常从文件系统取得可执行文件，还可结合文件映射与按需缺页。本实验暂时没有完整的文件装载基础设施，嵌入可以集中研究权限、地址空间与生命周期；代价是程序集合固定、内核镜像变大，更新用户程序通常需重建镜像。

## 7. GDB、QEMU 与 TCG 观察

### 7.1 客体 U→S→U

本轮在首条用户指令停住，观察 pc=0x800020、sp=0x80000000；用户 putc 的 ecall 位于 0x8000aa，调用号 30。进入内核时 scause=8，sepc 仍指向 ecall；内核分发时 tf.epc 已变为 0x8000ae。sret 后在用户 0x8000ae 继续，a0=0。[客体 GDB 原始证据](docs/logs/b-integration-20261127/guest-gdb.log)

内核和用户 ELF 必须分别加载符号；make gdb 只加载内核，并不自动知道用户库的源文件和函数。这些地址来自实际构建，复现应重新解析 ELF/反汇编。

### 7.2 同一进程的双重 GDB

本机安装 QEMU 已 strip，但构建目录保留未剥离文件。两者 Build ID `43a57e28ba870726acd7b94ba70b3c61d20e36f5`、代码段哈希一致，实际调试的源码和可执行文件匹配。[身份记录](docs/logs/b-dual-gdb-20261127-attempt5/identity.json)

宿主 GDB 观察到 trans_ecall 的 ctx.pc_next=0x8000aa，调用栈经过 TCG 翻译；执行时异常 helper 收到 cause=8/priv=0，riscv_cpu_do_interrupt 完成后 priv=1、sepc=0x8000aa、PC=0xffffffffc0200e00。返回阶段观察到 trans_sret 与 helper_sret，helper 前 priv=1，结束后 priv=0，返回 PC=0x8000ae，客体随后实际停在该用户地址。[双重摘要](docs/logs/b-dual-gdb-20261127-attempt5/summary.log)、[宿主原始日志](docs/logs/b-dual-gdb-20261127-attempt5/host-mi.log)、[客体原始日志](docs/logs/b-dual-gdb-20261127-attempt5/guest-mi.log)

QEMU 4.1.1 中，trans_ecall 生成异常 TCG 操作；生成代码执行后进入 helper，再由中断投递更新模拟 CSR/PC/特权级。trans_sret 生成返回 helper 调用，运行时恢复特权级并将 sepc 作为目标。翻译阶段与执行阶段是两件事，翻译函数命中不等于指令已经执行。

TCG 将客体指令转换为中间操作，再生成缓存的宿主机器代码。一次块翻译可服务多次执行，单步会影响块生成，所以设置宿主断点时要考虑缓存与单步状态。Lab2 的地址翻译观察也运行在这套动态翻译机制之上，但软件 TLB 与 TCG 块缓存不是同一种缓存。逐源码解释和复现见 [本轮双重 GDB 报告](docs/parts/b-integration.md)。

本机模拟器较旧，具体 mstatus 数字按该版本配置解释；通用 sstatus/SRET 含义以 [RISC-V Supervisor ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)为准，不能用旧模拟器数值替代现行规范。

## 8. 验证、复现与原始证据

### 8.1 结果与证据层次

| 检查 | 本次或历史结果 | 原始证据 |
| --- | --- | --- |
| B 阶段 0～4 隔离 | 15 类非法 ELF、268 个失败位置、真实耗尽、装载与 wait；使用显式 C 接口替身 | [历史日志](docs/logs/b-isolated-20261126-211915/qemu.log) |
| 本机干净构建/普通 hello | 构建返回 0，真实 hello 与 init 回收成功 | [构建](docs/logs/b-integration-20261127/build.log)、[启动](docs/logs/b-integration-20261127/hello-qemu.log) |
| 本机真实生命周期 | COW 8/8，逐页复制 8/8，无调度/fork/exit 替身 | [汇总](docs/logs/b-integration-20261127/suite.log)、[COW 专项](docs/logs/b-integration-20261127/suite/cow-c_lifecycle.log)、[逐页复制专项](docs/logs/b-integration-20261127/suite/eager-c_lifecycle.log) |
| 本机用户边界 | 非法 syscall、exec 失败保留及成功运行新 ELF，COW/eager 均通过 | [COW](docs/logs/b-integration-20261127/cow-boundary.log)、[eager](docs/logs/b-integration-20261127/eager-boundary.log) |
| 本机教师评分 | 原样 make grade，13 个用户程序及注入检查，130/130 | [评分](docs/logs/b-integration-20261127/grade.log) |
| 本机客体 GDB | 用户入口、ecall、分发、sret、用户继续通过 | [日志](docs/logs/b-integration-20261127/guest-gdb.log) |
| 本机双重 GDB | 同一 QEMU 的 TCG 翻译与运行时特权切换通过 | [摘要](docs/logs/b-dual-gdb-20261127-attempt5/summary.log) |
| C 的独立环境 | QEMU 6.2，COW/eager 16 项、教师初测/复测 130/130 | [专项](docs/logs/lab5-c/suite.log)、[评分复测](docs/logs/lab5-c/grade-confirm.log) |

教师注入的 syscall 检查使用合成 trapframe，本身不能证明真实 U 态；这一点由用户测试和 GDB 证据补充。专项 c_lifecycle 额外建立测试线程，因此该副本用户 PID 可为 3，不与普通 hello 的 PID=2 混淆。31548 等空闲页数依赖具体镜像，应比较同次测试前后恢复，而非硬编码到其他版本。

普通 hello 与专项在看到 init check memory pass 后由观察脚本终止 QEMU；教师评分在 DEBUG_GRADE 的 readline 停点采集并结束；双重 GDB 在用户返回后结束调试。退出方式分别记录，没有将人为/工具终止写成内核自行关机。

构建原始日志保留了 testbss 故意越界访问的编译警告，以及 WSL/NTFS 上小于一秒的文件时间戳偏差与 clock skew 提示。本次由 make clean 开始完整编译，随后对实际镜像执行了用户程序、评分和 GDB 验证，没有仅凭 make 返回成功判断运行正确。

### 8.2 复现

在 WSL 工程根目录使用已验证工具链：

```bash
export PATH=/mnt/d/3nd_grade/OS/environment/riscv-toolchain/bin:/mnt/d/3nd_grade/OS/environment/qemu/bin:$PATH
unset QEMU
make clean
make -j4
make qemu
```

普通内核会保持运行；看到结果后退出 QEMU，再运行评分或其他构建。自动复验：

```bash
python3 tools/verify-lab5-b.py --log-dir /tmp/lab5-b-integration
python3 tools/verify-lab5-b.py --boundary-only --log-dir /tmp/lab5-b-integration
python3 tools/lab5-dual-gdb.py \
  --qemu /mnt/d/3nd_grade/OS/environment/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64 \
  --log-dir /tmp/lab5-b-dual
```

首条脚本会 clean、构建、正式 hello、专项、评分、重建及客体 GDB；第二条在临时树增加用户边界程序；第三条需要匹配符号和源码的 QEMU。脚本不把测试程序/钩子写入正式启动路径。评分会清理构建产物，不要与 GDB 同时执行。逐页复制可用 make DEFS=-DLAB5_EAGER_FORK，从 clean 开始并保持后续构建标志一致。

## 9. 功能规格、AI 协作与实际迭代

### 9.1 Spec 与 prompt

以下是按本实验实现整理的规格摘要，不冒充完整原始聊天。B/C 的可追溯记录分别为 [B 规格](docs/prompts/b-user-loader.md)、[C 规格](docs/prompts/c-lab5.md)；A 的 Lab5 实施和真实问题见其阶段报告。本轮用户实际要求继续完成阶段 5～6，并仅参考其他分支 report.md 的格式与结构。

| 功能 | 规格中的依赖与保证 | 给 AI 的约束重点 |
| --- | --- | --- |
| 系统调用 | 依赖 tf/用户寄存器约定；保证编号检查、ecall+4、返回值与异常隔离 | 结合用户库核对 a0/a1～a5，不套用 Linux 的 a7 约定；保留 Lab5 U/S 栈入口 |
| 第一个用户进程 | 依赖 PMM/VMA/ELF/context；保证新 mm 完整才提交、失败回滚、真正 sret | 适配新增 PCB 字段；内核/用户参数分开；检查 ELF 和指针，不覆盖 C 函数 |
| 用户复制 | 依赖 mm_count/Page.ref/父子链；保证子返回 0、父返回 PID、失败不发布 | 先登记清理范围；保留 guard/只读页；COW 与逐页复制均提供真实验证 |
| 退出控制 | 依赖调度与 wait；保证僵尸、唤醒、孤儿接管、最终回收一致 | 当前栈不能自行释放；检查睡眠竞态、kill 打断及重复 wait |
| 双重 GDB/报告 | 依赖匹配 QEMU 符号与用户 ELF；保证观察同一模拟进程 | 保存原始日志，区分翻译/执行、历史/本次和隔离/真实用户态；不编造通过结果 |

### 9.2 实际问题与修正

1. **用户库宽度与参数问题。** A 将固定五参数及寄存器宽度修正，后续实际用户运行和本轮非法号测试验证了返回路径。
2. **回滚与页表存活范围。** B 修正 exit_range，增加跨范围存活页表与空闲页恢复检查；不是仅看到装载成功就结束测试。
3. **故障注入覆盖不足。** B 早期 --wrap 没有拦截 pmm.c 内部调用，只覆盖 5 个位置。失败日志保留；最终仅在临时树重命名真实分配入口，覆盖 268 个失败位置。
4. **用户断言通过却未恢复内存。** C 进一步检查 init 页数，修复 SLOB 整页归还；真实耗尽暴露并修复 First-Fit 尾部重复插入。
5. **评分采集与调度。** DEBUG_GRADE 到 readline；C 的 OpenSBI 1.3 环境另有串口/GDB 转接，本机 v0.4 直接评分通过。四 tick 轮转保留抢占并减少逐字符输出交织。
6. **本机 GDB 兼容。** 读取 satp 遇 E14，U 态设置内核地址断点遇不可访问；改为监视器/保存现场、先单步 ecall 进入 S 态后再设置内核断点，sret 也单步返回。未据此修改内核语义。
7. **宿主通道与符号。** QEMU -nographic 与 GDB 抢读输入、first_cpu 宏没有调试符号、U 态过早读 sret 编码等问题均实际出现并修正。失败 attempt1～4 与成功 attempt5 均保留，详细过程见双重 GDB 报告。

## 10. 与原理课程的联系及实验边界

| 原理概念 | 本实验体现与差异 |
| --- | --- |
| 特权级与系统调用 | ecall 触发受控入口，trap 保存现场，sret 恢复；调用号寄存器是本框架约定 |
| 进程与线程 | PCB/私有内核栈/context 表示执行实体；CLONE_VM 可以共享 mm，但不是完整 POSIX 线程模型 |
| 虚拟内存与保护 | VMA 管合法区间、PTE 管映射/权限；Sv39/TLB 与页引用协同，不含通用换页和按需文件装载 |
| 调度与同步 | 单 hart 四 tick 轮转、SIE 临界区和 mm_lock；不包含多核调度、跨核 TLB shootdown |
| 进程生命周期 | fork/exec/exit/wait 分别创建、替换、结束、回收；孤儿接管与僵尸回收不是同一动作 |
| COW | 独立地址空间暂共享物理页，通过写缺页/内核写回拆分；页引用与 mm 引用分层管理 |
| 执行与模拟 | 客体硬件行为由 QEMU 软件实现；TCG 翻译缓存与硬件地址翻译/TLB 分别讨论 |

仍未覆盖的机制包括文件系统可执行文件装载、动态链接、argc/argv 栈构造、交换/页面置换、多核并行、完整信号机制、权限凭据和设备驱动。外部中断分支没有完整 PLIC claim/complete，不能据分支存在称其已实现。课程 get_pte 还保留非叶 PTE 的 U 位约定，与现行 Sv39 的保留位要求存在可移植性限制；两种 QEMU 环境通过不等于所有 RISC-V 实现均接受。SLOB 的统计接口仍不能作为精确堆对象计数，因此本报告用实际物理页恢复和可观察生命周期作证据。

本轮完成的是现有课程环境中的功能、Challenge、真实用户态与双重调试验收；没有把课程内核描述成通用生产操作系统。
