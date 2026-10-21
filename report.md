# 操作系统实验二：物理内存管理与地址转换

| 成员 | 学号 | 主要工作 |
|---|---|---|
| 国峻赫 | 2412082 | 物理页初始化、页表与 TLB 双重 GDB 调试 |
| 陈铭宇 | 2412559 | First-Fit 分析、Best-Fit 实现及联合测试 |
| 尚文轩 | 2412456 | Buddy、SLUB、DTB 边界验证、自动调试工具及报告汇总 |

## 1. 实验目的与环境

本实验基于 RISC-V 版 ucore，研究内核启动阶段的物理内存组织、连续页分配及 Sv39 地址转换。首先从设备树获得内存范围，建立物理页描述符并保留固件、内核与元数据区域；随后通过统一的 `pmm_manager` 接口比较 First-Fit、Best-Fit 和 Buddy；在页分配器之上实现小对象缓存，并使用双重 GDB 观察页表查询及软件 TLB 行为。

实验使用 GCC 10.2.0，页大小为 4096 字节。A、B 的环境为 QEMU 4.1.1 / OpenSBI 0.4，分别使用 WSL Ubuntu 20.04 和 24.04.5；C 使用 WSL Ubuntu 22.04、QEMU 6.2.0 / OpenSBI 1.3。默认内存为 128 MiB，扩展测试另采用 64、96 和 256 MiB。

教师框架提供启动代码、页描述符接口、First-Fit 和部分检查函数。本组实现物理页初始化与 Best-Fit、Buddy，补充行为测试，并完成 SLUB、FDT 解析检查和自动调试扩展。默认管理器为 Best-Fit；Buddy 由 `PMM_BUDDY` 选择，对象测试由 `LAB2_EXTENSIONS` 启用。

## 2. 物理内存初始化

### 2.1 初始化流程与页号语义

`kern_init()` 在清零 BSS 后调用 `dtb_init()`，再通过 `pmm_init()` 选择管理器、初始化描述符并执行自检。`page_init()` 从 `get_memory_base()` 和 `get_memory_size()` 取得设备树范围，将可管理末端限制在框架支持的 `0x88000000` 以内，并向下按页对齐。

`nbase` 表示 DRAM 起始物理页号，`npage` 表示物理页号上界；描述符数量为 `npage - nbase`。默认 128 MiB 内存对应 `nbase=0x80000`、`npage=0x88000`，共 32768 个 `Page`。本次结构体大小为 40 字节，数组占用 1310720 字节。

描述符数组从链接符号 `end` 之后的第一个页边界开始。全部描述符先标为保留，再将数组末端向上取整所得的完整可用区间交给管理器。固件、内核、描述符数组及对齐间隙因而不参与分配。`end` 由链接脚本定义，表示各段布局完成后的地址边界，并非运行时填入的普通变量。

输入基址异常、大小为零、地址溢出、内核或描述符空间不足时触发 `panic`；没有完整空闲页时不调用 `init_memmap()`。虚实转换使用前先设置 `va_pa_offset=0xffffffff40000000`。

布局计算可写为：

```text
descriptor_count = npage - nbase
pages_pa         = ROUNDUP(PADDR(end), PGSIZE)
metadata_end     = pages_pa + descriptor_count × sizeof(Page)
free_begin       = ROUNDUP(metadata_end, PGSIZE)
free_end         = ROUNDDOWN(managed_end, PGSIZE)
```

描述符数组自身也占用物理内存，因此不能把 `end` 对齐后的地址直接作为空闲起点。先检查数组能否放入管理范围，再初始化描述符，可以避免在内存不足时先越界写入元数据。

### 2.2 内存布局验证

| 构建阶段 | 描述符起点 PA | 初始空闲区 | 空闲页数 | 根页表 PA |
|---|---|---|---|---|
| A 的 First-Fit 初始版本 | 0x80207000 | [0x80347000, 0x88000000) | 31929 | 0x80205000 |
| 主实验联合版本 | 0x80208000 | [0x80348000, 0x88000000) | 31928 | 0x80206000 |
| 启用 SLUB 的扩展版本 | 0x8020b000 | [0x8034b000, 0x88000000) | 31925 | 0x80208000 |

联合版本满足 `(0x88000000 - 0x80348000) / 4096 = 31928`。代码增加导致内核占用变化，表中各阶段分别对应独立构建。详细地址及摘要见 [A 初始化检查](docs/logs/a-page-init-debug.log)、[B 联合运行](docs/logs/b-integration-best-fit-run.log) 和 [扩展运行](docs/logs/extensions-runs/dtb-memory-matrix/slub-best-fit-run.log)。

联合版本的实际输出摘录如下：

```text
pages VA: 0xffffffffc0208000, PA: 0x0000000080208000
descriptor count: 32768, bytes: 1310720
free range: [0x0000000080348000, 0x0000000088000000), pages: 31928
```

其中 `1310720 = 0x140000`，故描述符数组末端为 `0x80208000 + 0x140000 = 0x80348000`。该构建的页数分解为：

| 区域 | 物理范围 | 页数 |
|---|---|---|
| 启动预留区 | [0x80000000, 0x80200000) | 512 |
| 内核、启动数据及对齐间隙 | [0x80200000, 0x80208000) | 8 |
| 页描述符数组 | [0x80208000, 0x80348000) | 320 |
| 初始空闲区 | [0x80348000, 0x88000000) | 31928 |

