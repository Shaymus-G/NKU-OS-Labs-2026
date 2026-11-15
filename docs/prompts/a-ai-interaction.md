# Lab4 A 部分：AI 提示词与对应回答

- 负责人：国峻赫（A，2412082）
- 任务范围：物理内存初始化、Best-Fit 接入、Sv39 基本页映射维护，以及这些模块的独立验证与联调复核。
- 版本：A 的实现提交为 `f66f380`；联合验证使用 `36412b6b221b2677602bbbb9ac29dd13a92ce7d9`。

## 1. 物理页初始化与 Best-Fit：先明确范围和不变量

### 提示词

> 我负责 Lab4 的内存部分。我把问题拆成两层：`page_init()` 决定哪些物理页能交给分配器，Best-Fit 决定从这些页中怎么选块。我准备保留 `default_pmm_manager` 作为注册符号，但实际使用 Best-Fit，因为教师保留的 `best_fit_check()` 要检查最小可用块，而不是只检查“能分到页”。
>
> 我认为 `npage=PPN(managed_end)` 是绝对物理页号上界，描述符数量应是 `npage-nbase`。请用实际 DRAM 区间检查我的推导，解释为什么不能直接移植 Lab2 的固定计数，并给出初始化、分配、拆分、释放及合并的实现步骤。请明确哪些字段只属于空闲块头，哪些计数不随合并改变。保留现有接口、自检和启动顺序。

### AI 对应回答

你的拆分正确。先将设备树给出的 DRAM 范围变成可信的空闲页区间，再在该区间上实现 Best-Fit；两层的边界条件应分别验证。

**物理页初始化。** 在 `kern/mm/pmm.c` 中读取 DTB 的基址和容量，检查基址符合本框架的 `DRAM_BASE`、容量非零且相加未溢出。设置 `va_pa_offset = PHYSICAL_MEMORY_OFFSET` 后，才能用当前直接映射约定计算 `PADDR(KERNTOP)` 和 `PADDR(end)` 一类地址。令：

```text
reported_end = DTB 内存基址 + 容量
managed_end  = 向下按页对齐(min(reported_end, PADDR(KERNTOP)))
npage        = PPN(managed_end)
count        = npage - nbase
pages_va     = 向上按页对齐(end)
pages_pa     = PADDR(pages_va)
free_begin   = 向上按页对齐(pages_pa + count × sizeof(struct Page))
```

在写描述符数组前，检查 `pages_pa < managed_end`，并用 `count <= (managed_end - pages_pa) / sizeof(struct Page)` 检查容量，避免直接计算乘积再判断时发生溢出。若对齐后的 `free_begin >= managed_end`，当前布局没有完整空闲页，应明确报错。

遍历所有描述符，把 `flags`、`property`、引用计数和 `pra_vaddr` 初始化，初始化 `page_link`、`pra_page_link`，并标记为保留。最后只用 `init_memmap(pa2page(free_begin), (managed_end - free_begin) / PGSIZE)` 登记可分配的区间。这样启动保留区、内核映像和描述符数组本身都不会进入空闲链表。

以本组的 `[0x80000000, 0x88000000)` 为例：`nbase=0x80000`，`npage=0x88000`，真正的描述符数量是 `0x8000=32768`。这次 Lab4 的 `sizeof(struct Page)=64`，数组占 2 MiB；结构体随实验阶段变化，旧版的字节数和页数都不应写死。独立构建的空闲起点若是 `0x80409000`，则空闲页数为 `(0x88000000-0x80409000)/4096=31735`。

**Best-Fit。** 在 `kern/mm/default_pmm.c` 中保留 `default_pmm_manager` 对外符号，实例名称用 `best_fit_pmm_manager`，头文件对齐 `default_pmm.h`。不修改 `basic_check()` 和 `best_fit_check()`。分配器维护两个不变量：

1. 链表按地址递增，只包含空闲块的头页。头页的 `property` 是连续页数，`PageProperty` 表明它是有效块头。
2. `nr_free` 是所有空闲页的总数。拆分和合并只重新组织块；申请成功减去申请页数，释放增加归还页数。

实现顺序如下：

