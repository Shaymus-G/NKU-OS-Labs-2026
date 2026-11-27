# Lab5 个人实验报告：内存基础、Trap 与系统调用

本文记录本人在 Lab5 中负责的内存基础、Trap 和系统调用实现，以及联合版本的验证结果。实现部分以本人提交 `e3dce38`、`6e0f462` 为依据；联合版本的进程和内存管理逻辑由组员继续实现和完善。各测试的具体版本以对应日志记录为准。

- 负责人：国峻赫（学号：2412082）
- 分支：`lab5`
- 公共基础提交：`e3dce38`，`feat(lab5): port memory and trap entry foundations`
- 系统调用与隔离测试提交：`6e0f462`，`feat(lab5): add syscall dispatch and isolated memory checks`
- 环境：WSL Linux，RISC-V 交叉工具链，QEMU 4.1.1，OpenSBI v0.4；隔离测试使用 QEMU virt、128 MiB 物理内存。

## 1. 实验目标与负责范围

Lab4 已能在内核中创建和调度内核线程。Lab5 的总体目标是在此基础上建立用户进程：加载用户程序、构造用户地址空间和用户态执行现场，通过 `sret` 进入 U 态；用户程序通过 `ecall` 请求内核服务，并通过进程复制、调度、等待和退出组成基本的进程生命周期。

本人实现的 A 部分侧重为该过程提供底层支撑：

1. 将前序实验的物理页分配、物理内存初始化和页映射基础适配到 Lab5。
2. 在 Lab5 已有 trap 保存入口基础上补充返回路径，使设计覆盖从 U 态和 S 态进入 trap 的情况。
3. 实现系统调用分发和包装，适配用户态的系统调用寄存器约定。
4. 提供调试用页表遍历，并在独立副本中检验当前可运行的内存基础。
5. 在联合版本具备真实用户态路径后，继续完成 U/S 态往返、双重 GDB、QEMU 源码跟踪和报告更新。

本人负责和适配的主要文件如下：

| 文件 | 本人工作 |
| --- | --- |
| `kern/mm/default_pmm.c` | 适配 Lab2 First-Fit，保留 Lab5 自带自检 |
| `kern/mm/pmm.c` | 适配 `page_init`、`page_insert`、`page_remove`；增加 `print_pgdir` |
| `kern/trap/trapentry.S` | 基于 Lab5 `SAVE_ALL` 补充 `RESTORE_ALL`、进入 C 的 trap 处理 |
| `kern/trap/trap.c` | 补充时钟中断、用户态 ecall 和异常处理的基础逻辑 |
| `kern/syscall/syscall.c` | 实现系统调用表、分发与 `sys_*` 包装 |
| `user/libs/syscall.c` | 调整用户态参数传递与返回值保存 |

本人的工作为用户程序运行提供物理内存、异常入口和系统调用基础；用户程序加载、进程创建与控制、调度和地址空间复制由组员负责。全组完成集成后，我对联合版本进行了启动、评分、专项回归和双重 GDB 验证，结果见第 10 节。

## 2. 背景与设计思路

### 2.1 用户态为何需要 trap 和系统调用

用户程序在 U 态运行，不能直接执行只允许内核完成的资源管理操作。程序通过 `ecall` 发出请求，硬件按照异常配置进入更高特权级，记录异常原因和返回地址；内核入口再保存寄存器，识别请求并调用相应的内核函数。服务完成后，通过 trap 返回恢复现场，并由 `sret` 恢复执行。

本项目采用的约定为：用户态 `a0` 存系统调用号，`a1`～`a5` 存参数，返回值写入保存现场中的 `a0`。这是当前实验框架的约定，应结合用户库和内核代码共同理解。

以 `putc` 为例，系统调用的主要路径为：

```text
用户程序 ecall
  → __alltraps / SAVE_ALL
  → trap(tf) → trap_dispatch(tf) → exception_handler(tf)
  → syscall() → sys_putc() → cons_putc()
  → RESTORE_ALL → sret → 用户程序继续执行
```

联合版本中，用户程序的实际执行以及一次 `putc` 系统调用的用户态与内核态往返已分别通过运行结果和 GDB 现场验证，见第 10 节。

### 2.2 用户地址空间与物理内存

用户程序加载和 fork 需要分配物理页、建立虚拟地址到物理页的映射，并在不再使用时回收。Lab5 采用 Sv39 三级页表：每级使用 9 位索引，每个页表页有 512 个 8 字节页表项，页内偏移为 12 位。不同层级均可能出现叶子映射，对应不同大小的页。

