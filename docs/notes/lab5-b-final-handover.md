# Lab5 B 阶段 5～6 联合验收交接

陈铭宇，2412559；2026-11-27（Asia/Shanghai）。

## 本轮版本与变更

集成源码基线为 `a7295ce9872cde2dc5dc0964b006b703a167aa54`，本轮没有新增内核代码修改。阶段 5～6 的验收工具、日志及总报告已提交为 `254d151fe892c49184e9eafa5e6ee99dcee5f545`（简称 `254d151`），本交接说明由后续文档提交补充，随本次交付发布到共同分支 `lab5`。先前 B 实现提交 240b6b9、文档提交 6ec1964 与 C 的全部提交已在基线中。

[查看阶段 5～6 验收与总报告提交](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/254d151fe892c49184e9eafa5e6ee99dcee5f545)。组员先 fetch 并检查本地状态，工作区干净且可快进时执行 `git pull --ff-only origin lab5`；确认历史包含 `254d151` 和后续 B 交接文档提交，再进行复核。有修改或分叉时先保存与审查，不强行覆盖。

- 根目录 report.md 已改为 [Lab5 总报告](../../report.md)，参考其他分支的结构，内容基于本实验源码与证据。
- 原根目录 Lab4 报告归档到 docs/archive/lab4-report.md，不将旧 30/30 当作 Lab5 评分。
- tools/verify-lab5-b.py：正式构建、hello、专项、评分、重建与客体 GDB；另有临时用户边界用例。
- tools/lab5-dual-gdb.py：同一 QEMU 进程的宿主/客体 GDB 协调与断言。
- tools/lab5-guest-roundtrip.gdb：兼容本机 QEMU 4.1.1 的 CSR/断点访问限制，不依赖直接读取全部 CSR。
- A/B 早期阶段报告新增联合结果索引，保留原始阶段正文；没有替 A/C 改写历史验证过程。

## 已实际通过

| 本机环境 | 结果 |
| --- | --- |
| GCC 10.2 / QEMU 4.1.1 / OpenSBI v0.4 | 干净构建，真实 hello 与 init 回收通过 |
| COW 与逐页复制 | 各 8 个真实进程回归通过；真实耗尽、引用、孤儿与回收通过 |
| 用户边界补充 | 两种复制模式下非法号返回、非法 exec 保留旧程序、合法用户 ELF 执行并退出 42 通过 |
| 原样 make grade | 130/130；未用 C 的串口转接器，未改教师断言 |
| 客体 GDB | 入口 0x800020，ecall 0x8000aa，分发后 0x8000ae，sret 返回该用户地址，a0=0 |
| 双重 GDB | trans_ecall、异常 helper、riscv_cpu_do_interrupt、trans_sret、helper_sret 均实际命中；宿主 priv 0→1→0，客体回到用户 |

运行证据见 [集成日志](../logs/b-integration-20261127/grade.log)、[双重 GDB 摘要](../logs/b-dual-gdb-20261127-attempt5/summary.log)与 [详细分析/复现](../parts/b-integration.md)。双重 GDB 使用构建目录未 strip 的 QEMU，其 Build ID 与 .text 哈希均与安装版本一致。

## 组员复核

A 复核 trap、异常状态和本轮补齐的宿主 QEMU/TCG 分析；C 复核 COW/退出/调度段落、生命周期日志及自己的贡献描述；B 维护唯一总报告和原始日志索引。当前没有必做功能待接入，不把本轮工具失败日志误读为内核验收失败。

Dirty COW 部分解释了本实验缺少 Linux 对应接口，不能直接复现原 CVE；现有测试覆盖相近权限/别名写回/失败回滚条件，没有宣称完整复现 Linux 漏洞。多核、通用按需缺页等课程外能力明确列为未实现。

本文件是供用户发送的文档，没有代发群消息。本次 GitHub 发布按用户明确要求执行，验收快照日志中的“未推送”字段保留其当时状态，不代表当前交付状态。
