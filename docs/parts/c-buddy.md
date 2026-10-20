# 成员 C：Buddy System 设计、实现和验证

负责人：尚文轩，学号 2412456。首次验证：2026-10-19；交接状态更新：2026-10-20。

## 1. 基线、职责和进度判断

初次开发基于 `lab2` 的 `a3574e2b288674f3d0045d6a71753bd94c5327ef`，该基线包含 A 的 `79b5791` 初始化、B 的 `f8ed38a`/`f68e0ee` 以及 A 的调试材料合并。C 的 Buddy 与材料已提交为 `b5c05e9706bedefb18cd7f6c3ad0b2404a8e5869`，并已包含在远程 `lab2`。旧 c-* 日志保留当时未提交工作树的 HEAD 与 SHA256，它们是 10 月 19 日开发阶段的真实记录，不改写为后来的提交状态。

A 已实现 DTB 初始化、页描述符及保留区域处理，提供 First-Fit 实测和 Sv39 大页、TLB 填充证据。B 已实现并验证 Best-Fit。C 在 10 月 19 日补齐 Buddy、自检、两份规格记录和总报告，并在本机回归默认模式。10 月 20 日同步至 `cf881a985919d78a773bd77c09998f324f603d27` 后，已核对 A 的 `cc646e0`/`6baf620` 联合回归与调试补证、B 的 `c3c4dd9`/`cf881a9` 联合验收材料。以下 C 构建数值仍指初次验证；后续联合结果见第 7 节。

## 2. 环境与启用方式

| 项目 | C 本次实际环境 |
|---|---|
| 操作系统 | Windows 上的 WSL Ubuntu 22.04 |
| 工具链 | 用户目录解包安装，GCC 10.2.0 |
| QEMU / 固件 | QEMU 6.2.0 / OpenSBI 1.3 |
| 物理内存 | 默认 128 MiB，DTB 区间 [0x80000000, 0x88000000) |
| 内核页大小 | 4096 字节 |

本环境不同于 A/B 的 QEMU 4.1.1、OpenSBI 0.4；本轮没有在他们的机器复测，不把本机结果说成共同环境验收。

在仓库根目录执行：

```bash
source ~/OSopt/env.sh
make clean
make DEFS=-DPMM_BUDDY
make qemu DEFS=-DPMM_BUDDY
# 退出：Ctrl+A，松开，再 X
```

恢复默认 Best-Fit：

```bash
make clean
make
make qemu
# 退出 QEMU 后，本机评分使用控制台转接：
make grade QEMU='python3 tools/qemu-grade-console.py'
```

一键完成两种模式的构建、启动检查和评分，并留下默认 Best-Fit 构建：

```bash
source ~/OSopt/env.sh
python3 tools/check-buddy.py
```

2026-10-20 更新：一键脚本把每次新证据写入 `docs/logs/c-runs/<运行时间>/`，拒绝复用已有目录，保留原 `c-*` 历史日志。可用 `--log-dir` 指定仓库内的新目录；记录中的工作树状态来自实际 Git 查询，不再固定声称“未提交”。

宏变化不会自动使旧对象失效，手动切换必须先 clean；每次都重新启动，不能在运行中更换管理器。本轮不改 Makefile，保持默认 Best-Fit，仅在 `pmm.c` 增加条件 include 和选择分支。

## 3. 数据结构与接口契约

源码为 `kern/mm/buddy_pmm.c/.h`，导出 `buddy_pmm_manager`。按 0..15 阶组织双向空闲链表，第 k 阶包含 2^k 页，最大单块 32768 页，即 128 MiB；最大块受真实空闲范围和对齐约束，实际不保证存在。

| 页状态 | flags | property |
|---|---|---|
| 保留页 | PG_reserved | 不参与分配 |
| 空闲块头 | PG_property | 实际块页数，必为 2 的幂 |
| 已分配块头 | 私有 BUDDY_ALLOCATED，第 2 位 | 原始请求 n |
| 块内部页 | 无上述块头位 | 0 |

每个状态还有空闲页总数和登记物理页号上下界。无需修改 `struct Page` 或公用管理器接口，私有位只在 Buddy 中解释。以后若公共 flags 增加新用途，需要重新核对第 2 位是否冲突。

`alloc_pages(n)` 返回连续且按实际块大小对齐的物理页块；n=3 时占用 4 页，`nr_free_pages()` 减少 4。调用者必须 `free_pages(base, 3)`，释放按已存原始请求识别整个 4 页块并使计数增加 4。传入 4、内部页指针、重复释放、部分释放均不支持；释放前引用计数必须为 0。非正页数按教师接口使用断言，超最大阶及 SIZE_MAX 请求返回 NULL。

