# Lab 1：内核入口、栈初始化与链接布局分析

实验者：尚文轩（成员 B）
对应分工：练习 1 —— 入口汇编、栈初始化、链接布局
实验日期：2026 年 10 月 10 日

## 1. 本部分目标与范围

本部分围绕练习 1 展开：解释内核入口 `kern/init/entry.S` 中两条关键指令 `la sp, bootstacktop` 与 `tail kern_init` 的操作和目的，并结合本次构建的实际反汇编、链接脚本与 GDB 观测结果，说明内核栈的空间布局、链接符号含义以及 `kern_init` 进入 C 语言环境后完成的操作。

本部分与小组其他成员的分工衔接如下：

- 复位引导代码、OpenSBI 入口到内核入口的执行路径由国峻赫的[启动调试草稿](startup-debug.md)负责；
- Makefile 构建流程、QEMU 加载方式和 SBI 输出机制由陈铭宇的[构建与输出分析](build-sbi.md)负责；
- 本部分使用二者提供的证据进行交叉核对，重点回答入口指令与布局问题。

本部分的分析以源码、链接脚本和本机实际构建结果为准，观测结果与推断分开表述。

## 2. 实验环境与工具版本

| 项目           | 本次使用的环境                                                  |
| -------------- | --------------------------------------------------------------- |
| 主机及系统     | Windows 主机，WSL2 Ubuntu 22.04.5 LTS                           |
| 交叉编译器     | Ubuntu 包 `gcc-riscv64-unknown-elf` 10.2.0-0ubuntu1（GCC 10.2.0） |
| 汇编器/链接器  | `binutils-riscv64-unknown-elf` 2.35.1                           |
| 调试器         | `gdb-multiarch` 12.1（以 `riscv64-unknown-elf-gdb` 名称调用）   |
| 模拟器         | QEMU 6.2.0（`qemu-system-riscv64`）                             |
| 固件           | OpenSBI v1.3（Ubuntu `opensbi` 包提供的 fw_dynamic 固件）       |
| 源码提交       | `a9bcd88`（lab1 分支）                                          |

与小组基线（SiFive GCC-Metal 10.2.0、GDB 10.1、QEMU 4.1.1、OpenSBI v0.4）相比，本机编译器主版本号一致（GCC 10.2.0），QEMU 与 OpenSBI 版本较新。固件相关数值（如内核入口执行前的 `sp`、`a1`）随固件版本变化，但本次构建中内核侧全部关键地址与国峻赫的启动记录**完全一致**（见 6.3 节对照表）。QEMU 6.2 下 `-device loader` 不再自动向 fw_dynamic 固件传递内核入口地址，本机通过不改动仓库 Makefile 的本地 QEMU 包装脚本补传 `-kernel` 参数解决（仅影响本机运行方式，不影响仓库代码与链接结果）。

本机运行环境变量设置：`source ~/OSopt/env.sh`（设置 `PATH` 与 `LD_LIBRARY_PATH`），随后在仓库根目录执行 `make clean && make`。构建输出与陈铭宇草稿第 2 节一致，生成 `bin/kernel` 与 `bin/ucore.img`。

## 3. 练习 1 的完整回答

### 3.1 `la sp, bootstacktop` 的操作与目的

`la`（load address）是一条 RISC-V 伪指令，它把符号 `bootstacktop` 的地址装入寄存器 `sp`。本次构建中它展开为两条实际机器指令（见第 4 节）：

```text
0x80200000: auipc sp,0x3
0x80200004: mv    sp,sp
```

第一条 `auipc sp,0x3` 把当前 PC 的高 20 位地址基准 `0x80200000` 与立即数 `0x3000` 相加写入 `sp`，得到 `0x80203000`；第二条是 `addi sp,sp,0` 的等价形式（加零偏移，反汇编器显示为 `mv sp,sp`），不改变数值。

其目的是在内核的第一条指令处建立**内核自己的栈指针**：把 `sp` 设置为预留在数据段中的内核栈空间顶部 `bootstacktop`，为接下来 C 函数的栈帧分配和函数调用提供存储空间。它并不清空栈内容，也不涉及对 OpenSBI 栈的保存或切换语义——只是把 `sp` 指向一块由内核自身链接布局预留的可用内存。

