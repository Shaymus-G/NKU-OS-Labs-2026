# 扩展：教学 SLUB 对象分配器

实现、测试及报告：尚文轩，2412456。新增扩展，不覆盖原有 A/B/C 主体任务。

## 接口和设计

`kern/mm/slub.c/.h` 提供命名缓存 create/alloc/free/shrink/destroy，以及 `kmalloc/kfree/ksize`。以既有页分配器为底层，不改变 Page 和 pmm_manager。单核教学 SLUB 风格：每个缓存优先从当前 partial 页取对象，空对象自身保存 next 指针，页头保存缓存归属、占用计数和 256 位分配位图；没有外部每对象描述符数组。partial/full/empty 三种链表支持状态转移与回收，每缓存最多保留一个空页。

这不是 Linux 多核 SLUB 的完整移植：没有每 CPU 批处理、NUMA、锁或远程释放协议，调用者必须串行化。明确这一边界比把一个简化分配器称作生产级实现更准确。

| 接口 | 契约 |
|---|---|
| kmem_cache_create(name,size,align,ctor) | 名称指针需保持有效；size>0，align 为 2 的幂；最小实际对齐 16；每页至少放下一个对象 |
| kmem_cache_alloc/free | 同一缓存的完整对象指针；构造回调每次分配执行；非法指针、重复或错缓存释放触发断言 |
| kmem_cache_destroy | 仍有活对象或属于内置通用缓存时返回 false，不能销毁 |
| kmalloc | 1..2048 字节选择 16/32/64/128/256/512/1024/2048 缓存；更大对象交给连续页分配 |
| kfree | NULL 无操作；大对象记录原始页数，正确兼容 Buddy 向上取整及按原请求释放的契约 |
| shrink | 只归还空页，不移动或破坏存活对象 |

固定 24 个缓存描述符中 8 个用于通用大小类、16 个可用于命名缓存；描述符耗尽返回 NULL，不耗物理页。大对象头位于首个物理页，返回地址按 16 字节对齐；零字节、溢出尺寸、页分配失败返回 NULL。

## 启用与运行

默认内核保持原实验行为；扩展编译宏仅在 `pmm_init()` 后运行扩展检查：

```bash
source ~/OSopt/env.sh
make clean
make DEFS=-DLAB2_EXTENSIONS
make qemu DEFS=-DLAB2_EXTENSIONS
# Buddy 底层需先 clean，再同时传入两个宏：
make clean
make DEFS='-DLAB2_EXTENSIONS -DPMM_BUDDY'
make qemu DEFS='-DLAB2_EXTENSIONS -DPMM_BUDDY'
```

自动验证：`python3 tools/check-extensions.py --negative-tests`。每次写入新时间目录，最后恢复默认构建；正常自检和预期 panic 分开启动。

## 实际验证与测试理由

`kern/mm/slub_test.c` 用断言检查：对齐 64 字节的 37 字节记录缓存、构造回调计数、对象复用、full/partial/empty 状态、活对象阻止销毁；跨两页互不重叠；27 个大小边界与大对象的数据隔离；64 个小对象只占一个物理页；1024 次混合申请/释放与独立字节图案及区间不重叠判据；16 个命名缓存描述符耗尽；真正耗尽全部物理页后小/大对象失败，再回收全部页、重新申请并恢复初始页数。

非法释放通过三次独立启动测试：重复释放、错误缓存释放、内部地址释放。每次必须出现所选 case 标记和断言 panic；预期异常不是正常自检失败，也不允许仅凭超时判成功。

初次版本在 Best-Fit、Buddy 下均通过，见 [首轮 Best-Fit](../logs/extensions-runs/slub-first/slub-best-fit-run.log)、[首轮 Buddy](../logs/extensions-runs/slub-first/slub-buddy-run.log)。这版扩展构建空闲页为 31925，根页表 PA 为 0x80208000；代码增加引起布局变化，不回填到旧版日志。后续增加混合/异常测试的准确布局与结果以各新日志为准。

加入混合与异常测试后的两种正常运行以及六次预期异常均通过：[Best-Fit](../logs/extensions-runs/slub-validation/slub-best-fit-run.log)、[Buddy](../logs/extensions-runs/slub-validation/slub-buddy-run.log)。独立负例日志以同目录 `*-negative-1/2/3-run.log` 保存；每份均有 case 标志和真实断言 panic。这是防御性校验成功的预期结果，不把它当作正常启动成功日志。

## 理论联系

页分配器解决连续物理页资源问题，SLUB 在页内切分重复尺寸的小对象，降低每次分配整页的浪费。固定大小类仍有内部碎片、页头和空对象链成本；保留一个空 slab 可加快复用，也会暂占一页，通过 shrink 主动归还。大对象仍受底层连续块碎片和 Buddy 取整限制，不保证“总空闲足够”就一定成功。
