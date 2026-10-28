# Lab3 B 阶段成果交接

更新时间：2026-10-28（Asia/Shanghai）。

- 负责人：B，陈铭宇，2412559。
- 所在分支：`lab3`，按 A 交接三人共用。
- 基线提交：`7a619bc9f54e794b851642e228d5633976c450aa`。
- B 实现提交 ID：`5c5461c6b71138c0af49e90c67e5fe7ecaf7bc64`，代码、自测工具和验证日志已推送至 `origin/lab3`。
- 报告、prompt 和本交接材料随独立文档提交交付；该文档提交可用 `git log -1 -- docs/notes/b-handover.md` 查询。
- 依赖：基线中的 A 内存初始化与 Trap 入口、恢复实现。

## 1. 已完成

1. 从 Lab2 迁移必要 Best-Fit 实现，保持 Lab3 教师检查，`.check = best_fit_check`，不引入旧测试包装依赖。
2. 完成 `interrupt_handler()`、`exception_handler()`，保留统一分发和打印函数。
3. 完成中断原因分类、持续时钟、计数、输出、关机，以及 16/32 位异常返回。
4. 用户确认暂按 10 次输出后关机，即 1000 ticks。该值属于本轮实现选择，非教师指定值。
5. 全量构建、正常运行、原始教师评分与 B 针对性自测通过。

## 2. 文件范围

正式源码修改：

- `kern/mm/best_fit_pmm.c`
- `kern/trap/trap.c`

新增：

- `tools/b-selfcheck.py`：只在系统临时目录中插入探针并切换 PMM。
- `docs/parts/b-trap-handlers.md`
- `docs/prompts/b-trap-handlers.md`
- 本交接文件与 `docs/logs/b-*.log` 真实验证证据。

没有修改 `kern/mm/pmm.c`、`kern/trap/trapentry.S`、`trap.h`、公共 `Makefile`、`kern/init/init.c` 或教师测试。没有向正式工程加入 `kern/test/`，默认仍为 First-Fit。

## 3. 复现命令

进入 WSL Ubuntu 后：

```bash
cd /mnt/d/3nd_grade/OS/lab/lab3/github
export PATH="/mnt/d/3nd_grade/OS/environment/riscv-toolchain/bin:/mnt/d/3nd_grade/OS/environment/qemu/bin:$PATH"
unset QEMU
make clean
make
make qemu
```

普通运行应输出 10 次 `100 ticks` 后自行退出。验证环境为 RISC-V GCC 10.2.0、QEMU 4.1.1，实际串口显示 OpenSBI 0.4。

```bash
make grade
make
python3 tools/b-selfcheck.py
```

`make grade` 的实际结果为 35/35、25/25、40/40，共 100/100；随后 `make` 已恢复普通内核镜像。自测脚本在两种 PMM 下均检查异常返回、诊断、时钟计数、PMM 中断状态恢复及实际关机。

自测脚本假定本轮确认的默认阈值为 10；未来改变阈值时需同步调整脚本预期。它会向 `docs/logs/` 更新自身的运行证据，不修改共享源码。

## 4. 证据和覆盖边界

- 普通构建：`docs/logs/b-full-build.log`
- 正常关机：`docs/logs/b-timer-shutdown.log`
- 教师评分：`docs/logs/b-grade.log`
- 评分后普通构建：`docs/logs/b-post-grade-build.log`
- 临时副本验证：`docs/logs/b-selfcheck-default*.log`、`docs/logs/b-selfcheck-best_fit*.log`
- 测试源码 SHA256 与工具版本：`docs/logs/b-selfcheck-manifest.log`
- Best-Fit 早期模块日志：`docs/logs/b-best-fit-module-build.log`

本轮没有新增截图；以原始文本日志为证据。B 自测不代替 C 的独立补充测试和最终共同回归。异常恢复仅覆盖本实验涉及的 16/32 位可读取指令，其他异常只诊断。

## 5. 请 A、C 配合

- A：在集成版本上完成 `stvec`、TrapFrame、`a0` 参数、返回 EPC、SP 和通用寄存器的 GDB 检查，更新 A 章节与证据。
- C：运行独立补充测试及共同环境回归，审阅正常关机与教师评分覆盖差异，汇总 B 章节到最终报告。
- 全组：提交前确认最终关机要求；若助教另有规定，更新代码、测试和报告。提交、推送时按共用分支规则核对远端，不强推。
