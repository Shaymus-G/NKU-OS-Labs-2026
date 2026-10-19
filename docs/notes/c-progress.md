# C 部分交接：Buddy、验证与报告汇总

负责人：尚文轩，2412456。日期：2026-10-19。

## 分支与版本

共用 `lab2`；起点 `a3574e2b288674f3d0045d6a71753bd94c5327ef`，已核对与远程一致。包含 A 初始化、B 实现及交接、A 调试材料。C 本轮尚未 commit/push，不能提供不存在的提交号；代码版本见每份成功日志中的 SHA256。

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
| tools/check-buddy.py | 两种模式清理重建、启动验证、默认评分、恢复默认构建 |
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

## 证据索引

- 成功：`c-buddy-build.log`、`c-buddy-run.log`、`c-best-fit-build.log`、`c-best-fit-run.log`、`c-grade.log`。
- 审计：`c-final-audit.log`，包含环境、公共差异和未改文件验证。
- 已修复失败：`c-buddy-first-failure.log`、`c-buddy-debug.log`、`c-grade-console-failure.log`。
- 图片：`c-buddy-log.png`、`c-grade-log.png`，为相应真实日志的摘录图，标题明确标注生成方式。
- 正文：`docs/parts/c-buddy.md` 和根目录 `report.md`。

## 后续共同验收

请 B 复核选择入口与 Buddy 完整块释放契约，并在共同 QEMU 4.1.1 环境复测；本机评分 20/20 不替代 Buddy 独立测试、双重 GDB 或全实验验收。请 A 核对自己的学号标记并补足其报告已指出的证据缺口：独立后续 TLB 命中、完整分页前后实测对照。旧 First-Fit 31929 页与本次 31928 页分别保留，不改旧证据统一数值。

C 的 prompt 文档诚实标注为助手整理与最终复核所用规格，没有冒称学生逐字输入或独立模型对话；如课程要求完整原始交互，应另导出本次 Codex 会话。提交前还需全组三人通读和共同环境验收。当前遵照用户要求未 commit/push。