总计 `512 + 8 + 320 + 31928 = 32768` 页。启动预留区是框架保留边界，不表示 OpenSBI 镜像实际占满 2 MiB。GDB 检查结合上述算术关系，用于验证内核和元数据未进入可分配区。

![初始版本物理内存布局](docs/images/a-page-init-output.png)

### 2.3 动态范围与 FDT 解析

在教师 DTB 读取和 A 初始化的基础上，新增有界接口 `dtb_parse_memory(blob, length, &base, &size)`。解析器检查结构块、字符串块及属性边界，按字节读取大端整数，支持 1 或 2 个 address/size cell。截断、坏 magic、零长度、溢出和越界属性返回错误，输出保持不变。当前 PMM 管理一段连续内存，多个 bank 或更宽 cell 被显式拒绝。

宿主测试使用实际 C 解析器与独立 FDT，共通过 2230 项检查，包括全部截断点和 2000 次固定种子变异。两种管理器下的 QEMU 结果如下：

| QEMU 内存 | DTB 报告末端 | PMM 管理末端 | 描述符数 | 初始空闲页数 |
|---|---|---|---|---|
| 64 MiB | 0x84000000 | 0x84000000 | 16384 | 15701 |
| 96 MiB | 0x86000000 | 0x86000000 | 24576 | 23813 |
| 128 MiB | 0x88000000 | 0x88000000 | 32768 | 31925 |
| 256 MiB | 0x90000000 | 0x88000000 | 32768 | 31925 |

表中容量矩阵启用了 SLUB 测试，两种 PMM 得到相同的初始化页数。64、96 MiB 下分别建立 655360、983040 字节的描述符数组，而不是继续保留 128 MiB 配置的数组大小。256 MiB 与 128 MiB 的管理范围及计数相同，符合裁剪规则。

解析失败也有明确的接口结果：非法或截断输入返回 `DTB_INVALID`，缺少有效 memory 节点返回 `DTB_NO_MEMORY`，多 bank 或不支持的 cell 宽度返回 `DTB_UNSUPPORTED`。测试将输出参数预置为哨兵值，并在失败后检查其未被修改；随机变异用于补充边界覆盖，不替代这些确定性用例。

256 MiB 配置验证了探测结果与管理上限的区分，并未扩展分配上限。见 [解析检查](docs/logs/dtb-parser-tests.log)、[64 MiB Buddy](docs/logs/extensions-runs/dtb-memory-matrix/slub-buddy-memory-64-run.log) 和 [256 MiB Best-Fit](docs/logs/extensions-runs/dtb-memory-matrix/slub-best-fit-memory-256-run.log)。

## 3. First-Fit 与 Best-Fit

### 3.1 空闲块表示与 First-Fit

教师 First-Fit 按物理地址递增组织双向空闲链表。块头设置 `PageProperty`，`property` 保存连续页数，`nr_free` 统计总量。分配从链表起点寻找第一个足够大的块，取出所需前缀，剩余部分作为新块保留。释放按地址插入，并检查前后相邻块是否可合并。注册函数本身不合并多次登记的相邻区间。

若低地址有 8 页块，高地址有不相邻的 5 页块，请求 4 页时，First-Fit 选择前者并剩余 4 页；Best-Fit 选择后者并剩余 1 页。该例说明单次选择的差异，不足以判断长期碎片的优劣。

### 3.2 Best-Fit 实现

`best_fit_alloc_pages()` 遍历空闲块，选择不小于请求的最小块；同样大小时选择低地址块，精确匹配时提前结束。成功返回恰好 `n` 页并更新剩余块及计数；没有足够连续块时返回 `NULL`，链表与计数均不改变。

`best_fit_insert_block()` 复用于登记和释放，负责按地址插入与双侧合并。计数由调用者更新，合并仅改变块结构，被吸收块头的标志与长度同时清除。教师 `basic_check()`、`best_fit_check()` 保留，通过包装回调追加独立测试。

分配与释放持续维护三个不变量：空闲链表按地址有序；所有空闲块长度之和等于 `nr_free`；相邻可合并块在回收后不再分离。地址顺序使释放时只需检查插入位置的前后块，最佳匹配则需比较候选长度。两种顺序分别服务于回收和选择，因此按地址维护链表并不等同于 First-Fit 策略。

设空闲块数量为 `k`，当前分配搜索和释放定位最坏均为 O(k)。按大小增加索引可减少候选搜索，但还需维护用于合并的地址索引，增加拆分和合并成本。Next-Fit 游标可减少重复扫描，同样不保证改善碎片。当前未引入上述优化。

### 3.3 行为测试

`best_fit_test.c` 通过公共接口暂时取出初始空闲区，在其中构造 32 页布局，排除背景块对选择和失败用例的影响。结束后归还整个区间，核对原块地址、块数、长度、空闲总量及保留页数量。

