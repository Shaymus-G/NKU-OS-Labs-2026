# 成员 B 进度与交接

更新：2026-10-18。负责人：成员 B（陈铭宇）。

当前可交接：First-Fit 分析、完整 Best-Fit、默认接入、补充行为测试、日志、截图及个人章节。普通编译、QEMU 自检和补充断言通过，教师评分 20/20，可以作为 C 继续接入 Buddy 的阶段基础。

本地仓库：D:\3nd_grade\OS\lab\lab2\github；当前分支 lab2，跟踪 origin/lab2。基线为 A 的初始化提交 79b5791a358d2e5f684b09e09dbd201325577c59。B 的代码、测试、学号和验证证据交接提交为 [f8ed38a8d6fb02e1093995eeb99d0744101eabe1](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/f8ed38a8d6fb02e1093995eeb99d0744101eabe1)；本交接文档等由其后的文档提交补齐。队友应获取 lab2 分支的完整最新版本，不能只获取 A 的基线。

## 本批文件

| 文件 | 本阶段改动 |
|---|---|
| kern/mm/best_fit_pmm.c | 完整 Best-Fit 模块；新增 best_fit_check_all()，先执行教师自检，再执行补充行为测试 |
| kern/mm/best_fit_pmm.h | 增加行为测试入口声明 |
| kern/mm/best_fit_test.c | 新增最佳匹配、拆分、合并、失败和恢复的独立断言测试 |
| kern/mm/pmm.c | 相对 A 的提交仅更改默认 manager，page_init() 原样保留 |
| tools/grade.sh | 仅将 CRLF 转为 LF，评分逻辑不变 |
| .gitattributes | 新增 *.sh text eol=lf，防止再次检出 CRLF 脚本 |

教师 basic_check()、best_fit_check() 保持原样；未改公共 Page、pmm_manager 接口、Makefile、教师评分断言及注入逻辑。

## 验证及材料索引

| 验证 | 结果 | 日志 / 截图 |
|---|---|---|
| clean build | 成功生成镜像，测试文件参与编译 | [编译日志](../logs/b-best-fit-behavior-build.log)、[截图](../images/b-build.png) |
| 初始化和 manager | Best-Fit 启用，默认 128 MiB，初始空闲 31928 页 | [QEMU 日志](../logs/b-best-fit-behavior.log)、[布局截图](../images/b-best-fit-layout.png) |
| 教师及新增自检 | ALL PASS，check_alloc_page() succeeded!，最终恢复 31928 空闲页 | [测试说明](../parts/b-best-fit-tests.md)、[行为截图](../images/b-best-fit-behavior.png) |
| 当前教师评分 | Page Init / Best-Fit 均 OK，20/20 | [评分日志](../logs/b-grade-behavior.log)、[评分截图](../images/b-grade.png) |
| 学号填写后最终评分 | B 标记为 2412559，再次评分 20/20 | [最终评分及源码摘要](../logs/b-final-grade.log) |
| 脚本换行修复 | 语法检查通过，修复后初次评分 20/20 | [历史修复日志](../logs/b-grade-fixed.log) |

用户已手工复现并截图。manual 日志与已有日志核对后去重，统一保留上表的编译、QEMU、当前评分日志；历史修复日志单独保留。

B 使用本地 WSL、SiFive GCC 10.2.0、QEMU 4.1.1、OpenSBI v0.4。A 的 Ubuntu 20.04 共同验收环境仍需在最终版本复现。QEMU 日志记录了相关源码 SHA256；地址及页数对应本次构建。

## 当前验证入口

先确保无仍在使用当前内核的调试会话，工具链和 QEMU 已在 PATH。B 本地执行：

```bash
cd /mnt/d/3nd_grade/OS/lab/lab2/github
make clean
make
make qemu
```

队友在自己的仓库根目录执行相同 make 命令即可，不需要照搬 B 的绝对目录。无需 A 临时排除 best_fit_pmm.c 的参数。

检查 best_fit_pmm_manager、所有行为 PASS、original state restored 和 check_alloc_page() succeeded!。Ctrl+A，松开，再按 X 退出后执行：

```bash
make grade
```

预期两项 OK、20/20。评分会清理本地构建产物并在临时副本注入测试，之后 make qemu 会重新构建。

## 后续批次

1. B 的学号已由本人确认，Best-Fit 标记已填写为 2412559；A 的标记由 A 确认。
2. B 的代码及证据已提交为 f8ed38a，本文件所在后续文档提交补齐说明。获取远程 lab2 最新版本后核对本表证据及复现结果。
3. 接收并复核 C 的 Buddy 代码和测试，确定实际切换入口并联合验证；切换后清理重建、重新启动。
4. 在最终小组提交和共同验收环境上回归；由 C 汇总报告，三人核对。当前 20/20 不涵盖 Buddy、双重 GDB 或报告。

## 给 A 的交接要点

- 本次 pmm.c 仅改默认管理器选择。合并时保留 A 的 page_init()，不要整文件覆盖。
- B 的代码及证据提交为 f8ed38a，后续文档提交补齐报告和交接材料；合并时同时保留。
- Best-Fit 和补充行为测试已完成本地编译、运行与评分验收，可作为阶段基线；最终联合版本仍需回归。

## 给 C 的交接要点

- 目前没有 Buddy 文件或选择宏，勿把计划中的示例选项当作现有命令。
- 请交接公开的 manager 符号、源码/测试文件位置、是否需公共接口调整及实际测试命令。
- 默认 Best-Fit 用于教师评分；Buddy 需通过明确入口独立验证。
- 新增行为测试仅由 Best-Fit 的 check 回调触发，要求启动时单个至少 32 页的连续空闲区及无并发分配；它不是 Buddy 的测试。结束时已核对空闲区恢复。
- 切换模式需重新构建并重启，不能在初始化后直接替换指针沿用旧分配器状态。

## C 汇总报告使用的文档

- [First-Fit 分析、Best-Fit 规格、改进讨论、问题修复及结果](../parts/b-first-best-fit.md)
- [补充行为测试与覆盖理由](../parts/b-best-fit-tests.md)
- [实现要求、原始约束及后续实际 AI 交互](../prompts/b-best-fit.md)
- [行为测试要求及规格](../prompts/b-best-fit-tests.md)

日志和截图按上表引用，不提交 bin/、obj/、QEMU 或工具链。上述文档已从旧工程迁入并更新，新仓库是当前交接入口；旧目录的文件保留为最初历史记录，不作为当前进度依据。
