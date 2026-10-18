# 成员 B：First-Fit 与 Best-Fit 工作记录

首次记录：2026-10-17；更新：2026-10-18。负责人：成员 B（陈铭宇）。本文用于交接 First-Fit 分析、Best-Fit 实现及已验证结果，可由 C 汇入最终报告。

当前工程为 lab2 分支，以 A 的初始化提交 79b5791 为基线。B 的代码、测试、学号及验证证据交接提交为 [f8ed38a8d6fb02e1093995eeb99d0744101eabe1](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/f8ed38a8d6fb02e1093995eeb99d0744101eabe1)，本文及其他交接文档由后续文档提交补齐。79b5791 本身不包含 B 的成果。

## First-Fit 源码分析

教师提供的 default_pmm.c 已实现 First-Fit，本组不把它记作自行实现的算法。

- default_init() 初始化双向循环空闲链表，将 nr_free 清零。
- default_init_memmap() 接收连续保留页，清除保留标志和引用计数；块头 property 记录页数，设置 PageProperty，并按 Page 地址递增插入链表。此函数本身不合并多次注册的相邻区间。
- default_alloc_pages() 从链表起点顺序查找第一个足够大的块。移除块头；大于请求时，将尾部剩余块放回原位置。成功后减去请求页数，清除返回块头的 PageProperty；找不到连续块时返回 NULL，空闲总量足够也不保证分配成功。
- default_free_pages() 清理释放区间的状态，将新块按地址插回；先合并前邻块，再合并后邻块。nr_free 只增加本次释放的页数，合并不增加总空闲量。
- default_nr_free_pages() 返回空闲页数，default_check() 调用教师检查验证分配与回收。

例子：按地址排列的两个互不相邻空闲块，低地址块有 8 页，高地址块有 5 页。请求 4 页时，First-Fit 选 8 页块并剩 4 页；Best-Fit 选 5 页块并剩 1 页。两次比较应分别从相同初始状态开始。

改进空间：First-Fit 的搜索和释放定位最坏需要线性遍历。可采用 next-fit 游标减少重复扫描，但未必改善碎片；也可增加大小索引或分级链表加速候选查找，代价是额外元数据和拆分、合并时维护多个索引。改用伙伴系统可限制块规格并简化伙伴合并，但会带来内部碎片和不同的请求契约。这些属于分析建议，当前未实现。

## Best-Fit 实现规格

接口保持教师 pmm_manager 不变；输入区间由 A 的 page_init() 注册。只接受合法页区间，n 必须大于 0，零页请求沿用教师 assert 约定。

内部采用独立 static free_area_t。链表按地址递增；PageProperty 标记空闲块头，property 表示块页数，nr_free 表示总空闲页数。块内部页和已分配页不带 PageProperty。

分配遍历空闲块，选最小足够块；大小相同时保留先遇到的低地址块，精确匹配提前停止。成功返回恰好 n 页，尾部剩余块留在原有地址顺序位置；失败不修改链表和计数。

注册和释放复用 best_fit_insert_block()，按地址插入后合并前后相邻块。仅调用者更新 nr_free，合并只改变块结构。注册清除初始保留标志，释放清除引用和块元数据，合并后清除被吸收块头的标记及长度。

默认 manager 已在 pmm.c 的 init_pmm_manager() 中改为 best_fit_pmm_manager。check 指向新增的 best_fit_check_all()，先执行教师原 best_fit_check()，再执行独立文件 best_fit_test.c 中的 best_fit_behavior_check()。教师 basic_check()、best_fit_check() 的内容逐字节保持原样；A 的 page_init() 保持原样，公共 Page 和 pmm_manager 接口未改。

Best-Fit 的改进空间：当前搜索最坏 O(k)，释放插入最坏 O(k)，k 是空闲块数量；精确匹配时可提前结束，但一般仍需遍历。可加入按大小排序的索引或平衡树，并保留地址索引用于合并，代价是更多元数据和更新逻辑。Best-Fit 可能留下难以复用的小块，不能仅凭名称认为碎片必然更少，应根据实际请求序列衡量。当前未实现上述索引优化。

## 验证状态