Best-Fit 支持任意长度和部分区间回收，其教师测试不符合 Buddy 的完整块释放契约，因此只通过各管理器自己的 `.check` 调用相应测试。当前是单核启动自检，不包含锁；并发环境需调用方串行化。

## 4. 初始化、分配与释放原理

`page_init()` 仍由 A 实现，Buddy 的 `init_memmap(base,n)` 接受其登记的保留页区间。先清理该区间的描述符，再不断选择同时满足物理页号对齐和剩余长度的最大 2 的幂块。即使空闲起点不对齐、总页数不是 2 的幂，也不会舍弃头部或尾部。登记多个区间时，可在正确伙伴边界进一步合并。

分配首先寻找能够满足取整页数的最低非空阶，移除块头，逐级将右半块放回对应阶链表，保留左半块作为返回块。无可用块时返回 NULL，不改变链表和计数。取整请求存在内部碎片，例如 17 页占用 32 页；Buddy 的价值是规则化拆分和合并，而不是保证每次请求浪费最小。

释放根据原始 n 计算阶数，检查分配头及引用计数，清除分配标记后重复计算：

```c
buddy_ppn = page2ppn(base) ^ (1UL << order);
```

只有伙伴位于当前登记边界、是同阶空闲块头时才移除并合并，继续升阶。比如对齐测试区的偏移 1、2 虽相邻，但 1 的伙伴是 0、2 的伙伴是 3，不能合成 2 页块。

空闲页数在登记、分配、释放时按真实页数改变；拆分和合并只改变结构，不改变空闲总量。生产分配、释放的阶数搜索/合并最多 O(MAX_ORDER)，链表摘取 O(1)；初始化清理描述符 O(n)。启动测试的全结构检查另计，不代表正常分配路径复杂度。

## 5. 测试方法与实际结果

测试位于同一模块，避免暴露私有链表接口。先验证真实初始化状态，再从主分配器借出 64 页作为隔离测试区。测试中仍使用真实 Page 和物理页号，通过公共 `alloc_pages/free_pages` 接口操作独立测试状态。最终重建原借出块头，把整块归还主状态，并验证主状态的完整结构和计数。

| 场景 | 实际判据 |
|---|---|
| 64 页拆为单页 | 拆分后 0..5 阶各一个余块，释放后恢复一个 6 阶块 |
| 单页及多种取整请求 | 1/2/3/4/5/8/9/16/17/32/33/64 页，检查对齐、占用和回收 |
| 相邻但不是伙伴 | 偏移 1、2 的两个单页保持分开，2 页请求失败 |
| 耗尽和碎片 | 64 次单页分配不重叠，耗尽返回 NULL；仅偶数页空闲时 2 页失败 |
| 多级合并 | 再释放全部奇数页，逐层恢复 64 页 |
| 不规则初始化 | 偏移 3 起的 37 页及另外两段 8 页，逐页核对完整 53 页集合 |
| 混合负载 | 固定种子 1024 次申请/释放，独立占用表验证重叠、计数及失败条件 |
| 主状态恢复 | 测试前后均为 31928 空闲页，全结构检查通过 |

每阶段核对空闲链表链接、物理对齐、块大小、描述符状态、可合并伙伴不残留及总计数。对齐的二进制块若重叠必存在包含关系，内部页无块头标记的检查可检出这种重叠；混合负载另有逐页占用表。测试输出是断言完成后的结果，不是无条件打印。

本次 Buddy 构建：内核末端 `0xffffffffc0207298`，描述符起点 VA `0xffffffffc0208000` / PA `0x80208000`，共 32768 个描述符、1310720 字节，空闲区 `[0x80348000,0x88000000)`，31928 页。见日志中的准确源码摘要；这些值只对应本次构建。

```text
[Buddy] split six levels and merge back: PASS
[Buddy] powers of two and rounded requests (3 -> 4): PASS
[Buddy] non-buddy neighbours, fragmentation, exhaustion: PASS
[Buddy] unaligned 37-page init, multiple ranges, no lost pages: PASS
[Buddy] 1024 mixed operations with occupancy oracle: PASS
[Buddy] state restored: 31928 -> 31928 free pages: PASS
check_alloc_page() succeeded!
```

## 6. 实际问题、AI 交互与修复

首次运行在第一组隔离测试时失败。GDB 捕获 `verify_state()` 的 total=127，而测试状态 nr_free=63。原因是伙伴描述符取自真实页数组，隔离状态在登记 64 页时读到了其外部主状态的同阶空闲伙伴。增加每个状态的登记页号边界后修复，完整重跑成功。保留 [失败运行](../logs/c-buddy-first-failure.log) 和 [GDB 栈与状态](../logs/c-buddy-debug.log)，不能把这些旧日志当成最终通过记录。

