# A：物理内存初始化与调试记录

> 负责人：国峻赫  
> 当前阶段：物理内存初始化及 First Fit 联合验证；QEMU 双重 GDB 尚未完成。  
> 仓库分支：lab2。当前源码和 Makefile 位于仓库根目录。  
> 本文保存位置：docs/parts/a-memory-debug.md。

## 1. 工作范围与当前状态

本部分负责实现 `kern/mm/pmm.c` 中的 `page_init()`，从设备树解析结果建立物理页描述符，并将可用物理页交给当前分配器。后续还将分析启动页表，并通过宿主 GDB 和 RISC-V GDB 调试 QEMU 中的地址翻译与 TLB 行为。

当前已经完成初始化实现、First Fit 配置下的编译运行，并按调试步骤完成初始化状态检查和截图。已取得的启动输出显示 `check_alloc_page() succeeded!`。本阶段通过命令行排除了尚未完成的 Best Fit 源文件，因此这些结果不代表 Best Fit、Buddy 或完整 Lab 2 已通过验证。

本文中的运行地址来自本次构建。增加代码或改变链接布局后，应以新构建的符号与输出为准。提交号将在提交后通过交接消息或最终验证记录注明。

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
| 当前物理页管理器 | `default_pmm_manager`，即 First Fit |

相关材料按以下路径整理；提交前核对文件名、实际内容和链接：

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

全部 32768 页中，空闲区之前占用或保留了 839 页。其中，OpenSBI 保留区为 512 页，内核及描述符之前的对齐空间为 7 页，描述符数组为 320 页。本次布局满足 `512 + 7 + 320 + 31929 = 32768`。

DTB 输出中的 `0x87ffffff` 是最后一个字节地址，而半开区间末端 `0x88000000` 不包含在区间中，两种表示一致。

## 8. GDB 检查过程与证据整理

已经按以下流程执行初始化状态检查并保存截图，精确的 GDB 返回值以随附日志和图片为准。

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

继续执行自检后，终端不再输出。此时可在 GDB 中用 Ctrl+C 暂停，通过 `info registers pc`、`x/6i $pc` 和 `si` 检查是否执行最终循环。若反汇编为单条自跳转，单步后 PC 不变是循环行为，不是初始化失败。

## 9. AI 辅助过程与验证方式

本阶段通过与 AI 讨论确定初始化接口、页号语义、描述符布局、边界检查及调试方法，并获取实现草案。随后在本地编译和运行，以输出及调试记录检验实现。

实际交互重点包括：

1. 明确 `npage` 是绝对物理页号上界，而非直接等于 DRAM 页数。
2. 先放置并保留描述符数组，再将剩余页面交给分配器。
3. 识别未完成 Best Fit 模块导致的编译阻塞，采用临时 First Fit 构建。
4. 解释初始化完成后终端不再输出的原因，并设计 GDB 检查方法。

实际 spec、提示词、代码修改与反馈保存在 [AI 交互记录](../prompts/a-page-init.md)。该文件需保留真实对话或准确摘录，不能把事后设计但未使用的提示词写成已执行过程。

目前已知的构建阻塞如第 6 节所述；其他问题仅在实际发现并验证后记录。编译通过不等于逻辑正确，First Fit 自检通过也不等于所有异常输入和边界场景均已验证。

## 10. 与操作系统原理的联系

| 实验概念 | 原理联系及本次范围 |
|---|---|
| 页描述符 | OS 用元数据记录物理页状态；元数据自身也占用并需保护物理内存 |
| 保留区与空闲区 | 初始化建立资源使用边界，防止分配器破坏固件、内核和自身数据 |
| 页对齐 | 以完整页为单位管理，不能把不足一页的尾部当成可用页面 |
| 物理页分配与地址映射 | 前者管理物理资源，后者决定虚拟地址如何访问物理资源；二者职责不同 |
| 管理器接口 | 通过函数指针统一调用不同算法，初始化可用区的逻辑与具体分配策略分离 |

当前阶段没有实现或完整验证进程独立地址空间、缺页处理、按需分配、页面置换、交换到磁盘或多核并发内存管理。

## 11. 阶段结论、限制与下一步

本次运行表明：在默认 128 MiB 内存和 First Fit 配置下，内核成功建立 32768 个页描述符，登记 31929 个初始空闲物理页，并通过当前分配器检查。内存布局的输出与独立算术核对一致。

尚需完成或补充：

- 对照实际 GDB 日志和截图，补全第 8 节逐项检查结论。
- 与 B 的完整 Best Fit 合并后，恢复普通构建并运行教师评分。
- 由 C 基于同一初始化实现验证 Buddy。
- 准备调试版 QEMU，完成宿主 GDB 与客体 GDB 的联合调试。
- 观察实际地址翻译、页表查询和 TLB 路径，记录关键调用与分支。
- 比较分页前后行为，并讨论 QEMU 软件 TLB 与真实 CPU TLB 的逻辑区别。

QEMU 双重 GDB 的结果将在实际完成后补入本文。目前的页表地址输出不能代替该任务。
