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

正常自检和非法释放分别启动；后者共在两种页管理器下运行六次，以预期断言作为判据。扩展版本的两种正常运行均恢复 `31925 → 31925` 页。见 [Best-Fit 下的 SLUB](../logs/extensions-runs/dtb-memory-matrix/slub-best-fit-run.log)、[Buddy 下的 SLUB](../logs/extensions-runs/dtb-memory-matrix/slub-buddy-run.log) 和 [独立重复释放检查](../logs/extensions-runs/slub-validation/slub-best-fit-negative-1-run.log)。

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