`struct Page` 描述物理页，页表项保存映射与权限信息，物理页的引用计数影响回收时机。复用前序实验代码时，不能整文件覆盖 Lab5 新增的用户内存管理逻辑；尤其保留框架中的 `get_pte()`、`unmap_range()`、`exit_range()` 及地址空间复制接口，供后续进程内存管理集成使用。

## 3. 物理内存基础实现

### 3.1 First-Fit 分配器

`kern/mm/default_pmm.c` 的分配器主体从 Lab2 移植。该实现按空闲链表顺序查找第一个足够大的空闲块，分配后根据需要拆分剩余部分；释放时按地址插入，并检查相邻空闲块是否可以合并。

Lab5 的头文件声明了外部可见的 `free_area`，移植时按当前头文件调整定义。文件末尾使用 `default_pmm_manager` 注册分配器方法；保留 Lab5 文件原有的 `basic_check()` 和 `default_check()`。

采用 First-Fit 的原因是与当前 Lab5 框架的默认管理器及自检契约相匹配；Lab4 使用过的 Best-Fit 则采用不同的空闲块选择策略。

### 3.2 物理内存初始化 `page_init()`

`kern/mm/pmm.c` 中的 `page_init()` 从 DTB 读取物理内存起始地址和长度，检查范围是否合理；再限制管理范围不超过内核支持的直接映射范围，并按页对齐末端。

实现根据可管理页数，在内核映像末尾之后放置 `struct Page` 数组。初始化时先把描述符标记为保留，再计算内核映像与描述符数组之后可使用的连续页区间，调用 `init_memmap()` 交给物理页分配器。代码还检查内存长度加法溢出、描述符空间不足和没有可用页等错误条件。

本次隔离环境中的 DTB 报告范围为 `[0x80000000, 0x88000000)`，初始化结果为 32768 个描述符、31572 个空闲页；该数值与具体内核映像和构建内容有关，不应视为所有版本的固定结果。

### 3.3 页映射插入与删除

`page_insert(pgdir, page, la, perm)` 使用 `get_pte(..., 1)` 查找或创建叶子项所在页表。它区分新映射与替换旧映射，维护物理页引用计数，写入 PTE 并失效相关 TLB；旧页引用归零时释放。

`page_remove(pgdir, la)` 使用 `get_pte(..., 0)` 查找已有项，不因为删除而新建页表；若映射有效，则清除 PTE、失效相关 TLB，减少被映射页引用计数，并在引用归零时释放。

当前实现保留 Lab4 中的权限处理：当传入 `perm=0` 等情况需要形成可读叶子项时补充 `PTE_R`。联合版本中与用户页权限有关的测试包括教师评分中的 `badsegment`、`faultread`、`faultreadkernel`、`pgdir`，以及两种地址空间复制模式的专项回归；这些测试不能覆盖所有可能的权限组合。

### 3.4 `print_pgdir()` 调试输出

根据当前 `satp` 获取根页表，并按 Sv39 层级检查有效页表项；通过 `R/W/X` 位区分叶子项与指向下一级的项，输出虚拟地址、PTE、物理基址和标志。为了防止输出过长，当前最多打印 128 个有效项；输出因此可能截断。

隔离运行时只观察到一个 L2 叶子项：

```text
L2 VA 0xffffffffc0000000 PTE 0x00000000200000cf
   PA 0x0000000080000000 flags 0x0cf leaf
```

这表明**该次隔离启动时**根页表中观察到了从高地址区域映射到 `0x80000000` 的 1 GiB 叶子映射；这一条输出本身不验证用户页表。打印的是大页物理基址，不能把它误写成任意页内地址的完整转换结果。

此接口由 `SYS_pgdir` 暴露为实验调试功能，输出可能包括内核映射信息；不应直接视为适合任意用户使用的生产接口。

## 4. Trap 入口和返回

### 4.1 保存现场与内核栈切换

Lab5 原有 `SAVE_ALL` 利用 `sscratch` 协调用户栈和内核栈：从用户态陷入时切换到预先准备的内核栈；从内核态陷入时继续在内核栈上保存现场。该宏为 trapframe 分配 `36 * REGBYTES` 空间，保存通用寄存器及 `sstatus`、`sepc`、`stval`、`scause` 对应现场。

在 RV64 环境中 `REGBYTES` 为 8，36 个槽位共 288 字节。保存后的 `sp` 指向 trapframe，入口将其放入 `a0` 后调用：