| 场景 | 关键断言 | 结果 |
|---|---|---|
| 最佳匹配 | 8/5/7 页块请求 4 页，选择 5 页块 | 通过 |
| 精确匹配与同大小选择 | 无余块；同大小按低地址选择 | 通过 |
| 拆分 | 10 页块取 3 页，余块位置和长度正确 | 通过 |
| 前、后及双侧合并 | 恢复连续块，旧块头清除 | 通过 |
| 碎片失败 | 三个分离的 4 页块不能满足 6 页请求 | 通过 |
| 耗尽与状态恢复 | 失败不修改状态；多轮后恢复布局 | 通过 |

B 的 QEMU 4.1.1 联合验证恢复 31928 个空闲页，教师评分为 20/20。见 [行为日志](docs/logs/b-integration-best-fit-run.log)、[评分日志](docs/logs/b-integration-grade.log)。该测试依赖 Best-Fit 的区间释放语义，不用于 Buddy。

2026-10-21，B 在扩展合入版本 `35c479d` 上清理重建默认 Best-Fit，再次通过全部行为检查，恢复 31928 页，普通教师评分仍为 20/20。见 [最新基础回归](docs/logs/b-extension-20261021-102252/baseline-best-fit-run.log)、[普通评分](docs/logs/b-extension-20261021-102252/default-grade.log)。启用完整 SLUB 自检的构建在 128 MiB 下恢复 31925 页；这是不同构建的内核占用差异，不改写早期日志的 31928 页。

最佳匹配用例在偏移 0、12、22 处分别构造 8、5、7 页空闲块。请求 4 页应返回偏移 12，留下偏移 16 的 1 页块，另外两个块不变。它可以区分“找到第一个足够块”和“找到最小足够块”，而仅检查返回非空无法证明 Best-Fit 选择正确。

碎片用例保留三个互不相邻的 4 页块，总量为 12 页。6 页请求仍应失败，因为不存在满足请求的连续区间；随后比较失败前后的布局和计数，可以验证搜索失败没有摘链或扣减计数。三种合并用例分别改变释放顺序，以覆盖仅前邻、仅后邻及双侧相邻的路径。

运行日志中的部分结果为：

```text
[Best-Fit behavior] minimum adequate block: PASS
[Best-Fit behavior] merge both neighbours: PASS
[Best-Fit behavior] fragmented and exhausted failures: PASS
[Best-Fit behavior] original state restored: PASS (31928 free pages)
```

最后一项同时检查原始块的位置与长度、块数和保留页数量，因而不仅验证空闲总量恢复，也检查临时构造布局没有遗留在管理器中。

## 4. Buddy System

### 4.1 组织方式与接口

Buddy 按 0～15 阶分类，第 `k` 阶块包含 `2^k` 页，每阶维护一条空闲链表。块头复用 `Page` 的 `property` 和标志位，不改变公共描述符。空闲块头记录实际块页数；分配块头使用私有标志，记录原始请求。内部页不设置块头标记。

请求 `n` 页时，占用大小为 `2^ceil(log2(n))`。申请 3 页扣除 4 页，释放调用仍为 `free_pages(base, 3)`。接口接受原始块头和原始请求，不支持内部页或部分释放。空闲计数按实际占用页数更新。

零页请求沿用框架断言约定；请求超过最大阶或没有合适块时返回 `NULL`，状态不变。释放前要求块内引用计数为零，并检查已分配块头、请求页数与边界。

### 4.2 初始化、拆分与合并

初始区间的起点和长度通常不满足大块条件。登记时选择同时满足物理对齐与剩余长度的最大块，逐段覆盖全部区间，保留头尾页，并允许多次登记后的合法伙伴合并。

分配从最低足够非空阶取块，逐阶将右半块放回低一阶链表，保留左半块。释放的伙伴由绝对物理页号计算：

```c
buddy_ppn = page2ppn(base) ^ (1UL << order);
```

只有伙伴位于登记边界内且为同阶空闲块头时才合并，随后继续检查上一阶。对齐区间偏移 1 和 2 虽相邻，各自伙伴却为 0 和 3，不能合成 2 页块。拆分和合并不改变空闲总量。

分配的阶数搜索与拆分为 O(MAX_ORDER)。释放中的合并同为 O(MAX_ORDER)，但实现还检查每个回收页的描述符，因此释放总复杂度为 O(s + MAX_ORDER)，`s` 为实际块页数。规则化块尺寸简化伙伴查找，同时引入取整造成的内部碎片。

以对齐的 64 页测试区为例，请求 3 页后实际返回 4 页块。下表偏移相对于该区间起点，是拆分规则对应的布局：

| 区间 | 页数 | 状态 |
|---|---|---|
| [0, 4) | 4 | 已分配，记录原请求 3 |
| [4, 8) | 4 | 2 阶空闲块 |
| [8, 16) | 8 | 3 阶空闲块 |
| [16, 32) | 16 | 4 阶空闲块 |
| [32, 64) | 32 | 5 阶空闲块 |

空闲总量为 `4 + 8 + 16 + 32 = 60`。按原请求释放后，偏移 0 与 4 先合为 8 页，再依次与偏移 8、16、32 的伙伴合并，恢复 64 页。块内未使用的第 4 页仍属于该分配，调用方不能单独释放。

### 4.3 测试与结果

自检从真实分配器借出对齐的 64 页，在独立状态中构造布局，通过公共页接口操作。结束时将原块归还主状态，检查链表、对齐、块内标志、合并及计数。