### 3.2 `tail kern_init` 的操作与目的

`tail` 是 GNU 汇编器对"尾调用"的助记写法，本次链接结果是一条直接跳转指令：

```text
0x80200008: j 0x8020000a <kern_init>
```

`tail` 与普通函数调用 `call` 的关键区别在于**不保存返回地址**：普通调用会在 `ra` 中写入下一条指令地址并建立"调用—返回"链，而 `tail` 直接转移控制权，`kern_init` 返回后不会回到入口汇编。

本次启动入口采用 `tail` 是合理的：[init.c](../kern/init/init.c) 将 `kern_init` 声明为 `__attribute__((noreturn))`，其内部以 `while (1)` 结束，永不返回；入口汇编在移交控制权后也没有任何需要继续执行的收尾工作。因此启动路径不需要保留返回地址，用尾跳转既符合 `noreturn` 语义，也省去了一次无意义的 `ra` 保存。

### 3.3 为什么进入 C 函数之前必须设置栈

C 函数的运行依赖栈，本次实验中这一点有直接的反汇编证据：[kern_init](../kern/init/init.c) 的第一段机器代码为：

```text
0x8020000a: addi sp,sp,-16     # 分配 16 字节栈帧
0x80200020: sd   ra,8(sp)      # 保存返回地址到栈上
0x80200022: jal  ra,802004b6   # 调用 memset，ra 被写入返回地址
```

进入 `kern_init` 后，函数立即在当前 `sp` 基础上向下分配栈帧，并把 `ra` 保存到栈内存中。这一系列操作都要求 `sp` 指向一块可读写的合法内存：

- 若 `sp` 保持固件遗留值，栈帧会写入固件正在使用的内存区域，破坏固件数据；
- 若 `sp` 指向无效地址，第一次栈访问就会引发访问异常，内核无法运行。

因此入口汇编的第一件事就是让 `sp` 指向内核自己预留的栈空间。这与后续实验中的进程切换保存上下文不同：这里只是为**第一个** C 函数建立最初的运行时环境，不涉及上下文结构的保存与恢复。

## 4. `entry.S` 源码与实际反汇编的对应分析

[entry.S](../kern/init/entry.S) 的全部汇编源码为：

```asm
    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    la sp, bootstacktop
    tail kern_init

.section .data
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:
```

### 4.1 目标文件（重定位）阶段的反汇编

编译后但链接前，[obj/kern/init/entry.o](../obj/kern/init/entry.o) 的反汇编为：

```text
0000000000000000 <kern_entry>:
   0: auipc sp,0x0
   4: mv    sp,sp
   8: auipc t1,0x0
   c: jr    t1 # 8 <kern_entry+0x8>
```

两个值得注意的现象：

1. `la sp, bootstacktop` 的立即数字段全部为 0。因为 `bootstacktop` 位于同文件的 `.data` 节，与 `.text` 节跨节引用，其最终偏移要等链接时各节排布确定后才能计算，汇编器只生成占位立即数并留下重定位记录。
2. `tail kern_init` 显示为 `auipc t1,0x0; jr t1` 两条指令。因为 `kern_init` 定义在另一个源文件（init.c）中，汇编阶段无法确定距离，汇编器按"经寄存器 `t1` 的绝对跳转"的最保守形式生成，等待链接器松弛（relaxation）。

目标文件的 `.rela.text` 重定位表完整记录了这两处待定项（原始输出见 [logs/entry-layout.txt](logs/entry-layout.txt)）：

```text
Offset  Type                Sym.Value  Sym.Name + Addend
0x0     R_RISCV_PCREL_HI20  0x2000     bootstacktop + 0   (+ R_RISCV_RELAX)
0x4     R_RISCV_PCREL_LO12_I 0x0       .L0 + 0           (+ R_RISCV_RELAX)
0x8     R_RISCV_CALL        0x0        kern_init + 0     (+ R_RISCV_RELAX)
```

