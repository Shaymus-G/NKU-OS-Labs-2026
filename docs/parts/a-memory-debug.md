# A：物理内存初始化与调试记录

> 负责人：国峻赫  
> 当前阶段：已完成物理内存初始化、First Fit 验证，以及双重 GDB 下的 Sv39 页表遍历、TLB 填充和填充后复核；最终联合版本待回归。  
> 仓库分支：lab2。当前源码和 Makefile 位于仓库根目录。  
> 本文保存位置：docs/parts/a-memory-debug.md。

## 1. 工作范围与当前状态

本部分负责实现 `kern/mm/pmm.c` 中的 `page_init()`，从设备树解析结果建立物理页描述符，并将可用物理页交给当前分配器。同时分析启动页表，并通过宿主 GDB 和 RISC-V GDB 调试 QEMU 中的地址翻译与 TLB 行为。

当前已经完成初始化实现、First Fit 配置下的编译运行，并按调试步骤完成初始化状态检查和截图。已取得的启动输出显示 `check_alloc_page() succeeded!`。本阶段通过命令行排除了尚未完成的 Best Fit 源文件，因此这些结果不代表 Best Fit、Buddy 或完整 Lab 2 已通过验证。

本文中的运行地址来自本次构建。增加代码或改变链接布局后，应以新构建的符号与输出为准。本次调试时已跟踪源码的基线提交为 `79b5791a358d2e5f684b09e09dbd201325577c59`；截图与日志在之后单独提交。当前 Git 检查显示已跟踪文件无未提交修改，但不对构建产物作逐字节一致性保证。陈铭宇已报告推送 Best Fit 实现，本文尚不将其结果作为 A 本机复测结果。

## 2. 实验环境与证据

| 项目 | 本次环境 |
|---|---|
| 操作系统 | WSL Ubuntu 20.04 |
| 仓库目录 | `~/NKU-OS-Labs-2026` |
| 交叉编译工具链 | SiFive GCC-Metal 10.2.0 |
| 客体调试器 | SiFive GDB-Metal 10.1 |
| 模拟器 | QEMU 4.1.1 |
| 固件 | OpenSBI v0.4 |
| QEMU 默认内存 | 128 MiB |
| 本次证据对应的物理页管理器 | `default_pmm_manager`，即 First Fit |

早期初始化阶段的材料如下；文件存在性和具体内容应在最终提交前核对：

- [环境记录](../logs/a-environment.log)
- [初次构建记录](../logs/a-build.log)
- [临时 First Fit 构建记录](../logs/a-build-first-fit.log)
- [初始化 GDB 日志](../logs/a-page-init-debug.log)
- [规格与 AI 交互记录](../prompts/a-page-init.md)

## 3. 初始化的整体流程

`kern_init()` 先清零 BSS，再调用 `dtb_init()`，读取并保存设备树中的内存信息，随后调用 `pmm_init()`。

`pmm_init()` 依次执行：

1. `init_pmm_manager()`：选择分配器，调用其初始化接口，建立空的内部管理状态。
2. `page_init()`：读取内存范围，建立页描述符，排除已占用区域，并通过 `init_memmap()` 登记空闲页。
3. `check_alloc_page()`：调用当前管理器的检查函数。
4. 获取并输出启动页表的虚拟地址和物理地址。

`pmm_init()` 返回后，`kern_init()` 进入 `while (1)`。因此输出停止且终端不返回 shell，是框架预定的运行行为。

框架在进入 `kern_init()` 之前已经配置启动页表并启用 Sv39。`page_init()` 的职责是在这一地址映射基础上组织物理页，不是创建完整的按需分页系统。

## 4. page_init() 的功能规格

目标函数为 `static void page_init(void)`，不接收参数，也不返回数值。

输入来源：

- `get_memory_base()` 和 `get_memory_size()`：设备树解析结果。
- 链接符号 `end`：内核镜像末端虚拟地址。
- `PGSIZE`、`DRAM_BASE`、`PHYSICAL_MEMORY_END`、`PHYSICAL_MEMORY_OFFSET` 等框架常量。

输出状态：

- `va_pa_offset`：内核直接映射的虚实地址偏移。
- `npage`：管理范围的物理页号上界。
- `pages`：物理页描述符数组的虚拟地址。
- 当前分配器中登记的初始空闲页区间。

主要约束：

