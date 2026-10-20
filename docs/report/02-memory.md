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

联合版本满足 `(0x88000000 - 0x80348000) / 4096 = 31928`。代码增加导致内核占用变化，表中各阶段分别对应独立构建。详细地址及摘要见 [A 初始化检查](../logs/a-page-init-debug.log)、[B 联合运行](../logs/b-integration-best-fit-run.log) 和 [扩展运行](../logs/extensions-runs/dtb-memory-matrix/slub-best-fit-run.log)。

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

![初始版本物理内存布局](../images/a-page-init-output.png)

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

256 MiB 配置验证了探测结果与管理上限的区分，并未扩展分配上限。见 [解析检查](../logs/dtb-parser-tests.log)、[64 MiB Buddy](../logs/extensions-runs/dtb-memory-matrix/slub-buddy-memory-64-run.log) 和 [256 MiB Best-Fit](../logs/extensions-runs/dtb-memory-matrix/slub-best-fit-memory-256-run.log)。
