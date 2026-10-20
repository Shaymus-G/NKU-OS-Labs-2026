# 成员 B 进度与交接

更新：2026-10-19。负责人：成员 B（陈铭宇，2412559）。

当前状态：B 的 First-Fit 分析、完整 Best-Fit、默认接入、补充行为测试及初次交接已完成。现已获取 C 的 Buddy 和 A 的最新证据，完成 Buddy 复核、两种管理器切换及本地联合回归；普通教师评分 20/20。报告核对后先按要求另列工作区根目录问题清单，随后按用户新指令只更新 github/report.md 的 B 部分，简介、A/C 章和汇总脚本保持原样。联合回归证据与 B 报告更新已提交为 c3c4dd9，交接文档由后续提交补齐。

本地仓库：D:\3nd_grade\OS\lab\lab2\github；当前分支 lab2，跟踪 origin/lab2。基线为 A 的初始化提交 79b5791a358d2e5f684b09e09dbd201325577c59。B 的代码、测试、学号和验证证据交接提交为 [f8ed38a8d6fb02e1093995eeb99d0744101eabe1](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/f8ed38a8d6fb02e1093995eeb99d0744101eabe1)；本交接文档等由其后的文档提交补齐。队友应获取 lab2 分支的完整最新版本，不能只获取 A 的基线。

本轮复核和联合回归的源码 HEAD 为 6baf620ebb414d821c1e5647bdec9cb486636a5e，包含 C 的 b5c05e9 Buddy 提交、A 的 cc646e0 共同环境回归及 6baf620 分页/TLB 补证。当前算法和测试源码未作修改，日志记录了 HEAD、环境及源码 SHA256。

本轮联合回归及 B 报告交接提交：[c3c4dd962cafea8050a1a1048973172e7408b244](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/c3c4dd962cafea8050a1a1048973172e7408b244)。队友获取 lab2 最新分支时应同时保留后续的 B 文档补充提交。被测源码版本仍是 6baf620，不能把日志中的原始 HEAD 改写成交接提交号。

## 10 月 18 日已交接文件

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

B 首次验证使用本地 WSL、SiFive GCC 10.2.0、QEMU 4.1.1、OpenSBI v0.4。上表为 10 月 18 日版本的历史证据，后续联合版本验证见下一节；不把新结论倒填进旧日志。

## 10 月 19 日复核与联合回归

### Buddy 复核结论

未发现阻塞本次接入的算法问题。C 的代码与文档中的取整分配、按原始请求完整释放、物理页号 XOR 伙伴、边界保护及实际占用计数相符。Page、pmm_manager、A 的 page_init()、B 的算法和教师检查保持原样。独立自检包含真实断言和占用表，不以打印 PASS 替代检查。

### B 本地实际回归

本次确认为 WSL Ubuntu 24.04.5 LTS、SiFive GCC 10.2.0、QEMU 4.1.1、OpenSBI v0.4，默认 128 MiB RAM。使用原教师评分，无需 C 的控制台转接脚本。

| 检查 | 实际结果 | 本轮独立日志 |
|---|---|---|
| 版本与环境 | HEAD 6baf620，记录工具版本及源码摘要 | [环境](../logs/b-integration-environment.log) |
| Buddy 清理构建 | DEFS=-DPMM_BUDDY，成功 | [构建](../logs/b-integration-buddy-build.log) |
| Buddy 自检 | 拆分、取整、非伙伴、碎片、不规则初始化、1024 次混合操作通过，31928 → 31928 | [运行](../logs/b-integration-buddy-run.log) |
| 切回 Best-Fit | 清理后不带 Buddy 宏重新构建，行为测试 ALL PASS，恢复 31928 页 | [构建](../logs/b-integration-best-fit-build.log)、[运行](../logs/b-integration-best-fit-run.log) |
| 普通教师评分 | 两项 OK，20/20，退出码 0 | [评分](../logs/b-integration-grade.log) |
| 评分后默认构建恢复 | 再次普通 make 成功；符号表包含 Best-Fit manager，不含 Buddy manager | 同 Best-Fit 构建日志末尾 |

本轮只终止验证程序自己启动的 QEMU 进程组。运行输出的 signal 15 来自内核自检完成、进入空转后由验证程序于 6 秒时结束进程，不是 panic。A/C 及 B 旧日志、截图没有被覆盖。本轮没有新生成截图。

A 的 Ubuntu 20.04 / QEMU 4.1.1 共同环境回归也已提交，见 a-after-c-* 日志及 cc646e0；不能继续把这项写成没有开展。小组最终报告的共同通读和提交仍应单独确认。