1. 区分虚拟地址、物理地址、物理页号和 `Page` 指针。
2. 校验内存范围和加法溢出，并遵守框架的管理上限。
3. 描述符数组位于内核之后，起点按页对齐。
4. 写描述符前检查数组空间是否足够。
5. 先将描述符设置为保留，随后只将确认可用的完整页面交给分配器。
6. OpenSBI、内核、描述符数组及对齐空隙不能作为空闲页分配。
7. 没有完整空闲页时不调用 `init_memmap()`；这不意味着后续需要分配页面的自检仍能通过。

## 5. 实现思路与关键概念

### 5.1 读取内存信息并确定边界

实现从 DTB 获取起点与大小，计算报告的末端，再结合 `PHYSICAL_MEMORY_END` 确定实际管理末端，并向下按页对齐。

当前框架的描述符编号以固定 `DRAM_BASE` 为起点，因此实现检查 DTB 起点是否符合该约定。该实现面向当前实验框架，没有声称支持任意起点或多个不连续内存区。

本次默认配置中，DTB 末端和框架管理上限均为 `0x88000000`。如果后续改变 QEMU 内存大小，需要重新核对管理上限和教师测试对 `npage` 的假设。

### 5.2 物理地址、虚拟地址与页号

在本实验的直接映射区域内，地址转换使用偏移：

```text
va_pa_offset = 0xffffffff40000000
物理地址 = 虚拟地址 - va_pa_offset
```

这里的运算按框架使用的 64 位地址表示进行。使用 `PADDR()` 前应先正确设置偏移。

`nbase` 是 DRAM 起始物理页号，`npage` 是末端物理页号上界，描述符数量为 `npage - nbase`。`pages[i]` 描述的物理页号为 `nbase + i`。

`pages` 是描述符数组地址，并不是所描述的物理页数据地址；描述符数组自身也占用需要保留的物理内存。

### 5.3 放置描述符数组

首先对内核末端 `end` 向上按页对齐，作为数组的虚拟起点。数组所需空间为：

```text
(npage - nbase) * sizeof(struct Page)
```

在写入数组前检查其物理范围没有超过管理末端。随后初始化每个描述符的引用计数、状态、块大小和链表字段，并设置保留标记。

### 5.4 登记空闲页

描述符数组末端向上按页对齐后，得到空闲物理页起点。该起点到管理末端之间的完整页面通过：

```c
init_memmap(pa2page(free_begin), free_count);
```

交给当前分配器。`init_memmap()` 继续调用管理器的具体实现，建立空闲链表和计数。因此初始化部分负责划定可用区域，分配器负责组织和分配这些区域。

## 6. 初次构建问题及处理

初次普通 `make` 在 `best_fit_pmm.c` 中失败：

```text
error: 'free_list' undeclared
error: 'nr_free' undeclared
```

原始文件保留了 Best Fit 检查函数，但分配器内部状态和管理函数尚未实现。检查函数引用上述状态，而 Makefile 默认会编译该文件，所以即使 `pmm.c` 已编译通过，整套工程仍无法完成构建。

本阶段没有删除或修改检查函数，而是保留 First Fit 为当前管理器，通过 Make 命令行临时排除未完成的 Best Fit 源文件：

```bash
set -o pipefail
make clean
make 'listf_cc=$(filter-out kern/mm/best_fit_pmm.c,$(call listf,$(1),$(CTYPE)))' \
    2>&1 | tee docs/logs/a-build-first-fit.log

make qemu \
    'listf_cc=$(filter-out kern/mm/best_fit_pmm.c,$(call listf,$(1),$(CTYPE)))'
```

该覆盖只影响相应的 Make 调用，不修改公共 Makefile。后续 Best Fit 完成后，需要清理并恢复普通构建，再联合验证。

这一问题属于未完成模块造成的集成阻塞，不是已经定位到的 `page_init()` 运行错误，也不应写成 AI 生成初始化代码的 Bug。

## 7. 运行结果与布局核对

本次实际输出的关键部分如下：