| 测试 | 验证内容 | 结果 |
|---|---|---|
| 六级拆分与回收 | 单页分配后 0～5 阶各有余块，释放恢复 6 阶块 | 通过 |
| 多种请求 | 1/2/3/4/5/8/9/16/17/32/33/64 页的对齐与取整 | 通过 |
| 相邻非伙伴 | 偏移 1、2 保持独立，2 页请求失败 | 通过 |
| 碎片与耗尽 | 全部分配无重叠；仅偶数页空闲时不能申请 2 页 | 通过 |
| 不规则初始化 | 非对齐 37 页与两段 8 页组成的 53 页集合完整可回收 | 通过 |
| 混合负载 | 1024 次操作，以独立占用表核对重叠、计数和失败条件 | 通过 |

主实验版本自检前后均为 31928 页，启用 SLUB 的后续构建中均为 31925 页。见 [初次验证](docs/logs/c-buddy-run.log)、[A 联合验证](docs/logs/a-after-c-buddy-run.log)、[扩展版本](docs/logs/extensions-runs/dtb-memory-matrix/slub-buddy-run.log)。

单页分配用例由一个 6 阶块连续拆分，完成后 0～5 阶各有一个余块，合计 63 页；回收后检查 6 阶块数为 1。碎片用例将 64 页逐页分配，再只释放偶数页，此时虽然有 32 页空闲，仍无法组成一个 2 页伙伴块；释放奇数页后才逐级恢复。两种情况分别检验拆分结构和伙伴关系，而非仅检查页数相加。

不规则登记用例先登记偏移 [3,40) 的 37 页，再登记 [48,56) 和 [40,48) 两段。分配全部单页后，将实际返回集合与 [3,56) 的 53 个位置逐一比较，检查没有丢页、重复或越界。1024 次混合负载另维护逐页占用表：成功分配的页必须原先未占用，失败时则检查不存在足够大的对齐空块。

```text
[Buddy] split six levels and merge back: PASS
[Buddy] powers of two and rounded requests (3 -> 4): PASS
[Buddy] non-buddy neighbours, fragmentation, exhaustion: PASS
[Buddy] unaligned 37-page init, multiple ranges, no lost pages: PASS
[Buddy] 1024 mixed operations with occupancy oracle: PASS
[Buddy] state restored: 31928 -> 31928 free pages: PASS
```

上述摘录来自主实验构建。扩展构建使用相同断言，以其运行时初始页数进行恢复检查。

开发中曾观察到隔离测试链表总页数 127 与计数 63 不一致。GDB 显示测试区合并了主状态中的区外伙伴。增加每个状态的登记页号边界后修复，完整自检通过。见 [定位日志](docs/logs/c-buddy-debug.log)。

## 5. SLUB 对象分配扩展

### 5.1 页内对象缓存

页分配器的最小单位为 4 KiB，直接用于小对象会造成较大浪费。`slub.c` 在页内保存 slab 头，将余下空间划为相同跨度的对象。空闲对象自身保存 next 指针，页头保存缓存归属、占用计数和分配位图；缓存使用 partial、full、empty 链表组织页面，每缓存最多保留一张空页。

命名缓存支持对象大小、对齐和构造回调。最小实际对齐为 16 字节，构造回调在每次分配时执行；存在活对象时拒绝销毁，`shrink` 只回收空页。固定 24 个缓存描述符中，8 个用于通用大小类，另有 16 个可用于命名缓存。

`kmalloc()` 对 1～2048 字节请求选择 16/32/64/128/256/512/1024/2048 字节缓存，更大请求由连续页分配器处理。大对象头记录原始申请页数，使 `kfree()` 同时适配 Best-Fit 与 Buddy 的释放契约。零尺寸、超大尺寸、缓存描述符耗尽或物理页不足时返回 `NULL`。

本实现采用单核串行的教学 SLUB 结构，未实现 Linux SLUB 的每 CPU 缓存、NUMA 和远程释放协议。

对象在页内的布局由以下关系确定：

```text
stride   = ROUNDUP(object_size, alignment)
offset   = ROUNDUP(sizeof(slab), alignment)
capacity = (PGSIZE - offset) / stride
```

通过已有目标文件的调试信息核对，本次 ABI 下 `sizeof(slab)=80`。对于测试中的 37 字节对象与 64 字节对齐，`stride=64`、`offset=128`，单页容量为 `(4096-128)/64=62` 个对象。因此第 63 次申请需要第二张物理页，而不是继续在第一页内越界写入。

| 页面状态 | 占用情况 | 后续行为 |
|---|---|---|
| partial | 至少一个活对象且仍有空闲槽 | 优先复用空闲槽 |
| full | 所有槽均被占用 | 转向其他 partial/empty 页或申请新页 |
| empty | 活对象数为零 | 每缓存至多保留一页，多余空页立即回收 |

对象释放时检查缓存归属、槽起点和分配位图，再将槽接回空闲链。这使错误缓存、内部指针和重复释放能够在对应条件处被拒绝。

大对象采用不同路径。本次大对象头为 32 字节，例如申请 8193 字节，需要 `ceil((8193+32)/4096)=3` 页；Best-Fit 实际取得 3 页，Buddy 则取得 4 页。头部仍记录原始申请 3 页，回收时传回 3，从而由底层管理器按其契约释放实际块。