即：`la` 伪指令对应 PC 相对寻址的 HI20/LO12 重定位对；`tail` 对应一条 `R_RISCV_CALL` 调用类重定位。每条后面附带的 `R_RISCV_RELAX` 表示允许链接器对该处指令做松弛优化。

### 4.2 链接后的反汇编与重定位的解析

链接后的 `bin/kernel` 中（原始输出见 [logs/entry-layout.txt](logs/entry-layout.txt)）：

```text
0000000080200000 <kern_entry>:
    80200000: 00003117    auipc sp,0x3
    80200004: 00010113    mv    sp,sp
    80200008: a009        j     8020000a <kern_init>
```

链接器完成了三件事：

1. **填充 `la` 的立即数**：`bootstacktop` 最终地址为 `0x80203000`，与 `auipc` 所在地址 `0x80200000` 的高 20 位差值即 `0x3`，写入 `auipc sp,0x3`；低 12 位偏移为 0，`addi sp,sp,0` 数值不变（显示为 `mv sp,sp`）。
2. **松弛 `tail`**：`R_RISCV_CALL` 的目标 `kern_init = 0x8020000a` 与调用点距离仅 10 字节，在 `j` 指令的 ±1 MiB 范围内，链接器把 `auipc t1,0x0; jr t1` 两条 4 字节指令松弛为一条 2 字节的 `j`（注意 `j` 出现在 `0x80200008`，下一条指令地址是 `0x8020000a`，间距 2 字节，说明最终采用的是 RVC 压缩指令形式）。这也是 `tail` 不保留返回地址的直接体现——若换成普通调用，链接器会生成带 `ra` 写入的 `jal`。
3. 重定位表中的 `R_RISCV_RELAX` 条目正是为第 2 步的松弛保留的许可标记。

综上，本次实验中伪指令到机器指令的对应关系为：

| 源码           | 目标文件阶段               | 链接后                        | 重定位类型                |
| -------------- | -------------------------- | ----------------------------- | ------------------------- |
| `la sp, bootstacktop` | `auipc sp,0x0` + `addi sp,sp,0` | `auipc sp,0x3` + `mv sp,sp`   | PCREL_HI20 + PCREL_LO12_I |
| `tail kern_init`     | `auipc t1,0x0` + `jr t1`   | `j 0x8020000a`（RVC 压缩形式） | R_RISCV_CALL（链接器松弛） |

## 5. 链接脚本与内核段布局

### 5.1 [tools/kernel.ld](../tools/kernel.ld) 的关键设置

```ld
OUTPUT_ARCH(riscv)
ENTRY(kern_entry)
BASE_ADDRESS = 0x80200000;
```

- `ENTRY(kern_entry)`：指定 ELF 的入口符号。`readelf -h` 显示 `Entry point address: 0x80200000`，与符号表一致。
- `BASE_ADDRESS = 0x80200000`：内核链接基址。该地址与 Makefile 中 QEMU loader 的装载地址 `addr=0x80200000` 一致（装载过程见陈铭宇部分），也与 OpenSBI 启动信息中 `Domain0 Next Address: 0x80200000` 一致，保证"链接时的符号地址 = 装载后的实际内存地址"。
- 段排布：`.text` → `etext` → `.rodata` → `ALIGN(0x1000)` → `.data` → `.sdata` → `edata` → `.bss` → `end`。
- `/DISCARD/`：丢弃 `.eh_frame` 等宿主工具链产生的、内核不需要的节。

### 5.2 本次构建的实际段布局

`readelf -S` 结果（原始输出见 [logs/entry-layout.txt](logs/entry-layout.txt)）与链接脚本逐项对应：

| 段       | 起始地址   | 大小    | 标志 | 说明                                   |
| -------- | ---------- | ------- | ---- | -------------------------------------- |
| `.text`  | 0x80200000 | 0x4c8   | AX   | 全部代码，含 `kern_entry`、`kern_init` |
| `.rodata`| 0x802004c8 | 0x270   | A    | 只读数据，含消息串与格式串             |
| `.data`  | 0x80201000 | 0x2000  | WA   | 经 `ALIGN(0x1000)` 后从 0x80201000 开始；内容几乎全部是 `bootstack` |
| `.sdata` | 0x80203000 | 0x8     | WA   | 小型全局变量，本构建中仅 `SBI_CONSOLE_PUTCHAR` 一个 8 字节变量 |
| （无 `.bss`）| —      | 0       | —    | 当前代码没有未初始化的全局数据         |

