# 扩展：动态物理内存范围与 FDT 边界验证

负责人：尚文轩，2412456。基于教师已有 DTB 支持和 A 的 page_init() 继续完善，不将原框架描述为本组从零实现。

## 实现与契约

教师原解析器已能读取典型 QEMU 设备树，A 已把动态结果接入 page_init()。本扩展将有界、可独立执行的解析抽到 `kern/driver/fdt_memory.c`，在 dtb.h 增加 `dtb_parse_memory(blob,length,&base,&size)`，保留 dtb_init/get_memory_base/get_memory_size 调用方式。

解析时核对 header、结构块与字符串块的边界及非重叠、token 深度、根节点、属性名终止和范围、属性长度、结束标记。所有多字节读取按字节拼装，不依赖 uint64_t 对齐；没有无界 strlen 或无界 token 遍历。只接受根节点直属 memory/memory@ 节点，子节点 reg 不被误识别。

| 情形 | 结果 |
|---|---|
| 单个连续 bank，1/2 个 address/size cell，非零无溢出 | 返回0，写入范围 |
| 截断、偏移越界、坏 magic、结构错误、零长度或地址溢出 | DTB_INVALID=-1，输出参数不变 |
| 没有可识别 memory reg | DTB_NO_MEMORY=-2，输出参数不变 |
| 超过2个 cell 或多个 bank | DTB_UNSUPPORTED=-3，输出参数不变，不能静默丢掉其余 bank |

本接口不是 libfdt 的完整替代，不解释所有设备节点、保留区和热插拔。框架仍只管理一段从 DRAM_BASE 开始的连续内存。runtime 信任 OpenSBI 传入的物理指针指向实际 RAM，先检查它落在启动根页表的 1 GiB DRAM 映射窗口，并限制可解析大小；这不等于能安全读取任意恶意物理地址。失败时全局内存范围为零，已有 page_init() 会拒绝初始化。

## 测试与结果

按 [实施前四段式规格](../prompts/dtb-memory.md) 完成。`tools/test-dtb-parser.py` 在宿主编译同一份实际 C 解析器，以 Python 独立构造 FDT。覆盖4种cell组合、4种内存容量、每个截断点、错误偏移/字符串/属性、nested memory误判、零范围、溢出、多个bank、2000次固定种子变异；失败验证调用方输出不变。共 **2230 项检查通过**，见 [解析测试日志](../logs/dtb-parser-tests.log)。

真实 QEMU 启动矩阵均启用 SLUB，并分别运行 Best-Fit/Buddy 自检：

| RAM | DTB 半开区间 | page_init 管理末端 | 两种模式 |
|---|---|---|---|
| 64 MiB | [0x80000000,0x84000000) | 0x84000000 | 通过 |
| 96 MiB | [0x80000000,0x86000000) | 0x86000000 | 通过 |
| 128 MiB | [0x80000000,0x88000000) | 0x88000000 | 通过 |
| 256 MiB | [0x80000000,0x90000000) | 0x88000000，受框架上限裁剪 | 通过 |

128 MiB 日志为 `slub-best-fit-run.log` / `slub-buddy-run.log`，其余为同目录 `*-memory-64/96/256-run.log`：[Best-Fit 默认128](../logs/extensions-runs/dtb-memory-matrix/slub-best-fit-run.log)、[Buddy 默认128](../logs/extensions-runs/dtb-memory-matrix/slub-buddy-run.log)、[Best-Fit 256](../logs/extensions-runs/dtb-memory-matrix/slub-best-fit-memory-256-run.log)、[Buddy 64](../logs/extensions-runs/dtb-memory-matrix/slub-buddy-memory-64-run.log)。每次记录当前源码 SHA256 与实际输出，不套用旧版地址。

默认模式未修改教师测试，[教师评分仍为20/20](../logs/extensions-runs/dtb-memory-matrix/default-grade.log)，评分后恢复默认 Best-Fit 构建。大于128 MiB的额外内存只报告不分配，不能据此声称突破管理上限。

复现：

```bash
source ~/OSopt/env.sh
python3 tools/test-dtb-parser.py
python3 tools/check-extensions.py --memory-matrix
```

实际遇到的环境问题：OSopt 刻意提供裸名 RISC-V as/ld，宿主 gcc 会从 PATH 误选它们并报 `as: unrecognized option --64`。宿主专用测试使用 `/usr/bin/gcc -B/usr/bin/` 明确选择宿主 binutils；未删除或修改工具链软链接。