```asm
mv a0, sp
jal trap
```

这里 `a0` 是 C 调用约定中的第一个参数；C 层通过它取得当前 trapframe。

### 4.2 恢复现场和 `sscratch`

`RESTORE_ALL` 先读取 trapframe 中的 `sstatus` 和 `sepc`，依据 `SPP` 判断预计返回的特权级。若返回 U 态，则把 trapframe 上方的栈地址放入 `sscratch`，供下次用户陷入时切换回内核栈；随后恢复控制状态和通用寄存器，最后恢复 `sp`，并由 `sret` 返回。

`sp` 需要最后恢复，因为恢复其他寄存器时仍通过当前 `sp` 索引 trapframe。临时使用的 `t0`、`t1`、`t2` 也在后续从 trapframe 恢复。

该做法依赖 trapframe 与内核栈布局一致。联合版本成功运行用户程序，且双重 GDB 观察到一次 `ecall` 后返回用户态；这为相关现场恢复提供了运行证据，但该次跟踪不能覆盖所有栈布局和嵌套异常情况。

### 4.3 中断与异常分发

`trap_dispatch()` 将 `cause` 按最高位区分为中断或异常。时钟中断处理重新设置下次时钟事件，更新 tick，并在 `current` 存在时设置 `need_resched`；当前框架在返回用户态前检查该标记。实际调度还依赖调度器与进程状态实现；联合评分中的 `spin`、`yield` 和专项回归对相关运行场景进行了检查。

用户态 `ecall` 由 `exception_handler()` 识别，先将 `epc` 前移 4 字节再调用 `syscall()`；否则返回后可能再次执行同一条 `ecall`。对当前尚未处理的用户异常，代码尝试调用 `do_exit(-E_KILLED)`；内核态未处理异常则报告内核错误。进程退出及资源回收由进程管理部分实现，并在联合版本的用户程序和专项回归中进行验证。

A 初始阶段的 `IRQ_S_EXT` 分支仅打印信息，不能据此认定 PLIC 外部中断识别与应答完整。COW 缺页处理由组员在联合版本中实现；两种复制模式的专项回归结果见第 10 节。

## 5. 系统调用实现

### 5.1 用户态封装

老师提供的用户库封装在无参数调用时仍读取固定数量的可变参数，并用 8 字节存储指令保存到 `int` 变量，可能产生未定义行为或覆盖相邻数据。现将公共封装改为固定接收五个参数，未使用的位置填 0；使用与寄存器约定对应的寄存器变量执行 `ecall`，从 `a0` 取得返回值，再转换成接口所需的 `int`。

该方案依赖内核正确恢复用户现场。联合版本的 `hello` 和系统调用相关测试通过；双重 GDB 进一步记录了一次 `putc` 的返回地址、栈指针与返回值。

### 5.2 内核分发

`kern/syscall/syscall.c` 在 `current->tf` 中读取系统调用号及 `a1`～`a5`，检查编号是否在数组范围内且表项非空，再调用对应包装函数，将返回值写回 `a0`。A 初始实现对非法编号返回 `-E_INVAL`；B 在联合版本的用户态边界测试中记录了非法编号返回后程序继续执行的结果，具体覆盖范围见 B 的联合验收记录。

| 系统调用 | 当前包装目标 / 职责 |
| --- | --- |
| `exit` | 调用 `do_exit`，处理进程退出 |
| `fork` | `do_fork(0, 0, current->tf)`，普通复制与现场语义由 C/B 联合实现 |
| `wait` | 将 PID 和退出码指针传给 `do_wait` |
| `exec` | 将 name、len、binary、size 传给 `do_execve` |
| `yield`、`kill` | 调用相应的进程控制函数 |
| `getpid` | 返回当前进程 PID |
| `putc` | 调用控制台输出 |
| `pgdir` | 调用调试页表打印函数 |

`sys_wait()` 和 `sys_exec()` 接受来自用户现场的参数，必须在底层实现中根据来源和权限检查指针、长度、溢出及数据复制。尤其不能将用户传入的地址作为可信内核镜像地址使用。B 的联合版本边界测试覆盖了部分非法 `exec` 参数和合法用户缓冲区场景；这些测试不能替代对所有指针及长度边界的全面审查。

### 5.3 与用户程序装载的衔接