```text
DTB Init
HartID: 0
DTB Address: 0x82200000
Physical Memory from DTB:
  Base: 0x0000000080000000
  Size: 0x0000000008000000 (128 MB)
  End:  0x0000000087ffffff
DTB init completed

Special kernel symbols:
  entry  0xffffffffc02000d8 (virtual)
  etext  0xffffffffc0201846 (virtual)
  edata  0xffffffffc0206018 (virtual)
  end    0xffffffffc0206078 (virtual)
Kernel executable memory footprint: 24KB
memory management: default_pmm_manager
physical memory map:
  DTB range: [0x0000000080000000, 0x0000000088000000)
  managed end: 0x0000000088000000
  kernel end VA: 0xffffffffc0206078
  pages VA: 0xffffffffc0207000, PA: 0x0000000080207000
  descriptor count: 32768, bytes: 1310720
  free range: [0x0000000080347000, 0x0000000088000000), pages: 31929
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0205000
satp physical address: 0x0000000080205000
```

上述 `entry` 是框架打印的 `kern_init` 地址，不能直接当作汇编符号 `kern_entry` 的地址。最后两行打印的是启动页表的虚拟、物理地址，不是读取 `satp` CSR 得到的寄存器原始值。

![物理内存初始化运行结果](../images/a-page-init-output.png)

图中展示 DTB 内存范围、当前管理器、描述符布局、空闲区及分配器检查结果。

| 项目 | 本次结果 | 核对方法 |
|---|---|---|
| DRAM 范围 | `[0x80000000, 0x88000000)` | 长度为 `0x08000000`，即 128 MiB |
| nbase | `0x80000` | `0x80000000 / 4096` |
| npage | `0x88000` | `0x88000000 / 4096` |
| 描述符数量 | 32768 | `0x88000 - 0x80000` |
| 内核结束物理地址 | `0x80206078` | `end` 减去映射偏移 |
| 描述符起点 | `0x80207000` | 内核末端向上按 4 KiB 对齐 |
| 描述符总大小 | 1310720 字节，即 `0x140000` | 32768 × 40 字节 |
| 空闲起点 | `0x80347000` | `0x80207000 + 0x140000`，本次已对齐 |
| 初始空闲页数 | 31929 | `(0x88000000 - 0x80347000) / 4096` |

表中的页号和每个描述符 40 字节可由实际输出与框架关系推算，独立 GDB 检查见下一节。

全部 32768 页中，空闲区之前占用或保留了 839 页。其中，DRAM 起点至内核物理起点之间的固件等预留区域为 512 页（不代表 OpenSBI 实际镜像占满 2 MiB），内核及描述符之前的对齐空间为 7 页，描述符数组为 320 页。本次布局满足 `512 + 7 + 320 + 31929 = 32768`。

DTB 输出中的 `0x87ffffff` 是最后一个字节地址，而半开区间末端 `0x88000000` 不包含在区间中，两种表示一致。

## 8. GDB 检查过程与证据整理

已经按以下流程执行初始化状态检查并保存截图。精确的 GDB 返回值以随附日志和图片为准；本节列出检查方法及对应判据，不将预期值冒充日志原文。最终合并报告前，应逐项对照实际记录补充检查结论。

终端 1 启动：

```bash
make debug \
    'listf_cc=$(filter-out kern/mm/best_fit_pmm.c,$(call listf,$(1),$(CTYPE)))'
```

终端 2 执行 `make gdb`，连接后记录日志，并在 `check_alloc_page` 处断下。通过源码位置确认 `page_init()` 已返回，再检查全局变量：

```gdb
set pagination off
set logging file docs/logs/a-page-init-debug.log
set logging overwrite on
set logging on
break check_alloc_page
continue
bt
list
p/x va_pa_offset
p/x nbase
p/x npage
p/d npage - nbase
p/x (unsigned long)&end
p/x (unsigned long)pages
p/d sizeof(struct Page)
```

根据上述变量独立计算描述符物理范围和空闲起点，并检查对应描述符的标记。

| 检查项 | 本次实现和 First Fit 初始状态的预期判据 |
|---|---|
| `va_pa_offset` | `0xffffffff40000000` |
| `nbase`、`npage` | 分别为 `0x80000`、`0x88000` |
| 内核末端所在页的保留位 | 1 |
| 描述符数组起始页的保留位 | 1 |
| 第一个空闲页的保留位 | 0 |
| 第一个空闲页的空闲块头标记 | 1 |
| 初始空闲块的 `property` | 31929，检查时尚未开始分配器自检 |

这些块头与 `property` 判据针对本次 First Fit 初始状态，不能直接套用于 Buddy 的内部结构。

![GDB 检查页描述符与空闲区](../images/a-page-layout-gdb.png)