| 操作 | 应执行的步骤 |
| --- | --- |
| 初始化 | 建立空链表，空闲计数设为零。 |
| 登记空闲区 | 清除传入区间的保留状态和旧引用；给首项设置 `property=n`，按地址插入链表，计数加 `n`。 |
| 申请 `n` 页 | 扫描所有足够的块，保留最小者；遇到恰好 `n` 页的块可提前停止。总页数够但没有足够长的连续块时，也应返回空，不能拿不连续的页拼接。 |
| 拆分 | 从链表移除原块头；若有余量，将 `best+n` 作为余块头放回原位置，长度设为原长度减 `n`。清除已分配原块头的空闲标志和长度，计数只减 `n`。 |
| 释放并合并 | 重置归还页的描述符，按地址插入新块，计数加 `n`。若左块尾与新块头相接，保留左块头、移除新块头，再用合并后的块检查右侧。相接才合并；不相接则保留两块。 |

例如有 10 页、6 页两个候选，申请 4 页应选 6 页块，留下 2 页。归还那 4 页后可与相邻的 2 页块合并，空闲页只增加 4。如果 10 页块在链表中的位置更靠前，这个例子就能区分 Best-Fit 与“选第一个够用块”的 First-Fit。完整函数见附录。

### A 的设计判断

先检查内核映像和描述符有没有被当作空闲页，再检查分配选择。仅凭 `alloc_page()` 能成功，既证明不了空闲范围正确，也区分不了 Best-Fit 和 First-Fit。保留框架对管理器的命名接口可以避免牵连其他模块；运行时名称则应忠实反映策略。

## 2. Sv39 页映射：用引用计数和失败状态约束实现

### 提示词

> 对 `page_insert()`，我先列三种情况：原 VA 没有映射、映射同一个物理页、映射另一个物理页。同页重插只能更新权限，不能让引用计数增长；换页时，如果旧引用降到零，我认为应该先更新 PTE 并同步翻译，再释放旧页。删除未映射地址也不应创建页表。
>
> 请按我给出的不变量复核，并给出可落到 `pmm.c` 的完整核心实现：说明 `get_pte(create)` 的两级中间表、第二次分配失败如何回滚、大页如何识别，以及 `perm=0` 为什么不能直接写成只有 V 的叶项。保留框架的教师断言，并单独指出课程特例与通用 Sv39 规范的差别。

### AI 对应回答

两套对象的引用不能混在一起：**页表页**是路径上的结构资源，**被映射的数据页**的引用计数对应有效叶映射。对同一数据页建立两个 VA 别名会得到两个引用；同一 VA 对同一数据页更新权限仍只有一个引用。

`get_pte()` 走 `PDX1(va) → PDX0(va) → PTX(va)`。有效上层项若含 `R/W/X`，对于合法编码这是高层叶项；本接口不拆 1 GiB 或 2 MiB 的大页，不应把它当表地址解引用。有效非叶项的物理页号通过 `PDE_ADDR()` 转成物理基址，再由 `KADDR()` 得到软件可访问的地址。`create=false` 的缺失路径直接返回空。即使成功返回末级 PTE 指针，`*ptep` 仍可能无效；读取者要检查 V。

两层都缺失时，第一张新表先清零但不连接到根。第二次申请失败，只释放还未发布的第一张表；成功后先写子项，再写根项，并作本地全范围翻译同步。下面给出对应本组接口的核心代码；框架已有宏、类型和函数声明继续使用。

```c
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    const pte_t leaf_bits = PTE_R | PTE_W | PTE_X;
    pde_t *pdep1 = &pgdir[PDX1(la)];
    struct Page *new_l1 = NULL;
    pde_t *l1;
    if (*pdep1 & PTE_V) {
        if (*pdep1 & leaf_bits) {
            return NULL;  /* A 1 GiB leaf is not a page-table pointer. */
        }
        l1 = KADDR(PDE_ADDR(*pdep1));
    } else {
        if (!create || (new_l1 = alloc_page()) == NULL) {
            return NULL;
        }
        l1 = page2kva(new_l1);
        memset(l1, 0, PGSIZE);
    }

    pde_t *pdep0 = &l1[PDX0(la)];
    if (*pdep0 & PTE_V) {
        if (*pdep0 & leaf_bits) {
            return NULL;  /* A 2 MiB leaf is not a page-table pointer. */
        }
    } else {
        struct Page *new_l0;
        if (!create || (new_l0 = alloc_page()) == NULL) {
            if (new_l1 != NULL) {
                free_page(new_l1);
            }
            return NULL;
        }
        set_page_ref(new_l0, 1);
        memset(page2kva(new_l0), 0, PGSIZE);
        /* Preserve the starter's non-leaf flags expected by check_pgdir(). */
        *pdep0 = pte_create(page2ppn(new_l0), PTE_U);
        if (new_l1 != NULL) {
            set_page_ref(new_l1, 1);
            *pdep1 = pte_create(page2ppn(new_l1), PTE_U);
        }
        /* Publishing a non-leaf entry requires the broader fence. */
        asm volatile("sfence.vma" ::: "memory");
    }
    pte_t *l0 = KADDR(PDE_ADDR(*pdep0));
    return &l0[PTX(la)];
}

int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }

    struct Page *old = (*ptep & PTE_V) ? pte2page(*ptep) : NULL;
    /* In this lab, perm=0 denotes a readable leaf, not a non-leaf. */
    if ((perm & PTE_W) || !(perm & (PTE_R | PTE_W | PTE_X))) {
        perm |= PTE_R;
    }

    if (old != page) {
        page_ref_inc(page);
    }
    *ptep = pte_create(page2ppn(page), perm);
    tlb_invalidate(pgdir, la);

    if (old != NULL && old != page && page_ref_dec(old) == 0) {
        free_page(old);
    }
    return 0;
}

void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep == NULL || !(*ptep & PTE_V)) {
        return;
    }

    struct Page *page = pte2page(*ptep);
    *ptep = 0;
    tlb_invalidate(pgdir, la);
    if (page_ref_dec(page) == 0) {
        free_page(page);
    }
}

// Synchronize a leaf PTE update for this VA on the local hart.
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    (void)pgdir;
    asm volatile("sfence.vma %0, zero" : : "r"(la) : "memory");
}
```