首个用户程序的装载和用户态现场由组员负责。它与本人工作的接口包括用户页表、trapframe、内核栈及返回路径：装载后需要设置用户程序入口和用户栈，并通过 trap 返回路径进入 U 态。联合运行中的 `hello pass.` 及双重 GDB 对首次用户指令和系统调用往返的观察，为这一衔接提供了实际证据。

## 6. 实际验证过程与结果

### 6.1 模块编译

在 Lab5 工程下，使用 `set -o pipefail` 并分别编译物理内存、trap 和系统调用相关目标。多目标编译返回状态为 0；其中输出中的 `up to date` 表示该次无需重新编译，并不构成运行测试。

```bash
make obj/kern/mm/default_pmm.o \
     obj/kern/mm/pmm.o \
     obj/kern/trap/trapentry.o \
     obj/kern/trap/trap.o \
     obj/kern/syscall/syscall.o \
     obj/user/libs/syscall.o
```

此前 `git diff --check` 对工作区源码检查没有输出。用户态 `sys_fork` 的实际反汇编显示 `a0=2`、`a1`～`a5` 为 0，随后执行 `ecall`。该观察不证明陷入后内核分发或用户态返回成功。

### 6.2 独立副本测试

为避免等待未实现的进程链路，测试时从当时 HEAD 导出临时副本，将当时的源码差异补丁应用到副本，并仅在副本中调整 `kern/init/init.c`：完成 `pmm_init()`、`print_pgdir()` 和 `vmm_init()` 后通过 SBI 关闭。正式仓库 `kern/init/init.c` 的 diff 检查无输出。

临时副本完整编译并运行 QEMU，观察到以下输出：

```text
memory management: default_pmm_manager
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.
A isolated check: PMM and VMM initialization returned
```

这说明该隔离镜像中的上述框架检查执行到了成功信息；它没有启动用户进程，也没有运行完整教师评分。

### 6.3 证据位置与复现边界

- [隔离编译日志](../logs/lab5-a-isolated/build.log)
- [隔离 QEMU 运行日志](../logs/lab5-a-isolated/qemu.log)
- [测试时基础提交](../logs/lab5-a-isolated/base-commit.log)
- [测试所用源码差异](../logs/lab5-a-isolated/source-changes.patch)
- [隔离版本启动文件](../logs/lab5-a-isolated/isolated-init.c.txt)

上述相对路径按本文放在 `docs/parts/` 下组织。复现时需同时考虑基础提交、当时的源码补丁和临时启动文件；不能只根据基础提交声称测试过后续全部代码。QEMU 日志中的串口回车符曾导致 Git 空白检查提示，原始日志已保留。

## 7. 实现中的问题与处理

1. **前序代码移植。** 最初自动提取 Lab4 `pmm.c` 函数时，脚本依赖的注释与实际文件不一致，因检查失败而没有写入。随后按函数定义提取 `page_init()`、`page_insert()` 和 `page_remove()`，检查差异并编译。Lab5 原有内存管理代码得到保留。
2. **分配器适配。** Lab4 使用 Best-Fit，本实验从 Lab2 移植 First-Fit，并保留 Lab5 自带分配器自检；隔离启动与联合启动均输出物理页分配自检成功信息。
3. **系统调用参数宽度。** 将用户库改成最多五个显式参数的固定封装，并通过 RV64 寄存器变量传递编号、参数和返回值。联合版本中用户程序及评分测试可以运行，GDB 还记录了一次 `putc` 的返回过程。
4. **独立验证的范围。** 初期通过临时副本先测试内存基础。该副本没有执行完整用户进程链路；集成后再通过普通启动、评分和专项回归补充验证。
5. **宿主条件断点。** 本机双重 GDB 脚本遇到条件断点未命中和 `trans_sret` 重复命中，结合本机反汇编调整临时脚本后完成取证。实际操作和适用范围见第 10.6 节。

## 8. 团队协作与接口

本人负责内存基础移植、trap 入口返回、系统调用分发及相关验证；B 负责用户程序加载等工作，C 负责进程生命周期、调度与内存复制等工作。集成时需要使用户页表、进程 trapframe、内核栈、系统调用参数与退出回收等接口保持一致。最终由组员共同完成的功能及其实现细节，以相应的代码和组员报告为准。

## 9. 实验过程中的认识

用户态支持需要物理页管理、虚拟地址映射、用户与内核栈切换、trapframe、系统调用参数传递及进程生命周期共同配合。单独编译目标文件能检查语法与部分接口，隔离启动能验证一部分内存基础；而真实用户态运行、回归测试和寄存器现场分别验证了更完整的行为。不同层次的证据应结合使用，并说明各自的范围。