### 5.2 功能与失败路径验证

| 场景 | 判据 | 结果 |
|---|---|---|
| 命名缓存 | 37 字节对象满足 64 字节对齐，构造次数正确 | 通过 |
| 缓存状态转移 | 跨两页分配、复用、空页回收，活对象阻止销毁 | 通过 |
| 尺寸边界与大对象 | 27 个边界请求保持各自字节图案 | 通过 |
| 页共享 | 64 个 16 字节对象共占一张物理页 | 通过 |
| 混合负载 | 1024 次操作中区间不重叠，数据保持完整 | 通过 |
| 资源耗尽 | 缓存元数据与真实物理页耗尽后失败，释放后恢复 | 通过 |
| 非法释放 | 重复释放、错误缓存、内部指针分别触发断言 | 通过 |

正常自检和非法释放分别启动；后者共在两种页管理器下运行六次，以预期断言作为判据。扩展版本的两种正常运行均恢复 `31925 → 31925` 页。见 [Best-Fit 下的 SLUB](docs/logs/extensions-runs/dtb-memory-matrix/slub-best-fit-run.log)、[Buddy 下的 SLUB](docs/logs/extensions-runs/dtb-memory-matrix/slub-buddy-run.log) 和 [独立重复释放检查](docs/logs/extensions-runs/slub-validation/slub-best-fit-negative-1-run.log)。

B 于 2026-10-21 在 QEMU 4.1.1 / OpenSBI 0.4 下独立复测：两种 PMM 的完整 SLUB 自检在 64、96、128、256 MiB 下均通过，六次非法释放均命中各自预期断言。128 MiB 下两种模式仍恢复 31925 页。见 [B 的 Best-Fit 运行](docs/logs/b-extension-20261021-102252/slub-best-fit-run.log)、[Buddy 运行](docs/logs/b-extension-20261021-102252/slub-buddy-run.log) 及 [矩阵与断言核对结果](docs/logs/b-extension-20261021-102252/results.json)。

命名缓存测试在第 63 个对象分配后检查 slab 数量为 2、活对象数为 63，空闲物理页数减少 2。释放一个槽再分配时返回同一地址，并重新执行构造回调；释放所有对象后只保留一个空页，`shrink` 再回收这一页，页数恢复到初值。这一过程同时覆盖缓存复用和页面生命周期。

```text
[SLUB] aligned cache, ctor, reuse, full/partial/empty, shrink: PASS
[SLUB] 64 small objects share one physical page: PASS
[SLUB] 1024 mixed objects, nonoverlap and byte-pattern oracle: PASS
[SLUB] real page exhaustion, allocation failure, full recovery: PASS
[SLUB] state restored: 31925 -> 31925 free pages: PASS
```

混合测试对每个活对象写入独立字节图案，回收前逐字节检查，并比较所有活对象区间。这比只检查分配成功和最终页数更能发现对象重叠。耗尽测试则真正取出全部空闲物理页，验证小对象和大对象申请均返回 `NULL`；逐页归还后重新申请成功，证明失败路径没有损坏缓存。

重复释放的独立运行记录为：

```text
[SLUB negative] case 1: expect assertion panic
kernel panic at kern/mm/slub.c:177:
    assertion failed: slab->allocated[index / 64] & bit
```

第一次释放已清除分配位，第二次释放因此在位图检查处停止。正常测试中出现同样 panic 应视为失败；这里的成功判据是指定负例触发了预期保护条件。

固定大小类减少了小对象独占整页的开销，但仍存在取整、页头和空页保留成本。保留空页有利于复用，显式 `shrink` 则将其归还底层分配器。大对象仍受连续空间和 Buddy 取整限制。

## 6. 页表与 TLB 调试

### 6.1 双重 GDB 与 Sv39 页表遍历

客体 RISC-V GDB 通过 QEMU stub 控制内核，宿主 GDB 调试 QEMU 进程。A 在 `riscv_cpu_tlb_fill()` 的函数体中捕获内核取指，并进入 `get_physical_address()` 观察页表读取。选择函数体断点是因为函数入口处的形参位置曾与调用者不一致，越过序言后参数恢复正常。

First-Fit 历史构建的目标 VA 为 `0xffffffffc02000d8`，`satp=0x8000000000080205`：MODE 为 8，根页表 PA 为 `0x80205000`。Sv39 的 VPN[2:0] 各为 9 位，PTE 为 8 字节；该地址的根级索引为 511，因而：

```text
PTE_PA = 0x80205000 + 511 × 8 = 0x80205ff8
PTE    = 0x200000cf
PPN    = PTE >> 10 = 0x80000
```

标志 V、R、W、X、A、D 置位，根项为叶子，物理页号低 18 位为零，满足 1 GiB 大页对齐。此次转换只读取根项；Sv39 支持三级页表，不意味着每次转换都读取三级。

该虚拟地址的分解为 VPN[2]=511、VPN[1]=1、VPN[0]=0、页内偏移 `0xd8`。PTE 低 8 位 `0xcf` 的解释如下：

| 字段 | 值 | 本次含义 |
|---|---|---|
| V | 1 | 条目有效 |
| R/W/X | 1/1/1 | 具有读、写、执行权限，是叶子项 |
| U/G | 0/0 | 未设置用户访问和全局映射标志 |
| A/D | 1/1 | 已设置访问与修改标志 |