`size` 工具的结果与之吻合：`text = 1848 (0x738 = 0x4c8 + 0x270)`，`data = 8200 (0x2008 = 0x2000 + 0x8)`，`bss = 0`。

`.data` 段 8 KiB 的容量几乎全部来自 `entry.S` 中 `.space KSTACKSIZE` 预留的栈空间；其余全局变量（如 SBI 服务号常量）作为小型数据进入了 `.sdata`。由于 `--gc-sections` 与 `-fdata-sections` 的配合，`libs/sbi.c` 中未被引用的 8 个服务号常量（`SBI_SET_TIMER` 等）在链接时被整节回收，`.sdata` 最终只保留被 `cons_putc` 引用链使用的 `SBI_CONSOLE_PUTCHAR`。

### 5.3 关键链接符号

`nm -n bin/kernel` 的结果（完整输出见 [logs/entry-layout.txt](logs/entry-layout.txt)）：

| 符号                 | 地址       | 来源               |
| -------------------- | ---------- | ------------------ |
| `kern_entry`         | 0x80200000 | entry.S，ELF 入口  |
| `kern_init`          | 0x8020000a | init.c，C 入口     |
| `bootstack`          | 0x80201000 | entry.S，栈底      |
| `bootstacktop`       | 0x80203000 | entry.S，栈顶      |
| `SBI_CONSOLE_PUTCHAR`| 0x80203000 | sbi.c 全局变量     |
| `edata`              | 0x80203008 | 链接脚本 PROVIDE   |
| `end`                | 0x80203008 | 链接脚本 PROVIDE   |

其中 `etext`、`edata`、`end` 由链接脚本的 `PROVIDE` 定义：`edata` 在 `.sdata` 之后（值为 0x80203008），`end` 在 `.bss` 之后——由于没有 `.bss`，二者相等（详见第 8 节）。

## 6. 内核栈的空间布局

### 6.1 栈空间的源码依据

[kern/mm/memlayout.h](../kern/mm/memlayout.h) 定义：

```c
#define KSTACKPAGE  2                    // # of pages in kernel stack
#define KSTACKSIZE  (KSTACKPAGE * PGSIZE) // sizeof kernel stack
```

[kern/mm/mmu.h](../kern/mm/mmu.h) 定义 `PGSIZE = 4096`、`PGSHIFT = 12`。因此内核启动栈大小为 `2 × 4 KiB = 8 KiB`。

[entry.S](../kern/init/entry.S) 在 `.data` 节中声明该栈：

```asm
.section .data
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:
```

`.align PGSHIFT` 使栈底按 4 KiB 对齐；`.space KSTACKSIZE` 在数据段中预留 8 KiB 空间；符号 `bootstack` 在空间起始处（低地址），`bootstacktop` 在结束处（高地址）。

### 6.2 栈的实际起止与增长方向

结合第 5.2 节的段布局，本构建中：

- **起止范围**：`bootstack = 0x80201000` 到 `bootstacktop = 0x80203000`，共 0x2000 = 8 KiB；
- **对齐**：栈底 0x80201000 按页（4 KiB）对齐（链接脚本在 `.data` 前的 `ALIGN(0x1000)` 与源码 `.align PGSHIFT` 共同保证）；
- **增长方向**：向下。RISC-V 的栈向低地址增长，`sp` 初始化为高地址的 `bootstacktop`，函数序言执行 `addi sp,sp,-16` 时 `sp` 减小、在栈顶下方分配栈帧。GDB 观测中 `kern_init` 建立栈帧后 `sp = 0x80202ff0 = 0x80203000 - 16`，与"向下增长"一致；
- **所在段**：栈空间位于 `.data` 段（WA，可读写），属于映像文件的一部分——`objcopy` 生成的裸镜像 `bin/ucore.img` 大小为 12296 字节（= 0x3008，即从 0x80200000 到 `end` 的完整内存映像长度），其中包含这段 8 KiB 的零填充栈空间。这与放入 `.bss`（映像中不占空间、运行时清零）的做法不同，但对本实验的启动行为没有差别。