## 10. 联合版本验证与本人取证

### 10.1 验证范围与环境

A 的阶段性实现完成后，B、C 的用户程序加载、进程生命周期、调度与地址空间复制等工作合入，形成了可运行用户程序的联合版本。本人在 WSL 环境下使用 RISC-V 交叉工具链、QEMU 4.1.1、OpenSBI v0.4 进行联合验证；有关 B、C 的实现与验证应结合各自报告，以下只记录本人实际看到的结果。

本轮日志目录为 `docs/logs/a-lab5-integrated-20261127-ZYIiyU/`。构建、评分、专项测试和双重 GDB 的源码提交应以本轮已有版本记录及工作区状态为准；不能仅由本文中 A 的早期提交推断全部联合测试的提交版本。早期隔离测试与联合测试是不同镜像、不同验证范围。

### 10.2 普通启动与用户程序

在联合版本执行 `make qemu`，观察到物理页分配、页表、内核分配器和虚拟内存相关自检的成功信息。随后内核启动用户程序 `hello`，输出：

```text
kernel_execve: pid = 2, name = "hello".
Hello world!!.
I am process 2.
hello pass.
all user-mode processes have quit.
init check memory pass.
```

该输出说明本次运行中用户程序启动、输出并退出，内核随后完成进程回收及内存检查。日志继续出现 `100 ticks`；QEMU 由外部终止，不能将其表述为内核自动关机。普通运行记录见 [qemu.log](../logs/a-lab5-integrated-20261127-ZYIiyU/qemu.log)。

### 10.3 教师评分

本人执行 `make grade`，观察到 13 个评分项的结果与输出检查均为 `OK`，最后为：

```text
Total Score: 130/130
grade exit=0
```

评分覆盖 `badsegment`、`divzero`、`softint`、`faultread`、`faultreadkernel`、`hello`、`testbss`、`pgdir`、`yield`、`badarg`、`exit`、`spin` 和 `forktest`。评分期间 `testbss.c` 的编译输出包括数组越界警告；评分结果和警告均按原始记录保留。评分通过表明所列测试在该次运行中通过，并不等于穷尽了所有异常输入。评分后本人重新执行 `make`，恢复普通构建。证据见 [grade.log](../logs/a-lab5-integrated-20261127-ZYIiyU/grade.log)、[grade.exit](../logs/a-lab5-integrated-20261127-ZYIiyU/grade.exit) 和 [评分后重建日志](../logs/a-lab5-integrated-20261127-ZYIiyU/rebuild-after-grade.log)。

### 10.4 COW 和逐页复制专项回归

本人运行 `tools/check-lab5-c.py --mode both`。脚本分别在隔离源码副本中对 COW 与 eager 两种模式执行 `hello`、`exit`、`yield`、`spin`、`forktest`、`forktree`、`waitkill` 和 `c_lifecycle`，共 16 项均显示 `PASS`，脚本退出码为 0。`c_lifecycle` 由该专项测试在副本中加入测试代码，以验证进程生命周期和虚拟内存相关行为；这些结果不应描述成普通 `make qemu` 默认运行了同一套测试。

专项测试结果见 [c-regression.log](../logs/a-lab5-integrated-20261127-ZYIiyU/c-regression.log) 和 [c-regression.exit](../logs/a-lab5-integrated-20261127-ZYIiyU/c-regression.exit)；逐项日志目录为 `docs/logs/a-lab5-integrated-20261127-ZYIiyU/c-suite-QAmAaa/`。

### 10.5 同一 QEMU 中的宿主与客体双重 GDB

在同一个 QEMU 进程中使用宿主 GDB 观察模拟器内部状态，并使用 RISC-V GDB 观察客体内核与用户程序。此次跟踪的是用户程序的一次 `SYS_putc` 调用，而不是全部系统调用。成功日志的关键观察如下：