截图与日志用于支持全局变量、地址转换和页面状态检查；单张截图只证明实际展示的项目，其他项目应引用日志。

继续执行自检后，终端不再输出。此时可在 GDB 中用 Ctrl+C 暂停，通过 `info registers pc`、`x/6i $pc` 和 `si` 检查是否执行最终循环。若反汇编为单条自跳转，单步后 PC 不变是循环行为，不是初始化失败的证据。

## 9. 双重 GDB 的环境与调试对象

双重 GDB 分别控制两个不同层次：宿主机 `/usr/bin/gdb` 调试 x86-64 的 QEMU 进程；`riscv64-unknown-elf-gdb` 通过 QEMU 的远程接口调试 RISC-V 内核。宿主 GDB 暂停 QEMU 时，客体 GDB 仍显示 `Continuing.` 是正常现象。

本次使用的调试版模拟器：

```text
~/OSopt/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64
QEMU emulator version 4.1.1
ELF 64-bit，x86-64，with debug_info，not stripped
readelf 输出包含 .debug_info 和 .debug_line
```

本次内核符号记录为：

```text
ffffffffc0200000 T kern_entry
ffffffffc02000d8 T kern_init
ffffffffc0205000 D boot_page_table_sv39
ffffffffc0205000 D bootstacktop
```

`bootstacktop` 与页表起点相同，并不意味着栈和页表重叠：该符号表示向低地址增长的栈的上边界，而页表从此处向高地址存放。

本轮主要证据：

- [内核符号](../logs/a-paging-symbols.log)
- [QEMU 源码位置记录](../logs/a-qemu-source-locations.log)
- [成功的宿主 GDB 调试日志](../logs/a-qemu-pagewalk-body.log)

`a-qemu-pagewalk.log`、`a-qemu-pagewalk-retry.log` 是早期尝试记录；其中是否存在无效参数输出，应按具体上下文判断，不能替代成功日志。`a-ucore-pagewalk.log` 可作为客体侧补充材料，其内容需单独核对。`a-build.log` 记录的是早期构建过程，若包含 Best Fit 未实现导致的错误，应保留其失败语境。

## 10. 断点问题与修正

最初在 `riscv_cpu_tlb_fill` 函数入口设置断点，停在 `cpu_helper.c:438` 的左花括号处。曾出现以下异常组合：当前帧 `address` 看似宿主地址、`access_type` 为未知枚举、`mmu_idx` 为很大数值，而调用者帧明确传入 `addr=4096`、`MMU_INST_FETCH` 和 `mmu_idx=3`。

这些输出说明该停止位置的形参显示不可靠。结合改用函数体断点后参数恢复正常，判断问题与函数序言执行位置及调试信息的变量位置描述有关，不能据此认定客体页表或 QEMU 内存损坏。

修正方法是在已完成参数和局部变量初始化的位置断下：

```gdb
break /home/gjhjz/OSopt/qemu-4.1.1/target/riscv/cpu_helper.c:451
```

该行是：

```c
ret = get_physical_address(env, &pa, &prot, address, access_type, mmu_idx);
```

第一次无条件停止时，实际得到：

```text
address = 4096，即 0x1000
access_type = MMU_INST_FETCH
mmu_idx = 3
```

随后对该断点增加目标虚拟页条件。本次会话中断点编号为 1：

```gdb
condition 1 (address & 0xfffffffffffff000ULL) == 0xffffffffc0200000ULL
continue
```

再次命中时得到目标内核取指访问。源码行号与地址均针对本次版本；重新构建或换版本后应重新定位。

另一个排查现象是：在宿主 GDB 中按 Ctrl+C 后，当前线程停在 `__ppoll()`，此时 `info args` 只显示宿主事件循环参数，访问 `address` 会提示不在当前上下文。这不代表翻译函数没有该参数，而是当前栈帧不属于该函数。应等待正确断点，不能在任意停止位置读取其他函数的局部变量。

## 11. Sv39 页表遍历实测

### 11.1 捕获内核虚拟地址取指

在 `riscv_cpu_tlb_fill` 的函数体断点处，实测：

| 项目 | 实际值 |
|---|---|
| 目标虚拟地址 | `0xffffffffc02000d8`，对应本次 `kern_init` |
| 访问类型 | `MMU_INST_FETCH` |
| `mmu_idx` | 1，对应本次 S 模式访问路径 |
| `env->satp` | `0x8000000000080205` |