代码中的权限处理有两个层次：

- 课程接口将 `perm=0` 解释为可读的基本页映射。`pte_create()` 设置 V；若叶项只有 V、没有 `R/W/X`，它不会被当成有效的末级数据页。`W=1,R=0` 是保留编码，所以写请求补 R。
- 课程的 `check_pgdir()` 会检查上层 PTE 的 U 位，这份代码保留框架原约定。通用 Sv39 规范要求软件清零非叶 PTE 的 U 保留位。课程 QEMU 和教师断言通过并不能代替通用架构兼容性证明。

替换和删除时，先使旧翻译不能继续指向待释放物理页，再把引用归零的旧页还给分配器。叶项修改使用按 VA 的 `sfence.vma`，发布非叶项则用范围更广的本地同步。这里处理的是当前 hart 上的翻译同步，不包含多 hart 通知。另外，`get_pte()` 返回空既可能是创建失败，也可能是拒绝高层叶项；本组 `page_insert()` 沿用统一的 `-E_NO_MEM` 返回，不要只看错误名就断定内存耗尽。

### A 的设计判断

用纸面例子先核对实现：`0x0→p1`、`0x1000→p2` 时引用为 `(1,1)`。把 `0x1000` 换到 `p1` 后必须是 `(2,0)`；再次给这个 VA 和同一物理页改权限，计数还应是 `(2,0)`。依次撤销两个 VA 时，`p1` 才会从 2 降为 1、再降为 0。这个例子同时检查引用计数、权限更新和物理页释放时机。

## 3. 独立验证与联调：让日志回答不同的问题

### 提示词

> 进程和调度部分还没接入，正式构建链接时找不到 `proc_init()` 和 `cpu_idle()`。这不能证明 PMM 有错，但光让两个对象文件编译成功也不能证明初始化能跑。我准备在独立工作树中保留原顺序到 `vmm_init()`，之后打印标记并停住；不在共享分支加入假实现。
>
> 请给出 WSL/QEMU 下可实际复现的步骤和验收判据。联调后，我想在 `check_pgdir()` 的一次映射替换前后各停一次，直接核对 PPN 和引用计数。请给出具体 GDB 检查方法，解释为什么要先保存原 `p2` 的 PPN，并区分独立自检、手动观察、未执行的失败注入和整组评分。

### AI 对应回答

分成三个层次验证。对象文件生成只说明各模块能编译；独立运行说明真实初始化链可以走通；联合运行、评分和 GDB 才能检查集成行为与内部状态。

**第一层：隔离运行。** 在 WSL 的正式仓库创建基于 `f66f380` 的独立工作树；若目标目录已有工作树，先核对提交和改动，不要复用错误版本。

```bash
cd "$HOME/NKU-OS-Labs-2026"
git worktree add --detach "$HOME/lab4-a-check" f66f380
cd "$HOME/lab4-a-check"
```

只在该目录的 `kern/init/init.c` 中，保留 `vmm_init();` 和之前的语句，将它之后的 `proc_init()`、时钟初始化、开中断及 `cpu_idle()` 替换为：