| 阶段 | 本次观察 | 含义 |
| --- | --- | --- |
| 首条用户指令 | `pc=0x800074`，宿主侧 `priv=0` | 用户程序进入 U 态 |
| `ecall` 前 | `pc=0x800054`，`a0=30` | 本次调用编号为 `SYS_putc` |
| 翻译与异常产生 | `trans_ecall` 对应 `0x800054`；`helper_raise_exception` 的 `cause=8`、`priv=0` | 分别观察到指令翻译和运行时用户态环境调用异常 |
| 异常投递后 | `priv=1`、`scause=8`、`sepc=0x800054` | QEMU 模拟 CPU 进入 S 态并记录异常现场 |
| 内核系统调用分发 | `tf.epc=0x800058` | 内核保存的返回地址比 `ecall` 地址增加 4 字节 |
| `sret` 前 | `sp=0x7ffffef0`，`tf.epc=0x800058` | 已到达客体的返回指令，用户栈指针已恢复 |
| 返回处理前后 | `helper_sret` 前 `priv=1`，后 `priv=0`，返回地址 `0x800058` | QEMU 的模拟特权级恢复为 U 态 |
| 用户态继续 | `pc=0x800058`、`sp=0x7ffffef0`、`a0=0` | 从 `ecall` 的下一条指令继续，取得返回值 |

其中十进制 `8388692`、`8388696` 分别为 `0x800054`、`0x800058`。地址仅代表本次构建。脚本输出 `DUAL GDB PASS`，通过 `set -o pipefail` 记录的退出状态为 0。

`trans_ecall`、`trans_sret` 是 QEMU 翻译客体指令的过程；`helper_raise_exception` 和 `helper_sret` 的断点及寄存器记录对应运行时处理。将二者区分，才能说明实际观察到了翻译与特权级状态变化。

证据：[结果摘要](../logs/a-lab5-integrated-20261127-ZYIiyU/dual-gdb-sret-yNlGHj/summary.log)、[宿主 GDB 原始日志](../logs/a-lab5-integrated-20261127-ZYIiyU/dual-gdb-sret-yNlGHj/host-mi.log)、[客体 GDB 原始日志](../logs/a-lab5-integrated-20261127-ZYIiyU/dual-gdb-sret-yNlGHj/guest-mi.log)、[运行输出](../logs/a-lab5-integrated-20261127-ZYIiyU/dual-gdb-sret-yNlGHj/run.log)、[退出状态](../logs/a-lab5-integrated-20261127-ZYIiyU/dual-gdb-sret-yNlGHj/exit.log)、[实际使用的临时脚本副本](../logs/a-lab5-integrated-20261127-ZYIiyU/dual-gdb-sret-yNlGHj/sret-breakpoint-script.py.txt)。

### 10.6 调试问题与适用范围

最初双重 GDB 脚本在宿主条件断点处未完整通过。结合失败记录和本机 QEMU 反汇编，检查了函数序言中的参数保存：在临时脚本中将 `helper_raise_exception`、`riscv_cpu_do_interrupt`、`helper_sret` 的断点分别移动到本次构建的 `+19`、`+16`、`+20`；将 `trans_sret` 设为一次性断点，避免重复命中影响后续观察。最终在这一临时脚本下通过完整验证。正式仓库中的 `tools/lab5-dual-gdb.py` 未因此修改；不能将临时脚本的通过直接表述为原脚本在本机无需调整即可通过。此前失败尝试与反汇编日志仍保存在本轮日志目录。

本次宿主调试使用 `$HOME/OSopt/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64`，其 Build ID 是 `2b5b23f69fd9bec49f11099327c8164fe27147a2`；安装目录下的 QEMU Build ID 不同。上述偏移依赖该可执行文件的具体编译结果，换版本或重新编译后须重新反汇编核对。调试前应准备与客体镜像匹配的 `bin/kernel`、`bin/ucore.img`、`obj/__user_hello.out`，并从项目根目录运行保留的脚本副本；不要在调试期间并行执行会清理或重建镜像的评分命令。

## 11. 联合验证后的认识与收尾

联合实验把 A 的 trap 与系统调用基础同 B 的用户程序加载、C 的进程生命周期及地址空间管理连接起来。普通启动、教师评分、两种地址空间复制模式回归，以及同一 QEMU 的双重 GDB，分别提供了不同层次的证据：前者说明用户程序实际运行和回收，评分和专项覆盖具体测试场景，双重 GDB 则揭示一次 `putc` 的特权级往返现场。这些结果各有范围，应保留对应原始日志，不能把单次 GDB 样本推广为所有系统调用均已穷尽验证。

本次实验让我理解了用户程序、内核与模拟器之间的分层关系：用户程序发起系统调用，内核负责保存与恢复执行现场及完成服务，模拟器负责执行和模拟指令及特权级变化。遇到调试结果与预期不一致时，也需要结合实际指令、断点位置与原始日志分析，不能仅凭某一个成功标记推断整个系统的行为。