### 6.3 GDB 验证：栈初始化前后的 `sp`

本机通过 `make debug` + GDB 脚本验证（完整会话记录见 [logs/entry-gdb.log](logs/entry-gdb.log)）：

| 观察位置                   | PC           | SP           |
| -------------------------- | ------------ | ------------ |
| 断点命中，入口指令执行前   | `0x80200000` | `0x80026eb0` |
| 执行 `auipc sp,0x3` 后     | `0x80200004` | `0x80203000` |
| 执行 `mv sp,sp` 后         | `0x80200008` | `0x80203000` |
| 执行 `j kern_init` 后      | `0x8020000a` | `0x80203000` |
| `kern_init` 建立栈帧后     | `0x8020003a` | `0x80202ff0` |

同时 GDB 直接查询符号：`&bootstack = 0x80201000`、`&bootstacktop = 0x80203000`，与 `sp` 的初始化结果一致，也与国峻赫的 `lab1_3.png`、`lab1_4.png` 截图互相印证。

入口执行前的 `sp = 0x80026eb0` 是固件遗留的栈指针。国峻赫基线上该值为 `0x8001bd80`，差异来自 OpenSBI 版本不同（v1.3 与 v0.4 的内部栈布局不同），属于固件相关数值，不影响内核侧结论。内核侧的全部关键地址（`kern_entry`、`kern_init`、`bootstacktop`、`cprintf = 0x80200056`、消息串 `0x802004c8`、格式串 `0x802004e8`、最终循环 `0x8020003a`）与国峻赫的记录完全一致。

## 7. `bootstacktop` 与 `SBI_CONSOLE_PUTCHAR` 的地址重合

国峻赫的记录中提到，GDB 显示 `sp` 的数值后面出现了 `<SBI_CONSOLE_PUTCHAR>` 的符号注释。本机 GDB 复现了同一现象：

```text
sp   0x80203000   0x80203000 <SBI_CONSOLE_PUTCHAR>
(gdb) info symbol 0x80203000
SBI_CONSOLE_PUTCHAR in section .sdata
```

原因在符号布局：`bootstacktop = 0x80203000` 是 `.data` 段的末尾，而 `.sdata` 段紧随其后从 `0x80203000` 开始，其第一个（也是唯一一个）符号 `SBI_CONSOLE_PUTCHAR` 恰好占据 `[0x80203000, 0x80203008)`。GDB 对裸数值做符号注释时，在该地址上匹配到的是 `.sdata` 段的变量符号，于是显示了 `<SBI_CONSOLE_PUTCHAR>`。

这是一个**显示层的符号匹配现象**：两个不同段的符号地址恰好重合（一个是栈顶"天花板"，一个是变量的起始地址），并不意味着 `sp` 指向该变量，也不影响栈的使用——栈向下增长，实际栈帧落在 `0x80203000` 以下，不会覆盖 `0x80203000` 处的 `.sdata` 变量。判断栈初始化正确性的依据是 `sp` 与 `&bootstacktop` 的数值相等，而不是数值后面的符号注释。

## 8. `edata`、`end` 与 BSS 清零范围

[kern_init](../kern/init/init.c) 的第一条语句是：

```c
extern char edata[], end[];
memset(edata, 0, end - edata);
```

其意图是把 BSS 段（未初始化的全局数据）清零。本次构建中：

- `readelf -S` 显示内核**没有 `.bss` 段**（当前代码的所有全局变量均有初始化值或由链接脚本保留空间）；
- `nm -n` 显示 `edata = 0x80203008`、`end = 0x80203008`，二者相等；
- 链接后 `kern_init` 的反汇编中，`memset` 的两个地址参数均计算为 `0x80203008`：