## 当前验证入口

先确保无仍在使用当前内核的调试会话，工具链和 QEMU 已在 PATH。B 本地执行：

```bash
cd /mnt/d/3nd_grade/OS/lab/lab2/github
make clean
make
make qemu
```

队友在自己的仓库根目录执行相同 make 命令即可，不需要照搬 B 的绝对目录。无需 A 临时排除 best_fit_pmm.c 的参数。

以上为默认 Best-Fit。Buddy 的实际入口为：

```bash
make clean
make DEFS=-DPMM_BUDDY
make qemu DEFS=-DPMM_BUDDY
```

退出后切回默认模式，必须重新执行 make clean、make，再启动。不要直接运行 C 的一键脚本来生成 B 的证据：它会覆盖 c-* 历史日志，且固定写入未提交状态。B 本轮独立执行上述构建/启动流程，评分使用普通 make grade。

检查 best_fit_pmm_manager、所有行为 PASS、original state restored 和 check_alloc_page() succeeded!。Ctrl+A，松开，再按 X 退出后执行：

```bash
make grade
```

预期两项 OK、20/20。评分会清理本地构建产物并在临时副本注入测试，之后 make qemu 会重新构建。

## 剩余共同事项

1. B 的学号已由本人确认，Best-Fit 标记已填写为 2412559；A 的标记由 A 确认。
2. B 的代码及证据已提交为 f8ed38a，本文件所在后续文档提交补齐说明。获取远程 lab2 最新版本后核对本表证据及复现结果。
3. C 的 Buddy 接收、静态复核、选择入口和 B 本地联合回归已完成，不再作为待办。
4. C/组长修正过时报告源及汇总脚本，再生成报告，全组三人核对。工作区根目录 Lab2报告检查问题.md 保存了具体位置和建议；本轮只更新 report.md 的 B 部分，C 的文档、A/C 报告章节和汇总脚本未改。根目录问题清单和 B 修改记录不属于 Git 仓库，需分享时单独发送。
5. B 联合交接时 A 的 page_init() 学号标记仍为 YOUR CODE；2026-10-20 用户已确认其学号为 2412082，由 C 补齐，此项已完成。
6. 本轮联合回归证据及 B 报告更新已提交为 c3c4dd9，当前文档由后续提交补齐，按用户授权一并推送 lab2。20/20 仅覆盖教师两项，Buddy 另由自检支持，调试结论由 A 的证据支持。

## 给 A 的交接要点

- 本次 pmm.c 仅改默认管理器选择。合并时保留 A 的 page_init()，不要整文件覆盖。
- B 的代码及证据提交为 f8ed38a，后续文档提交补齐报告和交接材料；合并时同时保留。
- Best-Fit、Buddy 和切换后的本地回归已在 6baf620 完成；若再修改算法或构建配置，按变化范围决定是否重跑。

## 给 C 的交接要点

- 已接收 kern/mm/buddy_pmm.c/.h，导出 buddy_pmm_manager；DEFS=-DPMM_BUDDY 为已验证入口，默认仍为 Best-Fit。
- 已复核契约：alloc_pages(n) 按 2 的幂取整，nr_free_pages() 按实际占用统计；free_pages(base,n) 必须使用原始请求和原始块头。3 页请求占用 4 页，必须用 3 释放，不能传 4、内部地址或部分释放。
- 默认 Best-Fit 用于教师评分；Buddy 需通过明确入口独立验证。
- 新增行为测试仅由 Best-Fit 的 check 回调触发，要求启动时单个至少 32 页的连续空闲区及无并发分配；它不是 Buddy 的测试。结束时已核对空闲区恢复。
- 切换模式需重新构建并重启，不能在初始化后直接替换指针沿用旧分配器状态。

## C 汇总报告使用的文档

- [First-Fit 分析、Best-Fit 规格、改进讨论、问题修复及结果](../parts/b-first-best-fit.md)
- [补充行为测试与覆盖理由](../parts/b-best-fit-tests.md)
- [实现要求、原始约束及后续实际 AI 交互](../prompts/b-best-fit.md)
- [行为测试要求及规格](../prompts/b-best-fit-tests.md)

日志和截图按上表引用，不提交 bin/、obj/、QEMU 或工具链。上述文档已从旧工程迁入并更新，新仓库是当前交接入口；旧目录的文件保留为最初历史记录，不作为当前进度依据。