| 检查 | 当前结果 | 证据 |
|---|---|---|
| 普通 clean build | 通过，Best-Fit 与新增测试均参与编译，成功生成镜像 | [编译日志](../logs/b-best-fit-behavior-build.log)、[编译截图](../images/b-build.png) |
| A 初始化与 Best-Fit 联合运行 | 正确选中 best_fit_pmm_manager，教师自检成功 | [QEMU 日志](../logs/b-best-fit-behavior.log)、[布局截图](../images/b-best-fit-layout.png) |
| 补充行为测试 | 最佳匹配、精确匹配、同大小选择、拆分、前后及双侧合并、失败后不变、多轮恢复均通过 | [测试说明](b-best-fit-tests.md)、[测试截图](../images/b-best-fit-behavior.png) |
| 教师 make grade | Page Init、Best-Fit 均 OK，总分 20/20 | [评分日志](../logs/b-grade-behavior.log)、[评分截图](../images/b-grade.png) |
| 学号填写后的最终评分 | 注释填写为 2412559 后再次通过，20/20 | [最终评分及源码摘要](../logs/b-final-grade.log) |

验证使用 B 本地 WSL、SiFive GCC 10.2.0、QEMU 4.1.1、OpenSBI v0.4，保持默认 128 MiB RAM。自动验证完成后，用户也手工复现并截图；两次 QEMU 的启动和测试输出一致，仅退出方式不同，评分均为 20/20。重复的 manual 日志已删除，统一引用上述日志。

新增测试后的本次实际布局：

| 项目 | 实际值 |
|---|---|
| 内核 end VA | 0xffffffffc0207078 |
| Page 数组 VA / PA | 0xffffffffc0208000 / 0x80208000 |
| 描述符数量及字节数 | 32768 个，1310720 字节 |
| 初始空闲范围 | [0x80348000, 0x88000000) |
| 自检前、行为测试恢复后的空闲页数 | 均为 31928 |

相对 A 的 First-Fit 版本，新增代码使内核多占一页，初始空闲页数由 31929 变为 31928；这是布局变化，不能继续照抄 A 的固定地址或页数。测试使用运行时计数，并已检查结束后原空闲块、总空闲量及保留页数量恢复。

## 实际问题与处理

1. 原始 Best-Fit 文件缺少 free_list、nr_free 等状态，导致普通构建阻塞。补齐完整模块后，已恢复普通 make，无需继续使用 A 临时排除 Best-Fit 的参数。
2. 首次 make grade 在执行脚本时出现 `tools/grade.sh: 146: Syntax error: ")" unexpected (expecting "then")`，尚未进入算法评分。只读语法检查复现错误，字节检查确认脚本为 CRLF。将其转为 LF，并增加 `.gitattributes` 的 `*.sh text eol=lf` 后，sh -n 通过、教师评分达到 20/20。评分逻辑和断言均未修改。[修复验证日志](../logs/b-grade-fixed.log) 属于新增行为测试之前的历史验证，当前最终评分引用 b-grade-behavior.log。

上述是实际遇到的构建和脚本问题。本次没有发现并修复 Best-Fit 算法运行 Bug，不编造其他调试经历。自动 QEMU 日志末尾的 signal 15 是验证程序在检查到输出并等待 5 秒后主动终止空转内核，不代表 panic。

## 交接与复现

从本组实际工程的 Makefile 所在目录执行，确保工具链和 QEMU 已在 PATH：

```bash
make clean
make
make qemu
```

应看到 best_fit_pmm_manager、行为测试 ALL PASS、check_alloc_page() succeeded!。Ctrl+A，松开，再按 X 退出 QEMU 后运行：

```bash
make grade
```

预期两项 OK，总分 20/20。make grade 会清理本地构建并在临时副本中注入测试；不提交临时副本或 bin/、obj/。

实现过程和后续实际交互见 [实现 prompt 记录](../prompts/b-best-fit.md)，测试规格见 [测试 prompt 记录](../prompts/b-best-fit-tests.md)，交接文件清单及剩余事项见 [B 进度与交接](../notes/b-progress.md)。

## 尚待完成的联合事项

- 当前仓库尚无 C 的 Buddy 模块。需取得模块名、manager 符号、测试入口及非 2 的幂请求/释放契约，再确定切换方式和做联合回归；目前没有已实现的 Buddy 选择命令。
- 新增行为测试只适用于当前单段空闲区、至少 32 页、无并发的 Best-Fit 启动自检，不作为 Buddy 测试。
- 仍需在共同验收环境和最终小组提交上复现；本次 20/20 不能代替 Buddy、双重 GDB 或最终报告验收。
- B 的学号已由本人确认，Best-Fit 标记已填写为 2412559；A 的标记由 A 确认。
- B 的代码及证据已提交为 f8ed38a；C 获取 lab2 分支时同时保留后续文档提交，并整合其他成员章节。

本节已具备 B 阶段交接所需的实现、验证和材料索引，不代表小组全部实验已完成。