根级叶子需要将下方两级虚拟页号补入物理页号。QEMU 中本次计算可以核对为：

```text
vpn_low18 = (VA >> 12) & ((1 << 18) - 1) = 0x200
PA_page   = (0x80000 | 0x200) << 12      = 0x80200000
PA        = PA_page + 0xd8              = 0x802000d8
```

因此 PTE 指向的 1 GiB 物理范围基址为 `0x80000000`，而目标取指所属的 4 KiB 页基址为 `0x80200000`，两者属于不同粒度。

转换路径生成目标所在的 4 KiB 物理页基址 `0x80200000`，加页内偏移 `0xd8` 得到完整地址 `0x802000d8`。见 [宿主页表遍历日志](docs/logs/a-qemu-pagewalk-body.log)。

![历史构建的根页表项读取](docs/images/a-qemu-root-pte.png)

### 6.2 软件 TLB 填充与直接命中

历史会话观察到 `tlb_set_page()` 接收虚拟页 `0xffffffffc0200000`、物理页 `0x80200000`、权限 7、大小 4096。该大小描述本次软件 TLB 填充，不改变客体根级 1 GiB 叶子映射的含义。同次访问在填充后通过 `tlb_hit()` 断言。

关键调用关系为：

```text
get_page_addr_code
  -> tlb_fill
     -> riscv_cpu_tlb_fill
        ├─ get_physical_address       页表查询与权限检查
        └─ tlb_set_page
           -> tlb_set_page_with_attrs 软件条目填充
```

其中查表和填充是 `riscv_cpu_tlb_fill()` 内先后发生的两个调用，不是查表函数调用填充函数。观测到的填充参数如下：

| 参数 | 实际值 |
|---|---|
| vaddr | 0xffffffffc0200000 |
| paddr | 0x80200000 |
| prot | 7，即 R/W/X |
| mmu_idx | 1 |
| size | 4096 字节 |

联合版本另一次会话在 `get_page_addr_code()` 第 1030 行停止，入口已有：

```text
addr             = 0xffffffffc02000d8
entry->addr_code = 0xffffffffc0200000
```

执行 `next` 后到第 1039 行，跳过包含 `tlb_fill()` 的未命中分支，说明这次后续取指地址查询直接复用已有条目。该日志不能确定客体执行 `kern_init` 的次数。见 [直接命中日志](docs/logs/a-qemu-tlb-hit-after-c.log)。

两种证据的区别在于判断发生的时刻：历史会话在未命中后完成填充，再检查标签匹配；联合会话在进入未命中判断之前已有匹配项。两份日志来自不同构建和会话，分别支持相应路径，而不组成同一条目的连续生命周期记录。

![联合版本的软件 TLB 直接命中](docs/images/a-qemu-tlb-hit-after-c.png)

### 6.3 分页切换前后对照

| 状态 | PC | SP | satp |
|---|---|---|---|
| 联合版写 satp 前 | 0x80200034 | 0x8001bd80 | 0 |
| 联合版高地址入口 | 0xffffffffc02000d8 | 0xffffffffc0206000 | 0x8000000000080206 |

分页前 MODE 为 Bare；分页后为 Sv39，根页表 PA 为 `0x80206000`。栈顶与页表起始 VA 相同，分别表示向低地址增长的栈上界和向高地址存放的页表起点，并不重叠。低地址断点在分页后重插入失败的问题，通过禁用该断点再继续到高地址入口解决。见 [入口成功补录](docs/logs/a-paging-transition-confirm.log)。

对照表记录的是写入前和成功到达入口后的两个状态，不是写寄存器指令本身的所有中间状态。入口处反汇编为 `auipc a0,0x7`，函数尚未执行后面的栈帧调整；因此 SP 的值应按停止位置解释，不能仅依据 GDB 显示的 C 源码行推断 `memset` 已经执行。

### 6.4 自动地址查询与单步演示

自动工具从当前 ELF 反汇编定位 satp 写指令，从符号表读取内核入口和根页表，启动自己的 QEMU/GDB 会话。实测 Bare 模式的单步使 PC 从 `0x80200030` 前进到 `0x80200034`；高地址单步使 PC 从 `0xffffffffc02000d8` 前进到 `0xffffffffc02000dc`。当前 `satp` 与根页表实时读取相符，目标物理地址为 `0x802000d8`。

工具另以 17项独立 fixture 检查 4 KiB、2 MiB、1 GiB 叶子、非规范地址、非法权限和大页对齐。回放页面展示已记录的寄存器与 PTE，不重新执行客体。它补充了可复现的客体查询流程，宿主软件 TLB 结论仍由 A 的独立调试支持。

实时记录还包含两种失败查询：

| 查询 | 观测或检查结果 | 含义 |
|---|---|---|
| VA=0 | 根索引0，PTE=0，报告 V=0 | 当前启动页表没有该映射 |
| VA=0x4000000000 | 在读取 PTE 前报告 noncanonical | 不满足 Sv39 高位符号扩展要求 |

这些查询用于验证工具的错误说明，并非通过客体执行故意触发两次访存异常。独立 fixture 则补充了真实启动页表中没有出现的其他叶子层级与非法条目。

