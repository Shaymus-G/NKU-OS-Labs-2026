# B：扩展复测与正式报告复核

日期：2026-10-21。负责人：陈铭宇，2412559。本轮成果提交为 [6cc33466b2dbbe3cf12cfea196087cc412b160dd](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/6cc33466b2dbbe3cf12cfea196087cc412b160dd)，本提交号和统一交接记录由后续文档提交补齐；队友同步 `lab2` 完整分支。

## 版本与环境

被测内核源码 HEAD：`35c479da0ca0ba300a777cf1830808857b5c3f76`。本轮未修改 `kern/`、`libs/`、Makefile、教师评分或注入逻辑。只为现有扩展工具增加 `--native-grade`，使其可选择普通 `make grade`；不带此参数时维持 C 的控制台转接入口。此工具修改已随完整矩阵实际执行通过。

环境：WSL Ubuntu 24.04.5 LTS、SiFive GCC 10.2.0、QEMU 4.1.1、OpenSBI 0.4。默认 128 MiB，容量矩阵另有 64、96、256 MiB。见 [环境与工具摘要](../logs/b-extension-20261021-102252/environment.log)、[脚本执行汇总](../logs/b-extension-20261021-102252-suite.log)。日志保留原始被测版本号，不将之后的文档修改冒充被测提交。

## 实际结果

| 项目 | 结果 | 证据 |
|---|---|---|
| Best-Fit 下完整 SLUB | 四种容量全部通过 | [128 MiB](../logs/b-extension-20261021-102252/slub-best-fit-run.log)、[64 MiB](../logs/b-extension-20261021-102252/slub-best-fit-memory-64-run.log)、[96 MiB](../logs/b-extension-20261021-102252/slub-best-fit-memory-96-run.log)、[256 MiB](../logs/b-extension-20261021-102252/slub-best-fit-memory-256-run.log) |
| Buddy 下完整 SLUB | 四种容量全部通过 | [128 MiB](../logs/b-extension-20261021-102252/slub-buddy-run.log)、[64 MiB](../logs/b-extension-20261021-102252/slub-buddy-memory-64-run.log)、[96 MiB](../logs/b-extension-20261021-102252/slub-buddy-memory-96-run.log)、[256 MiB](../logs/b-extension-20261021-102252/slub-buddy-memory-256-run.log) |
| 非法释放 | 两种 PMM 各三次，共六次命中预期断言 | [结果及表达式](../logs/b-extension-20261021-102252/results.json)；对应 `slub-*-negative-*-run.log` |
| DTB 解析器 | 2230 项通过 | [宿主 C 解析器测试](../logs/b-extension-20261021-102252/dtb-parser.log) |
| 无扩展 Buddy | 全部自检通过，31928 → 31928 页 | [构建](../logs/b-extension-20261021-102252/baseline-buddy-build.log)、[运行](../logs/b-extension-20261021-102252/baseline-buddy-run.log) |
| 无扩展 Best-Fit | 全部行为检查通过，恢复 31928 页 | [构建](../logs/b-extension-20261021-102252/baseline-best-fit-build.log)、[运行](../logs/b-extension-20261021-102252/baseline-best-fit-run.log) |
| 普通教师评分 | Page Init、Best-Fit 均 OK，20/20，退出码 0 | [评分](../logs/b-extension-20261021-102252/default-grade.log) |
| 最终构建 | 默认 Best-Fit；无 Buddy manager、slub_check 链接符号 | [产物摘要](../logs/b-extension-20261021-102252/default-artifact.log) |

正常 SLUB 自检覆盖对齐、构造回调、复用、full/partial/empty 转移、shrink、27 个尺寸边界、64 个小对象共享一页、1024 次混合操作、元数据及真实物理页耗尽。矩阵独立核对 DTB 容量、管理末端、描述符数量与字节数、空闲区计算和最终恢复计数。64/96/128/256 MiB 下两种 PMM 分别恢复 15701/23813/31925/31925 页；256 MiB 的探测末端为 0x90000000，管理末端仍为 0x88000000。

六次非法释放依次为重复释放、错误缓存、内部指针。除了脚本检查 panic，还逐项核对分配位图、缓存归属、槽地址对齐对应的实际断言，避免将其他 panic 误计为通过。负例构建的内核占用与正常构建可能不同，不能要求它们打印相同初始页数。

所有正常运行均无 panic。测试程序在输出结束、内核空转后于 12 秒结束自己创建的进程组；末尾 signal 15 为测试程序收尾。没有终止其他用户会话。旧日志及截图均保留，本轮没有新增截图。

## 复现

在仓库根目录、WSL 中执行，工具链与 QEMU 4.1.1 已加入 PATH，`QEMU` 未被设置为目录：

```bash
python3 tools/test-dtb-parser.py
python3 tools/check-extensions.py --native-grade --negative-tests --memory-matrix \
    --log-dir "docs/logs/b-extension-$(date +%Y%m%d-%H%M%S)"

# 上述脚本已经执行普通 make grade 并重建默认镜像。
# 单独验证无扩展 Buddy，退出后再切回默认 Best-Fit：
make clean
make DEFS=-DPMM_BUDDY
make qemu DEFS=-DPMM_BUDDY
# Ctrl+A，松开，再按 X
make clean
make
make qemu
# Ctrl+A，松开，再按 X
```

本次自动执行了相同的基础构建/启动命令，并保存各次日志；普通评分已在扩展脚本尾部通过，不重复计作第二次评分。

## 报告核对与改动

已对照 `default_pmm.c`、`best_fit_pmm.c`、`best_fit_test.c`、`buddy_pmm.c`、`slub.c/.h`、`slub_test.c`、DTB 解析和初始化代码，复核正式章节 03-fit、04-buddy、05-slub、07-validation 及相关分析。未发现需要改动分配器实现的阻塞问题。

- First-Fit 的首个足够块、Best-Fit 的最小足够块及同大小低地址选择、拆分和双侧合并与实现一致。
- Buddy 按实际占用计数，释放使用原始请求；SLUB 大对象头记录的申请页数与此契约一致。
- 报告所列 slab 头 80 字节、37 字节对象按 64 对齐时容量 62，以及三页大对象在 Buddy 下占四页的计算一致。
- 教师 `basic_check()`、`best_fit_check()` 与课程 `lab2.zip` 经 LF 归一化后逐字节一致；内核、公共库及教师测试与 HEAD 一致。见 [源码与报告审计](../logs/b-extension-20261021-102252/source-report-audit.log)。
- 03-fit 补入最新基础回归；05-slub 补入 B 环境验证；07-validation 更新原环境待复测状态、独立证据和普通评分入口。随后由八份正文重新生成仓库 `report.md`，未直接改写生成文件中的孤立段落。

第一次报告检查采用 WSL Git 的默认换行配置，将 Windows 的 CRLF 检出误报为整文件修改及尾随空白。该次原始输出保留在 `source-report-audit-initial.log`；后续只在检查进程中使用 `core.autocrlf=true` 与 Windows 检出一致，报告检查通过。没有因此重写教师文件或持久修改 Git 配置。最终文档检查另存 `report-validation.log`。

## 仍待其他环节处理

自动 GDB 演示未在 B 本轮执行，不将 SLUB/DTB 通过扩展成整套演示已获 B 验证。A 的真实初始化 AI 交互仍需 A 补齐；三人通读、实验原 PDF 逐项核对、正式课程提交尚待小组安排。本工作区已有 `lab2的网页pdf`，可供后续手册核对。用户现已授权提交和推送，本仓库更新不代表已完成课程入口提交。