```text
0x8020000a: auipc a0,0x3
0x8020000e: addi  a0,a0,-2    # a0 = 0x80203008 <edata>
0x80200012: auipc a2,0x3
0x80200016: addi  a2,a2,-10   # a2 = 0x80203008 <edata>
0x8020001e: sub   a2,a2,a0    # a2 = 0
```

- GDB 在 `memset` 断点处直接观测到实参：`memset (s=0x80203008, c=0, n=0)`。

由此可以确定：**本次构建中 BSS 清零范围为空，`memset` 实际清零 0 字节**。这是当前代码状态的正常结果（没有任何未初始化的全局数据），不是缺陷——国峻赫草稿中"由反汇编推断两地址相等"的推断，现由符号表与 `memset` 实参双重证实。清零逻辑本身予以保留，是后续实验加入未初始化全局数据（如页表、进程结构）后的必要初始化步骤。

## 9. `kern_init` 进入 C 环境后的操作

结合源码与链接后反汇编，`kern_init` 依次完成：

1. **清零 BSS 段**：`memset(edata, 0, end - edata)`。本次构建中范围为空（第 8 节），但调用链完整：准备 `a0/a1/a2` 实参并 `jal memset`；
2. **输出启动信息**：把消息串 `"(THU.CST) os is loading ...\n"`（地址 `0x802004c8`，位于 `.rodata`）与格式串 `"%s\n\n"`（地址 `0x802004e8`）作为实参调用 `cprintf`（地址 `0x80200056`），终端打印出启动文字后返回（输出如何经 SBI 到达终端见陈铭宇部分）；
3. **进入无限循环**：`while (1);` 链接为 `0x8020003a: j 0x8020003a` 的自跳转指令，GDB 观测 PC 保持 `0x8020003a` 不变。

函数的栈帧行为：序言 `addi sp,sp,-16` 分配 16 字节栈帧并把 `ra` 保存到 `8(sp)`，`sp` 从 `0x80203000` 降为 `0x80202ff0`；由于函数以死循环结束且声明为 `noreturn`，不会执行函数返回时的栈恢复。该行为与入口采用 `tail` 跳转的约定（第 3.2 节）相互一致。

## 10. 与其他成员部分的衔接与核对结果

1. **启动路径**：国峻赫草稿记录的执行顺序（复位代码 → OpenSBI 入口 `0x80000000` → 内核入口 `0x80200000`）在本机得到验证；本机 OpenSBI v1.3 的启动信息明确打印 `Domain0 Next Address: 0x80200000`，与链接基址一致；
2. **地址交叉核对**：国峻赫记录的内核侧关键地址与本机构建逐一相同（第 6.3 节），证明两份分析基于等效的源码与链接结果，可以合并使用；固件侧数值（入口前 `sp`、`a1`）因固件版本不同而不同，各自保留实际记录；
3. **装载时机**：本部分仅确认"链接地址 = 装载地址 = 固件跳转地址"三者一致；镜像如何被 QEMU 放入内存、与 CPU 开始执行的区分由陈铭宇部分说明；
4. **报告整合边界**：本部分不涉及复位引导代码逐条分析、OpenSBI 内部过程与 SBI 输出机制，这些内容分别在另两位成员的草稿中。

## 11. 资料依据

1. 源码：`kern/init/entry.S`、`kern/init/init.c`、`tools/kernel.ld`、`kern/mm/mmu.h`、`kern/mm/memlayout.h`、`libs/sbi.c`；
2. 本次实验的原始命令输出：[docs/logs/entry-layout.txt](logs/entry-layout.txt)（`readelf -h/-S/-r/-s`、`nm -n`、`objdump -d` 的完整文本）；
3. 本次实验的 GDB 会话记录：[docs/logs/entry-gdb.log](logs/entry-gdb.log)（断点、单步、寄存器与符号查询、QEMU 启动输出）；
4. 国峻赫的启动调试草稿 [docs/startup-debug.md](startup-debug.md) 与截图 `docs/images/lab1_3.png`、`docs/images/lab1_4.png`（栈初始化证据，本部分直接引用，不重复截取）；
5. 陈铭宇的构建与输出分析草稿 [docs/build-sbi.md](build-sbi.md)（构建流程与装载方式）。