将目标保存到宿主 GDB 便利变量：

```gdb
set $target_va = address
set $pa_out = &pa
set $prot_out = &prot
```

这些便利变量属于当前宿主 GDB 会话，其中指针只用于相应调用仍有效的期间，不应在退出或重启后复用。

![捕获目标内核取指](../images/a-qemu-fetch-entry.png)

对本次 `satp` 解码：MODE 为 8，即 Sv39；ASID 为 0；根页表 PPN 为 `0x80205`，因此根页表物理地址为 `0x80205000`。这与内核输出的启动页表物理地址一致。

### 11.2 读取根页表项

在 `cpu_helper.c:254` 设置临时断点，此时 `pte` 已从客体物理内存读取，`ppn` 的赋值尚未执行：

```gdb
tbreak /home/gjhjz/OSopt/qemu-4.1.1/target/riscv/cpu_helper.c:254
continue
info args
p levels
p ptidxbits
p ptesize
p i
p ptshift
p/x base
p/x addr
p/x idx
p/x pte_addr
p/x pte
next
p/x ppn
```

实测结果：

| 变量 | 值 | 含义 |
|---|---|---|
| `levels` | 3 | Sv39 支持三级页表 |
| `ptidxbits` | 9 | 每级索引 9 位 |
| `ptesize` | 8 | 每项 8 字节 |
| `i` | 0 | QEMU 遍历循环第一次迭代，对应最高级页表 |
| `ptshift` | 18 | 当前级下方的虚拟页号位数 |
| `base` | `0x80205000` | 根页表物理基址 |
| `idx` | `0x1ff` | VPN[2]，即第 511 项 |
| `pte_addr` | `0x80205ff8` | 根页表项物理地址 |
| `pte` | `0x200000cf` | 读取的页表项 |
| `ppn` | `0x80000` | 执行赋值后得到的 PTE 物理页号 |

地址核算如下：

```text
idx = (0xffffffffc02000d8 >> 30) & 0x1ff = 0x1ff
pte_addr = 0x80205000 + 511 × 8 = 0x80205ff8
ppn = 0x200000cf >> 10 = 0x80000
```

![根页表项与遍历参数](../images/a-qemu-root-pte.png)

### 11.3 叶子判定、权限与大页

PTE 低位 `0xcf` 表示 V、R、W、X、A、D 为 1，U、G 为 0。该项有效并具有访问权限，因此是叶子项，不会沿下一层页表继续遍历。

本次按 `next` 观察有效性、叶子/非叶子、非法权限组合、特权级与大页对齐检查的执行路径，并成功继续到物理地址生成处。最高级页表的叶子项对应 1 GiB 映射；其物理页号低 18 位为零，满足该级大页的对齐要求。

因此，“Sv39 是三级页表”不意味着每次转换必定读取三级页表。本次仅在根级读取叶子项便完成转换。变量 `i=0` 是循环编号，不能写成“在最低级页表找到 4 KiB 叶子项”。

### 11.4 计算物理地址

在 `cpu_helper.c:334` 前停止，源码为：

```c
target_ulong vpn = addr >> PGSHIFT;
*physical = (ppn | (vpn & ((1L << ptshift) - 1))) << PGSHIFT;
```

本次实际值：

```text
vpn = 0xffffffffc0200
ppn = 0x80000
ptshift = 18
vpn & ((1ULL << ptshift) - 1) = 0x200
```

执行赋值后：

```text
*physical = 0x80200000
目标地址的低 12 位 = 0xd8
完整目标物理地址 = 0x80200000 | 0xd8 = 0x802000d8
```

这里该叶子转换路径返回的是目标所属的 4 KiB 物理页基址，不能把它误写为完整取指地址。1 GiB 映射中的低层虚拟页号补入物理页号，页内偏移保持不变。

执行 `finish` 返回调用者，观察到返回值为 0；通过事先保存的输出参数指针，得到物理页基址 `0x80200000`、权限值 `0x7`。后续进入 `tlb_set_page` 进一步印证了该结果。

![翻译结果及物理地址核算](../images/a-qemu-translation-result.png)

## 12. QEMU 软件 TLB 填充与复核

### 12.1 调用链

本次调用栈显示以下关系：

```text
tb_htable_lookup
  -> get_page_addr_code
     -> tlb_fill
        -> riscv_cpu_tlb_fill
           -> get_physical_address
           -> tlb_set_page
              -> tlb_set_page_with_attrs
```