```c
cprintf("A-only check: PMM and VMM initialization returned\n");
while (1) {
}
```

在工具链已进入 `PATH` 后构建并运行，同时把证据存到正式仓库已建立的日志目录：

```bash
mkdir -p "$HOME/NKU-OS-Labs-2026/docs/logs"
set -o pipefail
make 2>&1 | tee "$HOME/NKU-OS-Labs-2026/docs/logs/a-isolated-build.log"
make qemu QEMU="$HOME/OSopt/qemu/bin/qemu-system-riscv64" 2>&1 |
    tee "$HOME/NKU-OS-Labs-2026/docs/logs/a-isolated-qemu.log"
```

先确认构建成功，再运行 QEMU。看到 `check_alloc_page`、`check_pgdir`、`check_boot_pgdir`、`kmalloc_init`、`check_vma_struct`、`check_vmm` 的成功消息与最后的完成标记，才能说启动走过了真实 `vmm_init()`。随后手动退出 QEMU。回到正式仓库，用 `git diff HEAD -- kern/init/init.c` 检查临时改动没有带回。当前不能把这组结果写成整组评分。

**第二层：联合运行。** B、C 的功能合入后，在正式仓库记录 `git rev-parse HEAD`，运行 `make clean`、`make`、`make qemu`。检查内存自检与 `initproc` 输出；看到线程函数返回后进入 `do_exit()` 的占位路径，应按源码解释，不记为自然关机或资源全部回收。退出 QEMU 后直接运行本机的 `make grade`，保存三个评分项，随后用 `make` 恢复普通镜像，再做 GDB 调试。

**第三层：在同一次 GDB 会话前后取值。** 一个终端启动 `make debug`，另一个运行该仓库的 `make gdb`，它会自动连接本地的 1234 端口。以 `36412b6` 对应的构建为例，两个断点分别是替换语句前的第 383 行、第一次删除前的第 390 行；其他版本应先查看 `check_pgdir()` 选择对应语句。以下命令在 GDB 中执行：

```gdb
thbreak kern/mm/pmm.c:383
continue
set $l1 = (pde_t *)((((unsigned long long)boot_pgdir_va[0] >> 10) << 12) + va_pa_offset)
set $l0 = (pte_t *)((((unsigned long long)$l1[0] >> 10) << 12) + va_pa_offset)
p/x $l0[0]
p/x $l0[1]
set $p1_ppn = ((unsigned long long)$l0[0] >> 10) & ((1ULL << 44) - 1)
set $old_ppn = ((unsigned long long)$l0[1] >> 10) & ((1ULL << 44) - 1)
p pages[$p1_ppn - nbase].ref
p pages[$old_ppn - nbase].ref
thbreak kern/mm/pmm.c:390
continue
p/x $l0[0]
p/x $l0[1]
set $new_ppn = ((unsigned long long)$l0[1] >> 10) & ((1ULL << 44) - 1)
p $new_ppn == $p1_ppn
p pages[$p1_ppn - nbase].ref
p pages[$old_ppn - nbase].ref
```

两个地址的前两级索引都是 0，末级索引分别为 0 和 1。上述命令用本实验这两条高层 PTE 的 PPN 和直接映射找到末级表；对于可能包含高位扩展字段的通用 PTE，取非叶 PPN 时也应显式掩码。替换前先保存 `$old_ppn`，否则替换后从 `$l0[1]` 取到的是新页 `p1`，会把同一页读两遍。对优化后的 ELF，若 `p1/p2` 局部变量不可用，从 PTE 和全局 `pages` 读数仍能完成核对。

| 验证项 | 替换前预期 | 替换后预期 |
| --- | --- | --- |
| `0x0` 指向 | `p1` | `p1` |
| `0x1000` 指向 | `p2` | `p1` |
| `0x1000` 的叶权限 | `V/R/W/U` | `V/R` |
| `p1.ref` / 原 `p2.ref` | 1 / 1 | 2 / 0 |

如果需要**另测**第二张页表申请失败，应在临时副本中挑选根项未建立的测试 VA，构造只有一次中间表申请能成功的情形，再断言根项未发布、数据页引用不变、空闲页数恢复。这个测试与上面的 GDB 替换不同；没有运行前只能写成建议。

### A 的实测与证据