![自动演示中的实时页表查询记录](docs/images/extensions-address-query.png)

见 [自动 GDB 日志](docs/logs/address-demo/live-confirm/gdb.log)、[结构化记录](docs/logs/address-demo/live-confirm/trace.json)、[离线回放](docs/logs/address-demo/live-confirm/replay.html) 及 [解码测试](docs/logs/address-demo/live-confirm/decoder-tests.log)。

## 7. 综合验证

### 7.1 测试层次与结果

测试分为框架检查、算法行为检查、对象分配检查和调试观察。算法测试通过公共接口改变状态，配合描述符、计数和独立占用表验证；地址转换测试将合成页表判据与实际 GDB 记录分开。

| 验证项 | 范围 | 实际结果 |
|---|---|---|
| 教师评分 | Page Init、Best-Fit | A/B 原环境及 C 本机均为 20/20 |
| Best-Fit 行为 | 选择、拆分、三类合并、失败和恢复 | 全部通过 |
| Buddy | 取整、拆分、伙伴合并、碎片及 1024 次混合操作 | 全部通过 |
| SLUB | 缓存、大小类、大对象、混合负载、耗尽及六次预期异常 | 两种 PMM 下通过 |
| DTB | 2230 项解析检查、四种内存容量 | 通过 |
| 地址转换 | 双重 GDB、切换对照、直接命中及自动查询 | 获得对应日志 |

主实验联合版在 A、B 的 QEMU 4.1.1 环境通过。新增 SLUB、DTB 和自动演示先在 C 的 QEMU 6.2.0 环境验证；2026-10-21，B 在 QEMU 4.1.1 / OpenSBI 0.4 下完成 SLUB、DTB 及两种 PMM 的独立回归，普通教师评分为 20/20。本轮 B 未运行自动 GDB 演示，该项仍引用 A/C 的既有证据。教师评分不包含 Buddy、SLUB 或调试结果。

C 的扩展后默认评分见 [教师评分记录](docs/logs/extensions-runs/dtb-memory-matrix/default-grade.log)。该容量矩阵中，两种 PMM 与 SLUB 共完成八次正常启动。相应 [源码摘要与审计](docs/logs/extensions-final-audit.log) 用于关联版本；报告结构调整不改变这些代码和结果。

B 本轮被测内核源码为 `35c479da0ca0ba300a777cf1830808857b5c3f76`，未修改算法和教师检查。环境与源码摘要见 [B 环境记录](docs/logs/b-extension-20261021-102252/environment.log)，完整索引及复现说明见 [B 扩展复测记录](docs/notes/b-extension-review-2026-10-21.md)。

| B 本轮验证 | 实际结果 | 独立证据 |
|---|---|---|
| 两种 PMM × 四种内存容量的 SLUB | 八次正常自检通过；64/96/128/256 MiB 分别恢复 15701/23813/31925/31925 页 | [矩阵结果](docs/logs/b-extension-20261021-102252/results.json) |
| 两种 PMM × 三类非法释放 | 重复释放、错误缓存、内部指针分别命中预期断言，六次通过 | 同上，逐项保存断言表达式及原始日志 |
| 宿主 C FDT 解析器 | 2230 项检查通过 | [解析日志](docs/logs/b-extension-20261021-102252/dtb-parser.log) |
| 关闭扩展后的 Buddy、Best-Fit | 两种基础自检通过，均恢复 31928 页 | [Buddy](docs/logs/b-extension-20261021-102252/baseline-buddy-run.log)、[Best-Fit](docs/logs/b-extension-20261021-102252/baseline-best-fit-run.log) |
| 普通教师评分与最终产物 | 20/20；最终镜像为默认 Best-Fit | [评分](docs/logs/b-extension-20261021-102252/default-grade.log)、[产物检查](docs/logs/b-extension-20261021-102252/default-artifact.log) |

评分脚本的关键输出为：

```text
-check page init: OK
-check best fit:  OK
Total Score: 20/20
```

该结果说明教师检查入口通过；算法的特定性质仍由独立用例支撑。例如，“最佳匹配”需要验证选择的块，“伙伴合并”需要验证阶结构，“对象隔离”需要检查区间和内容。最终空闲页数恢复可检出计数泄漏，但单凭计数不能排除两次返回重叠地址，因此还需占用表和对象数据校验。

### 7.2 复现方法

在仓库根目录、已激活工具链的环境中，默认模式执行：

```bash
make clean
make
make qemu
```

退出 QEMU（Ctrl+A，松开，再按 X）后运行 `make grade`。C 的 OpenSBI 1.3 使用半主机控制台，需将输出转交给评分脚本读取：

```bash
make grade QEMU='python3 tools/qemu-grade-console.py'
```

转接脚本仅路由控制台输出，不修改评分断言。A/B 的 OpenSBI 0.4 环境使用普通评分。

Buddy 与对象测试分别使用以下编译宏；每次切换先执行 `make clean`，再重建并重新启动：

```bash
make DEFS=-DPMM_BUDDY
make qemu DEFS=-DPMM_BUDDY

# 退出 QEMU，清理后再启用对象测试
make clean
make DEFS='-DPMM_BUDDY -DLAB2_EXTENSIONS'
make qemu DEFS='-DPMM_BUDDY -DLAB2_EXTENSIONS'
```