其中 `get_physical_address` 与 `tlb_set_page` 是 `riscv_cpu_tlb_fill` 内先后执行的调用，并非前者调用后者。TB 是 QEMU 的翻译块缓存概念，不能与地址转换 TLB 混为一谈。

### 12.2 填充参数

在 `accel/tcg/cputlb.c:847` 的函数体位置设置临时断点：

```gdb
tbreak /home/gjhjz/OSopt/qemu-4.1.1/accel/tcg/cputlb.c:847
continue
info args
p/x vaddr
p/x paddr
p/d size
p mmu_idx
bt 5
next
```

实测参数：

```text
vaddr   = 0xffffffffc0200000
paddr   = 0x80200000
prot    = 7
mmu_idx = 1
size    = 4096
```

`next` 后到达该函数末尾第 849 行，说明 `tlb_set_page_with_attrs` 调用已经返回。

![软件 TLB 填充参数](../images/a-qemu-tlb-fill.png)

这里的 `size=4096` 表示本次 QEMU 软件 TLB 填充接口传入的页大小，不会改变原始根页表叶子项的 1 GiB 映射含义。不能根据该参数认定客体使用了最低级 4 KiB 叶子页表。

### 12.3 填充后命中条件复核

返回 `get_page_addr_code` 后，在第 1036 行设置临时断点：

```gdb
tbreak /home/gjhjz/OSopt/qemu-4.1.1/accel/tcg/cputlb.c:1036
continue
bt 5
p/x addr
p/x entry->addr_code
p/x $target_va
next
```

观察到：

```text
addr             = 0xffffffffc02000d8
entry->addr_code = 0xffffffffc0200000
$target_va       = 0xffffffffc02000d8
```

第 1036 行为：

```c
assert(tlb_hit(entry->addr_code, addr));
```

执行 `next` 后到达第 1039 行：

```c
if (unlikely(entry->addr_code & (TLB_RECHECK | TLB_MMIO))) {
```

因此，本次 TLB 填充后检查通过，目标地址所在虚拟页与取指 TLB 项对应。

![填充后的 TLB 命中检查](../images/a-qemu-tlb-recheck.png)

**证据边界：这证明同一次访问在填充后通过命中断言，不是一次独立的后续访问直接命中 TLB 的实验。** 当前没有把另一次访问在第 1030 行跳过填充分支的现场记录作为已完成结果，也没有仅凭到达第 1039 行就声称其条件分支结果。

### 12.4 结束会话与日志保存

本次执行以下命令结束宿主侧调试：

```gdb
disable breakpoints
set logging off
detach
quit
```

退出后通过 `ls -lh` 和 `tail -60` 确认 `docs/logs/a-qemu-pagewalk-body.log` 存在，大小约 13 KiB。日志尾部保存了 TLB 填充参数、调用栈、取指地址、`entry->addr_code` 及到达第 1039 行的记录。GDB 退出不影响已经写入磁盘的证据。

## 13. 分页前后与软件、硬件实现的联系

本次捕获的早期访问是 `0x1000`、`MMU_INST_FETCH`、`mmu_idx=3`；后续内核访问是 `0xffffffffc02000d8`、`MMU_INST_FETCH`、`mmu_idx=1`，并实际读取到 Sv39 模式的 `satp`。

前一条记录证明捕获了早期 M 模式取指上下文，但没有单独记录该次 `satp` 和转换返回值，不能仅据此声称完成了分页开关前后的全部对照。源码分析表明，QEMU 对 M 模式取指采用不经 Sv39 页表遍历的路径；本次 S 模式目标取指则实际经过了上述根页表叶子转换。

QEMU 通过软件结构模拟地址转换和权限检查，并借助软件 TLB 缓存翻译结果。真实 CPU 同样需要遵守 RISC-V 的地址翻译、权限和异常语义，但一般由硬件 MMU、页表遍历机制和 TLB 实现，内部结构与替换策略不由本次 QEMU 软件结构直接决定。

本实验能够观察 QEMU 中相应函数与软件缓存条目，不能由此推断真实处理器的 TLB 容量、硬件命中延迟或具体替换算法。QEMU 的 TB 缓存还可能复用已经翻译的指令块，因此重复执行同一段客体指令不一定再次经过本次记录的 C 函数调用链。