- **独立运行（`f66f380`）：** 上述框架自检和 `vmm_init()` 返回标记均出现，初始空闲区为 `[0x80409000, 0x88000000)`，共 31735 页。原始记录：[独立 QEMU 日志](../logs/a-isolated-qemu.log)。
- **联合运行（`36412b6`）：** 内存自检与 `initproc` 输出正常，初始空闲页为 31730；教师评分 PMM、Process Create、Schedule 三项均通过，**30/30**。联合日志目录：`docs/logs/a-integrated-20261115-124341/`。
- **GDB 实测：** `0x1000` 的 PTE 从 `0x20104417` 变为 `0x20103803`，PPN 从 `0x80411` 变为 `0x8040e`，与 `0x0` 的 PPN 一致。引用计数从 `(1,1)` 变为 `(2,0)`，见联合日志目录的 `a-pte-ref-gdb.log`。两个停点未单独测量空闲页恢复，仍由完整的 `check_pgdir()` 自检覆盖。没有另行做第二张页表申请失败注入。

## 4. 思考题：是否该拆开查找和分配？

### 提示词

> 我观察到 Sv39 的 `get_pte()` 在两层中间页表上做了相似的工作。我理解 Sv48 会多一层，但如果把整个函数直接复制一段，失败回滚仍容易漏掉已经申请的页。也不能为了减少重复就让所有调用默认创建页表，因为 `get_page()` 和删除未映射地址都不应改变内存状态。请比较 Sv32、Sv39、Sv48，并评价“保留当前接口、抽取 walker”和“对外拆分查询与创建”各适用于什么场合。

### AI 对应回答

| 模式 | 层数 | 各级 VPN 索引 | 页表项大小 | 每张表容量 |
| --- | ---: | --- | ---: | --- |
| Sv32 | 2 | 各 10 位 | 4 字节 | 1024 项 × 4 字节 |
| Sv39 | 3 | 各 9 位 | 8 字节 | 512 项 × 8 字节 |
| Sv48 | 4 | 各 9 位 | 8 字节 | 512 项 × 8 字节 |

三种模式的基本页偏移都是 12 位。每层的共同步骤是按 VPN 取项、判断有效性、区分叶项和下级指针。Sv39 的两段中间层代码重复的是这套动作，不是同一个索引。大页在高层即可命中叶项，所以硬件翻译不总要走到末级；本实验的软件 API 只维护 4 KiB 页映射，应有意识地拒绝高层叶项，不能盲目继续解引用。

对本实验，保留 `get_pte(pgdir, va, create)` 的好处是不用改动已有调用方；需在实现和调用处维护 `create=0` 无分配、`create=1` 失败不留半条路径的契约。若扩展到多种分页模式，宜让共享 walker 封装层次、位宽及终止条件，并将对外查询和创建分开。纯查询返回缺失或叶项信息；创建函数另行记录已申请的表页，成功时发布、失败时回滚。这样既共享遍历，又让调用者能看出哪些路径允许分配，甚至可进一步区分“不支持的大页”和“内存不足”。

与 VMA 相比，页表是硬件翻译和权限检查的数据，VMA 是内核记录的虚拟地址区间。本实验按需创建的是**页表结构**，并不意味着实现了由缺页异常触发的数据页按需分配、置换或用户地址空间复制。

补充一处与内存模块直接相关的同步：框架在 `alloc_pages()`、`free_pages()` 和 `nr_free_pages()` 外保存、关闭并恢复本地中断，以免单 hart 上的中断路径打断空闲链表或计数操作。恢复时只在进入时 SIE 为 1 的情况下重新开启，因此调用者已关中断时不会被内层误开。这不能替代多核锁。

### A 的取舍

当前框架只需要稳定的 Sv39 基本页映射，我保留了原接口，优先把“只读不分配、失败不发布、不会误走大页”做清楚。若维护范围扩大，我会先拆出通用遍历及错误分类，再考虑改变调用接口。

## 附录：第 1 组回答对应的实现代码

以下摘自本组实现，用来对照第 1 组回答。它是现有 `pmm.c` 和 `default_pmm.c` 的函数片段；保留原头文件、教师自检和注册入口，不作为独立编译文件。

**`kern/mm/pmm.c`：建立描述符和空闲区。**