自动验证入口：

```bash
python3 tools/test-dtb-parser.py
python3 tools/check-extensions.py --negative-tests --memory-matrix
python3 tools/test-sv39.py
python3 tools/demo-address-translation.py
```

B 的 OpenSBI 0.4 环境使用以下入口完成扩展矩阵、非法释放和普通评分；不带 `--native-grade` 时保留 C 原有的控制台转接评分方式：

```bash
python3 tools/check-extensions.py --native-grade --negative-tests --memory-matrix \
    --log-dir "docs/logs/b-extension-$(date +%Y%m%d-%H%M%S)"
```

日志目录必须尚不存在。完整矩阵、预期异常和解析检查分别保存，不将预期 panic 计作正常启动成功。

各次运行保存到新目录，保留早期证据。测试脚本在内核完成输出后结束其空转进程；正常日志末尾的终止信号不是 panic 判据。

## 8. 分析与讨论

### 8.1 分配策略的适用层次

物理页初始化确定资源边界，PMM 决定页块如何分配，SLUB 进一步将页划分为对象，而页表规定虚拟地址如何访问物理资源。这几层通过接口组合，但职责不同：成功申请物理页不会自动建立新的虚拟映射。

First-Fit 和 Best-Fit 都可保留任意长度的块。前者缩短某些请求的搜索，后者优先消耗最小足够块；两者都可能因外部碎片而在总空闲量足够时失败。Buddy 的尺寸限制使伙伴定位和合并更规则，但申请 17 页需占 32 页。SLUB 适合重复的小尺寸对象，降低整页分配浪费，同时承担大小类取整、页头和空页缓存成本。不同策略的优劣应结合请求序列与工作负载评价。

| 策略或层次 | 分配单位与选择 | 主要代价 | 本实验中的直接证据 |
|---|---|---|---|
| First-Fit | 任意长度页块，首个足够块 | 线性搜索、外部碎片 | 教师实现分析与选择示例 |
| Best-Fit | 任意长度页块，最小足够块 | 通常遍历候选，可能留下小余块 | 8/5/7页选择用例、碎片失败 |
| Buddy | 按阶向上取整 | 内部碎片、完整块回收约束 | 3页请求占4页、非伙伴不合并 |
| SLUB | 固定尺寸对象槽及大对象页块 | 对齐与元数据、空页缓存 | 64小对象共享1页、shrink恢复 |

本实验没有测量分配耗时、缓存命中率或长期工作负载下的碎片分布，因此上述比较限于算法结构与已验证行为，不给出某策略整体性能更优的结论。

### 8.2 地址转换与缓存

多级页表以额外查表开销换取稀疏映射的空间效率，大页可在高层提前结束遍历。TLB 缓存地址转换结果，减少重复查表。QEMU 的软件条目便于调试，但其数据结构、填充粒度与 TB 缓存行为不能用于推断真实处理器的容量、延迟或替换算法。

### 8.3 实验范围

本实验覆盖单核启动阶段、单段物理内存和现有启动页表。FDT 扩展未实现多 bank、所有保留区或任意设备树语义；PMM 保持 128 MiB 上限。教学 SLUB 未实现多 CPU、NUMA 与远程释放。实验也未实现进程独立地址空间、按需分页、页面置换、交换和并发分配。这些问题属于后续内存管理工作的扩展方向。

## 9. AI 辅助开发与验证

AI 用于需求整理、实现、测试构造、调试脚本和结果分析。扩展规格采用 Prompt（任务及修改范围）、Rely（最小依赖）、Guarantee（交付接口）和 Specification（前置条件、分支与后置状态），将“满足何种行为”与具体实现步骤区分。

| 模块 | 规格与过程记录 | 主要验证依据 |
|---|---|---|
| 初始化 | [A 规格记录](docs/prompts/a-page-init.md) | 内存布局与 GDB；实际交互条目仍需作者补齐 |
| Best-Fit | [实现](docs/prompts/b-best-fit.md)、[测试](docs/prompts/b-best-fit-tests.md) | 教师检查和独立行为用例 |
| Buddy | [实现](docs/prompts/c-buddy.md)、[测试](docs/prompts/c-buddy-tests.md) | 阶结构、独立占用表及状态恢复 |
| SLUB | [对象分配规格](docs/prompts/slub.md) | 数据图案、区间不重叠及真实耗尽 |
| DTB | [解析规格](docs/prompts/dtb-memory.md) | 合成输入与真实容量矩阵 |
| 自动调试 | [演示规格](docs/prompts/address-demo.md) | 合成页表与实时 GDB |

迭代中，Buddy 测试曾越过隔离范围合并空闲块，修正登记边界后恢复一致；宿主解析测试因 PATH 选择了 RISC-V 汇编器，改为显式指定宿主 binutils；自动演示因 GDB 返回有符号 `satp` 而误判模式，按 64 位无符号解释后通过。上述问题均保留对应运行或定位记录。

Buddy 两份文档包含后续整理的规格，原始交互保留在会话中；DTB 和演示规格在各自编码前写入。规格整理与原始提示词的来源分别注明。AI 生成结果以实际代码、断言和运行证据核验，不能代替成员对实现的理解。