## 14. AI 辅助过程、问题与验证

AI 辅助工作包括初始化规格整理、页号和地址语义解释、实现草案、编译问题分析、双重 GDB 调试步骤，以及输出解读。实际执行和验证由我在本地完成。

| 过程 | 发现与修正 | 验证依据 |
|---|---|---|
| 物理页初始化 | 区分 `npage` 和 DRAM 页数，保留固件、内核及描述符空间 | 内存布局输出和算术核对 |
| 初次构建 | 未完成的 Best Fit 模块引用未定义状态；临时排除该文件 | 编译错误及后续 First Fit 自检 |
| 宿主调试器 | `sudo gdb` 最初不可用，需要安装宿主 GDB | 后续成功附加 QEMU 并读取源码 |
| 函数入口断点 | 入口形参显示异常，基于异常参数的条件断点不可靠 | 改在第 451 行停止后参数与调用者一致 |
| 停止线程上下文 | `__ppoll` 栈帧中无法查看翻译函数形参 | 在正确函数体断点处读取参数 |
| 地址解释 | 区分转换返回的物理页基址和完整取指地址 | `0x80200000 + 0xd8 = 0x802000d8` |
| TLB 证据解释 | 区分填充后断言与独立后续命中 | 第 1036 行执行后到第 1039 行 |

## 15. 原理联系与未覆盖内容

| 实验概念 | 原理联系 |
|---|---|
| 页描述符 | OS 用元数据管理物理页，元数据自身也需要占用并保护内存 |
| 保留区与空闲区 | 初始化建立资源边界，避免分配器覆盖固件、内核和自身状态 |
| 物理分配与虚拟映射 | 前者管理资源，后者决定地址访问关系，职责不同 |
| Sv39 多级页表 | 每级索引 9 位，叶子可以出现在不同层级，大页可提前结束遍历 |
| 访问权限 | 有效位、读写执行权限、特权级及对齐共同决定转换能否成功 |
| TLB | 缓存转换结果，减少重复页表查询；本次观察填充与填充后复核 |
| 管理器接口 | 初始化可用区逻辑与 First Fit、Best Fit、Buddy 的策略分离 |

本部分没有实现进程独立地址空间、按需分页、页面置换或交换，也没有验证所有非法 PTE、权限故障、A/D 位更新、多核并发和 TLB 失效场景。

## 16. 与陈铭宇、尚文轩的集成及阶段结论

本报告记录的实验基于我的 First Fit 阶段源码，已经完成：

1. 建立 32768 个物理页描述符，登记 31929 个初始空闲页，并通过当前管理器自检。
2. 在宿主 GDB 中捕获内核高地址取指，读取 `satp` 和根页表项。
3. 确认根级 1 GiB 叶子映射，计算完整物理地址 `0x802000d8`。
4. 观察软件 TLB 填充参数及同次访问填充后的命中断言通过。

陈铭宇交接称其已在 `lab2` 推送以下提交：

```text
f8ed38a8d6fb02e1093995eeb99d0744101eabe1  代码、测试及证据
f68e0ee51a42a64ce923eea4777b6365558d8b1e  交接文档
```

他报告已启用 `best_fit_pmm_manager`、初始空闲页数为 31928、教师评分 20/20。这些是他的交接结果，尚未作为我本机联合回归结果确认。增加代码可能改变内核末端、页表符号及空闲区边界，因此不能把他的地址或页数直接替换到本次旧日志中。

合并后应移除临时排除 Best Fit 的命令参数，执行普通清理、构建、运行和评分。随后重新构建并记录新符号，将联合版本证据保存为新文件，保留本轮日志。尚文轩的 Buddy 接入、管理器切换及联合测试还需单独验证。

尚需完成或补充：

- 核对初始化阶段截图、日志及 prompt 文件，并填入本人学号，保留教师要求的代码标记。
- 如需证明独立后续 TLB 命中，补录在实际后续访问中跳过填充分支的过程。
- 如需完整分页前后实测对照，补录分页启用前后的寄存器及转换结果。
- 在整合后的 Best Fit/Buddy 版本上完成共同环境回归，并关联准确源码版本。
- 由全组整合各自报告、真实 prompt 和证据，检查实验手册全部要求。

当前结论限于上述已观察到的初始化与地址转换路径，不代表整个 Lab 2 或所有组员任务已经完成。