```c
static void
page_init(void)
{
    extern char end[];
    const uint64_t mem_base = get_memory_base();
    const uint64_t mem_size = get_memory_size();
    const uint64_t reported_end = mem_base + mem_size;
    if (mem_base != DRAM_BASE || mem_size == 0 || reported_end < mem_base) {
        panic("page_init: invalid DTB memory range");
    }

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    const uintptr_t limit = PADDR(KERNTOP);
    uintptr_t managed_end = reported_end < limit ? reported_end : limit;
    managed_end = ROUNDDOWN(managed_end, PGSIZE);
    if (managed_end <= PADDR(KERNBASE)) {
        panic("page_init: insufficient memory for kernel");
    }
    npage = PPN(managed_end);

    const uintptr_t pages_va = ROUNDUP((uintptr_t)end, PGSIZE);
    const uintptr_t pages_pa = PADDR(pages_va);
    const size_t count = npage - nbase;
    if (pages_pa >= managed_end ||
        count > (managed_end - pages_pa) / sizeof(struct Page)) {
        panic("page_init: insufficient memory for page descriptors");
    }

    const uintptr_t free_begin =
        ROUNDUP(pages_pa + count * sizeof(struct Page), PGSIZE);
    if (free_begin >= managed_end) {
        panic("page_init: no usable physical pages");
    }

    pages = (struct Page *)pages_va;
    for (size_t i = 0; i < count; i++) {
        struct Page *p = &pages[i];
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
        list_init(&p->pra_page_link);
        p->pra_vaddr = 0;
        SetPageReserved(p);
    }
    const size_t free_count = (managed_end - free_begin) / PGSIZE;
    init_memmap(pa2page(free_begin), free_count);
    cprintf("PMM: DRAM [0x%lx, 0x%lx), managed end 0x%lx\n",
            (unsigned long)mem_base, (unsigned long)reported_end,
            (unsigned long)managed_end);
    cprintf("PMM: %lu descriptors, sizeof(Page)=%lu, free=[0x%lx, 0x%lx), %lu pages\n",
            (unsigned long)count, (unsigned long)sizeof(struct Page),
            (unsigned long)free_begin, (unsigned long)managed_end,
            (unsigned long)free_count);
}
```

**`kern/mm/default_pmm.c`：分配器核心及注册结构。** 下列前半段位于教师自检函数之前，注册结构仍放在 `best_fit_check()` 定义之后。

```c
static free_area_t free_area;
#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

static void
best_fit_init(void)
{
    list_init(&free_list);
    nr_free = 0;
}

static void
best_fit_init_memmap(struct Page *base, size_t n)
{
    assert(n > 0);
    for (struct Page *p = base; p < base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;

    list_entry_t *le = list_next(&free_list);
    while (le != &free_list && le2page(le, page_link) < base) {
        le = list_next(le);
    }
    list_add_before(le, &base->page_link);
}

static struct Page *
best_fit_alloc_pages(size_t n)
{
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }

    struct Page *best = NULL;
    for (list_entry_t *le = list_next(&free_list);
         le != &free_list; le = list_next(le)) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n && (best == NULL || p->property < best->property)) {
            best = p;
            if (p->property == n) {
                break;
            }
        }
    }
    if (best == NULL) {
        return NULL;
    }

    size_t length = best->property;
    list_entry_t *prev = list_prev(&best->page_link);
    list_del_init(&best->page_link);
    if (length > n) {
        struct Page *rest = best + n;
        rest->property = length - n;
        SetPageProperty(rest);
        list_add_after(prev, &rest->page_link);
    }
    best->property = 0;
    ClearPageProperty(best);
    nr_free -= n;
    return best;
}

static void
best_fit_free_pages(struct Page *base, size_t n)
{
    assert(n > 0);
    for (struct Page *p = base; p < base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;

    list_entry_t *le = list_next(&free_list);
    while (le != &free_list && le2page(le, page_link) < base) {
        le = list_next(le);
    }
    list_add_before(le, &base->page_link);

    list_entry_t *prev = list_prev(&base->page_link);
    if (prev != &free_list) {
        struct Page *left = le2page(prev, page_link);
        if (left + left->property == base) {
            left->property += base->property;
            base->property = 0;
            ClearPageProperty(base);
            list_del_init(&base->page_link);
            base = left;
        }
    }
    list_entry_t *next = list_next(&base->page_link);
    if (next != &free_list) {
        struct Page *right = le2page(next, page_link);
        if (base + base->property == right) {
            base->property += right->property;
            right->property = 0;
            ClearPageProperty(right);
            list_del_init(&right->page_link);
        }
    }
}

static size_t
best_fit_nr_free_pages(void)
{
    return nr_free;
}
```

```c
const struct pmm_manager default_pmm_manager = {
    .name = "best_fit_pmm_manager",
    .init = best_fit_init,
    .init_memmap = best_fit_init_memmap,
    .alloc_pages = best_fit_alloc_pages,
    .free_pages = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check = best_fit_check,
};
```
