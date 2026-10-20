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

转换路径生成目标所在的 4 KiB 物理页基址 `0x80200000`，加页内偏移 `0xd8` 得到完整地址 `0x802000d8`。见 [宿主页表遍历日志](../logs/a-qemu-pagewalk-body.log)。

![历史构建的根页表项读取](../images/a-qemu-root-pte.png)

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

执行 `next` 后到第 1039 行，跳过包含 `tlb_fill()` 的未命中分支，说明这次后续取指地址查询直接复用已有条目。该日志不能确定客体执行 `kern_init` 的次数。见 [直接命中日志](../logs/a-qemu-tlb-hit-after-c.log)。

两种证据的区别在于判断发生的时刻：历史会话在未命中后完成填充，再检查标签匹配；联合会话在进入未命中判断之前已有匹配项。两份日志来自不同构建和会话，分别支持相应路径，而不组成同一条目的连续生命周期记录。

![联合版本的软件 TLB 直接命中](../images/a-qemu-tlb-hit-after-c.png)

### 6.3 分页切换前后对照

| 状态 | PC | SP | satp |
|---|---|---|---|
| 联合版写 satp 前 | 0x80200034 | 0x8001bd80 | 0 |
| 联合版高地址入口 | 0xffffffffc02000d8 | 0xffffffffc0206000 | 0x8000000000080206 |

分页前 MODE 为 Bare；分页后为 Sv39，根页表 PA 为 `0x80206000`。栈顶与页表起始 VA 相同，分别表示向低地址增长的栈上界和向高地址存放的页表起点，并不重叠。低地址断点在分页后重插入失败的问题，通过禁用该断点再继续到高地址入口解决。见 [入口成功补录](../logs/a-paging-transition-confirm.log)。

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

![自动演示中的实时页表查询记录](../images/extensions-address-query.png)

见 [自动 GDB 日志](../logs/address-demo/live-confirm/gdb.log)、[结构化记录](../logs/address-demo/live-confirm/trace.json)、[离线回放](../logs/address-demo/live-confirm/replay.html) 及 [解码测试](../logs/address-demo/live-confirm/decoder-tests.log)。