普通教师评分在本地出现 `no $qemu_out`。本机 OpenSBI 使用半主机控制台，固件及内核的输出落到宿主 stdout/stderr，而评分只读取 `-serial file:`。新增 `tools/qemu-grade-console.py` 将评分请求的串口改接 stdio，并将 stdout/stderr 合并到原请求文件。它不改内核或评分断言、不生成 PASS 字样、不改 OSopt 包装器。保留 [原始失败](../logs/c-grade-console-failure.log)。共同环境原来的 UART 配置可继续使用普通 `make grade`。

实现和测试分别采用四段式规格，保存 [实现提示词记录](../prompts/c-buddy.md) 和 [测试提示词记录](../prompts/c-buddy-tests.md)。其中明确区分用户实际指令、助手整理的设计规格及后续调试，未编造独立模型会话。AI 承担本轮编码、测试生成、运行分析与文档整理；学生仍需理解并能解释契约、XOR 伙伴和状态恢复过程。

## 7. 回归、证据和边界

默认 Best-Fit 的 B 行为测试全部通过；本机教师评分通过 Page Init、Best-Fit 两项，总计 **20/20**。这是默认模式评分，Buddy 正确性由上述独立测试证明，教师脚本不评分 Buddy。

- [Buddy 完整构建](../logs/c-buddy-build.log)、[Buddy 运行](../logs/c-buddy-run.log)
- [默认构建](../logs/c-best-fit-build.log)、[Best-Fit 回归](../logs/c-best-fit-run.log)
- [教师评分](../logs/c-grade.log)、[环境与源码差异检查](../logs/c-final-audit.log)
- [Buddy 日志摘录图](../images/c-buddy-log.png)、[评分日志摘录图](../images/c-grade-log.png)：由真实日志排版，属于日志可视化，不冒充原始终端截屏。

后续联合验证已完成，证据按各自环境分开保存：

| 验证方 | 被测版本与环境 | 结果及证据 |
|---|---|---|
| A | `b5c05e9`，WSL Ubuntu 20.04 / QEMU 4.1.1 / OpenSBI 0.4 | Buddy 恢复 31928 页，Best-Fit 行为通过，普通评分 20/20；[版本](../logs/a-after-c-revision.log)、[Buddy](../logs/a-after-c-buddy-run.log)、[Best-Fit](../logs/a-after-c-best-fit-run.log)、[评分](../logs/a-after-c-grade.log) |
| B | `6baf620`，WSL Ubuntu 24.04.5 / QEMU 4.1.1 / OpenSBI 0.4 | 复核 Buddy 契约，独立完成两种模式切换与普通评分 20/20；[环境与摘要](../logs/b-integration-environment.log)、[Buddy](../logs/b-integration-buddy-run.log)、[Best-Fit](../logs/b-integration-best-fit-run.log)、[评分](../logs/b-integration-grade.log) |

A 也已补足分页切换前后对照和后续取指地址查询直接命中已有 TLB 项的观察，见 [分页成功补录](../logs/a-paging-transition-confirm.log)、[直接命中日志](../logs/a-qemu-tlb-hit-after-c.log) 和 [A 章节](a-memory-debug.md) 第 17～18 节。联合版根页表 PA 为 `0x80206000`，历史 First-Fit 为 `0x80205000`，不能混用。直接命中日志证明查询跳过 `tlb_fill()`，不能据此断言“CPU 第二次执行 kern_init”。

未测试多核竞争、热插拔、NUMA、超过本框架 128 MiB 管理上限、预期 panic 的非法释放用例，也未实现 SLUB。这些限制继续保留，不因教师评分通过而删除。

## 8. 答辩要点与队友交接

说明 npage 是物理页号上界、nbase 是 DRAM 起始物理页号，描述符数量为两者之差；伙伴计算使用物理页号而不是空闲区偏移。解释为何两块相邻不一定可合并，以及为何总空闲页足够仍可能分配失败。对比 Best-Fit 的外部碎片与 Buddy 向上取整的内部碎片。物理块分配不会自动建立页表映射，本实验不等于实现进程虚拟内存。

Buddy 提交的公共改动仅是 `pmm.c` 条件 include 与管理器选择；A 的 page_init()、B 的源码、教师检查和 Makefile 在该接入中保持不变。启用命令及完整清单见 [C 交接记录](../notes/c-progress.md)。Buddy 成果已在远程；本次报告收尾由用户授权分子任务进行本地 commit，最终 push 由用户操作。A 的学号标记和初始化实际 AI 交互记录仍需本人确认，全组三人通读不由本次自动核验代替。
