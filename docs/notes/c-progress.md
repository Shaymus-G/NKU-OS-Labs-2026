# C 部分交接：Buddy、验证与报告汇总

负责人：尚文轩，2412456。初次交付：2026-10-19；最新交接核对：2026-10-20。

## 分支与版本

共用 `lab2`；C 初次开发起点为 `a3574e2b288674f3d0045d6a71753bd94c5327ef`，成果已提交为 `b5c05e9706bedefb18cd7f6c3ad0b2404a8e5869` 并包含在远程分支。旧 c-* 日志的未提交状态描述仅对应 10 月 19 日开发阶段，以当时的 SHA256 关联代码，不改写历史证据。10 月 20 日已同步并核对远程 `cf881a985919d78a773bd77c09998f324f603d27`，包含 A 的 `cc646e0`、`6baf620` 和 B 的 `c3c4dd9`、`cf881a9`。

## 本次完成

1. 新增 Buddy 管理器、按阶空闲链表、初始化分解、物理页号 XOR 伙伴、取整分配和完整块回收。
2. 增加六组实际断言自检，含 1024 次混合操作与占用表验证，恢复 31928 初始空闲页。
3. 默认 Best-Fit 回归通过，本机教师 Page Init/Best-Fit 评分 20/20。
4. 完成个人章节、两份规格/提示词记录、真实失败与修复证据、总报告汇总。

## 文件清单和公共差异

| 文件 | 用途 |
|---|---|
| kern/mm/buddy_pmm.c/.h | Buddy 和自检，符号 buddy_pmm_manager |
| kern/mm/pmm.c | 仅新增 PMM_BUDDY 条件 include/选择；默认 Best-Fit |
| tools/check-buddy.py | 两种模式清理重建、启动验证、默认评分、恢复默认构建；新证据写入 c-runs 独立目录 |
| tools/qemu-grade-console.py | 本机半主机控制台与教师串口文件的评分转接 |
| tools/assemble-lab2-report.py | 从个人章节重建总报告 |
| tools/render-c-evidence.ps1 | 将真实日志摘录排版为 PNG，非原始终端截图 |
| docs/parts/c-buddy.md | C 个人章节 |
| docs/prompts/c-buddy.md、c-buddy-tests.md | 分开的实现/测试规格及过程来源说明 |
| docs/logs/c-*、docs/images/c-* | 本轮证据 |
| report.md | 汇总 A/B 原章节和 C 章节，保留历史版本边界 |

未改 Makefile、pmm.h、memlayout.h、A 的 page_init()、B 源码、教师 basic_check()/best_fit_check()、tools/grade.sh、tools/inject.py 或 OSopt 包装器。用户已授权完成 C 接入，公共入口差异记录在此；未向其他成员发送消息。

## 契约和复现

- `alloc_pages(n)` 向上取整至 2 的幂，空闲页数扣除实际块大小。
- `free_pages(base,n)` 必须提供原始请求 n 和原始头指针。3 页请求占用 4 页，必须按 3 释放，不能按 4 或部分释放。
- 范围 0..15 阶；零请求、非法释放断言，不在正常成功测试中运行。超过最大阶、无合适连续块返回 NULL。
- 不是线程安全接口，启动阶段单核自检；后续并发需外部串行化。

执行目录是仓库根目录：

```bash
source ~/OSopt/env.sh
make clean
make DEFS=-DPMM_BUDDY
make qemu DEFS=-DPMM_BUDDY
# 退出后切回：
make clean
make
make qemu
# 退出后评分；本机需转接 stdout/stderr：
make grade QEMU='python3 tools/qemu-grade-console.py'
# 或自动完成以上验证，留下默认 Best-Fit 构建：
python3 tools/check-buddy.py
```

本次环境为 WSL Ubuntu 22.04 / GCC 10.2.0 / QEMU 6.2.0 / OpenSBI 1.3，均使用现成 OSopt；没有 sudo 或安装工具。自动脚本终止故意空转的 QEMU 后核对实际断言输出，不能用超时替代测试通过。

2026-10-20 起自动脚本默认写入 `docs/logs/c-runs/<运行时间>/`，也可用 `--log-dir` 指定仓库内的新目录，已有目录会被拒绝。旧 c-*、a-*、b-* 日志均不覆盖；Git 状态由运行时实际读取。B 交接中对旧脚本覆盖历史日志的提醒属于修复前的事实，保留其原稿。

## 证据索引

- 成功：`c-buddy-build.log`、`c-buddy-run.log`、`c-best-fit-build.log`、`c-best-fit-run.log`、`c-grade.log`。
- 审计：`c-final-audit.log`，包含环境、公共差异和未改文件验证。
- 已修复失败：`c-buddy-first-failure.log`、`c-buddy-debug.log`、`c-grade-console-failure.log`。
- 图片：`c-buddy-log.png`、`c-grade-log.png`，为相应真实日志的摘录图，标题明确标注生成方式。
- 正文：`docs/parts/c-buddy.md` 和根目录 `report.md`。
- 10 月 20 日收尾与最新回归：[最终核验清单](c-final-review.md)，新证据独立保存在 `docs/logs/c-runs/2026-10-20-final/`。

## 联合验收更新及剩余核对

A 已在 Ubuntu 20.04 / QEMU 4.1.1 / OpenSBI 0.4 上完成两种管理器回归和普通评分 20/20；B 已复核 Buddy 完整块释放契约，并在 Ubuntu 24.04.5 / QEMU 4.1.1 / OpenSBI 0.4 上完成独立联合回归及普通评分 20/20。见 [A 回归](../logs/a-after-c-grade.log)、[B 环境](../logs/b-integration-environment.log)、[B 回归](../logs/b-integration-grade.log)。这两项不再列为待办，旧 c-* 日志仍作为 C 环境历史证据保留。

A 的 [分页前后成功补录](../logs/a-paging-transition-confirm.log) 和 [软件 TLB 直接命中](../logs/a-qemu-tlb-hit-after-c.log) 已交付。直接命中应表述为“后续取指地址查询直接命中已有 TLB 项”，不能称为“CPU 第二次执行 kern_init”。旧 First-Fit 的 31929 页 / 根页表 PA 0x80205000，与联合版的 31928 页 / 0x80206000 分别保留。

A 明确不交付四份历史日志：`a-build.log`、`a-qemu-pagewalk-retry.log`、`a-qemu-pagewalk.log`、`a-ucore-pagewalk.log`，均为“本地历史记录，未随本次提交交付”。不提供有效下载链接；成功页表遍历仍引用 `a-qemu-pagewalk-body.log`。

C 的 prompt 文档诚实标注为助手整理与最终复核所用规格，没有冒称学生逐字输入或独立模型对话；如课程要求完整原始交互，应另导出本次 Codex 会话。2026-10-20 用户已确认 A 学号 2412082、B 学号 2412559，三人代码标记已核对。仍需 A 补齐初始化实际 AI 交互记录，并由全组三人通读最终报告。本次按子任务分别在本地 commit，只使用尚文轩现有 Git 身份、不加 AI 共同作者；push 由用户进行。
