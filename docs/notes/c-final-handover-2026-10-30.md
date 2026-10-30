# Lab3 C 最终交接（2026-10-30）

负责人：尚文轩（C），2412456。分支lab3。接收人：国峻赫（A）、陈铭宇（B）。本文替代初次交接的当前状态，旧日志和旧章节保留版本含义。

## 1. 提交与依赖

最初三个提交 `a8ca059`、`2aeec0f`、`027fae3` 已由用户推送，本轮不重复将它们列为未推送。本轮新增：

| 提交 | 内容 |
|---|---|
| bb88599 | 隔离用户态闭环及初始权限负例 |
| 90dd248 | 初版扩展报告与交接 |
| 21b6881 | 课程通知、新版手册逐项核对；用户态改显式选项 |
| 0a96eb4 | 实际栈NX、内核栈和CSR恢复；Vectored真实路由 |
| df3c665 | 变异检出、可复现记录、最终评分与默认恢复 |

本交接及报告更新另行提交，以发送前git log为准。本轮用户没有授权push，由用户审阅后推送。提交作者与提交者均为shangwenxuan，无AI共同作者。依赖A/B在f475900前的完整内核；最终被测源文件和参数通过清单及临时源码留档标识，不用文档提交号代替源码证据。

## 2. 对照真正手册的完成度

两个核心模块已完成；C分工中的独立测试、公共接入、教师评分、共同版本本机回归、章节与报告汇总均已交付。手册stvec扩展不仅有原理说明，还实现真实路由实验；do-while(0)思考已解释。报告七项要求已逐项对应，见 [要求清单](lab3-requirements-audit.md)。

最新课程通知明确用户态Challenge是旧版遗留，今年无需完成。遵照用户追求高完成度的指令，已作为自主探索实现并验证，不能宣传为教师要求、加分承诺或必做项。完整用户进程、嵌套Trap、多核同步和通用页故障修复不属于本次已完成结论。

## 3. 源码与接入

- kern/lab3_checks/checks.c、context.S：四种异常编码、32槽快照、SIE保护、持续时钟。
- user.c、user_entry.S：独立用户代码/栈页、sscratch内核栈交换、两次ecall、四种保护负例。
- vectors.c、vector_entry.S：256字节对齐、4字节槽位的stvec向量表；同步异常BASE与SSIP中断BASE+4。
- tools/c-regression.py：仅在临时副本接入与切换PMM，支持--user-mode、--vectors和--mutant。
- tools/c-grade.py：C本机OpenSBI1.3的临时PATH控制台适配；A/B可继续普通make grade。

正式Makefile/init.c、A/B实现、SAVE_ALL/trapframe/函数签名、教师测试和OSopt均未改动。没有创建kern/test。默认First-Fit镜像不链接补充探针；测试结束已重新构建并实际运行默认镜像。

## 4. 实测结果与证据

环境：WSL Ubuntu22.04、GCC10.2、QEMU6.2、OpenSBI1.3，128MiB。以下目录时间戳采用运行宿主UTC，不能当作Asia/Shanghai墙钟时间。

| 检查 | 结果 | 日志入口 |
|---|---|---|
| 两PMM核心回归 | 各64次真实异常、32槽快照、PMM/SIE、201至1000 ticks、退出0 | [095900](../logs/c-runs/20261030-095900/results.json) |
| 两PMM全部扩展 | stvec双路由、U/S往返、内核栈、CSR恢复、4个保护负例通过 | [095832](../logs/c-runs/20261030-095832/results.json) |
| 漏恢复t0负例 | 两PMM均被断言检出 | [095748](../logs/c-runs/20261030-095748/results.json) |
| 固定EPC+4负例 | 两PMM均被断言检出 | [095810](../logs/c-runs/20261030-095810/results.json) |
| 教师评分重新执行 | 35+25+40=100/100 | [评分](../logs/c-final-20261030/grade.log) |
| 默认重建及正常关机 | 默认First-Fit，十次统计后退出0 | [运行](../logs/c-final-20261030/default-run.log) |

各c-runs目录包含default/best_fit构建与运行、invocation.json、results.json、原源码清单、临时变换后的关键源码及ELF哈希。本轮未新增截图，以原始文本日志与断言为证。A原五张截图及GDB日志继续交付，不倒填新结果。

## 5. 复现命令

在仓库根目录已激活工具链的WSL执行；C每个新shell先source ~/OSopt/env.sh，A/B使用各自环境：

```bash
python3 tools/c-regression.py
python3 tools/c-regression.py --user-mode --vectors
# 以下两项故意构造错误副本，PASS表示断言检出，不能用正式内核替换：
python3 tools/c-regression.py --mutant drop-t0
python3 tools/c-regression.py --mutant epc-always4
python3 tools/c-grade.py        # C本机；A/B普通make grade
make                           # 评分后恢复默认
python3 tools/assemble-lab3-report.py --check
```

每次运行生成新证据目录，已有目录不覆盖。故意错误的内核panic后由10秒外层timeout结束，判据同时要求非零返回和assertion failed；正常测试要求退出0，无panic，统计数量精确匹配。

## 6. 报告与限制

正式report.md由docs/report/lab3.md生成；详细C材料见 [C章节](../parts/c-validation.md)，规格与实际过程见 [基本规格](../prompts/c-validation.md)、[自主扩展规格](../prompts/c-user-challenge.md)。A/B原章节不覆盖。修正文案应编辑源稿后重新生成。

用户态向量限定关闭中断后的U源同步Trap，不支持S源/嵌套；Vectored探针限定单核S源无嵌套。四个用户负例分别是CSR非法访问、内核页读取、代码页写入、栈取指；权限故障按已知指令或预定标签恢复，不是修复映射。关机阈值10次统计/1000 ticks仍为实现选择，手册没有给出具体数值。

## 7. A/B还需做什么

1. A核对本人初始化、TrapFrame和GDB结论；如愿复核自主探索，重点看栈切换与SPP/CSR恢复。
2. B核对原因分类、长度推进、统计及关机描述；本轮没有修改你的实现。
3. 建议A/B在QEMU4.1.1环境运行两条正常回归命令，提供新的日志目录。当前只声明C环境通过新增扩展，不能冒充三人环境已全部复测。
4. 用户推送后，全组三人通读并确认最终版本，按课程实际入口提交。提交平台动作不由本会话代办。

## 8. 群内可直接发送

在实际push后替换提交号再发送：

> A、B，我已按最新版Lab3手册完成C的完整验收和报告补充，已推送lab3，最新提交【填写实际HEAD】，请同步整个分支。核心部分两PMM各64次真实异常、寄存器快照、中断保护、持续时钟和1000 ticks关机均通过，本轮重新评分100/100。手册的stvec扩展已做真实BASE/BASE+4路由实验；旧用户态Challenge虽今年取消，仍作为可选自主探索完成U/S往返和四种权限负例。还用故意错误副本验证测试能检出漏恢复寄存器与EPC推进错误。A/B实现及教师测试都没改。请看docs/notes/c-final-handover-2026-10-30.md，正式报告为report.md。A核对汇编与GDB章节，B核对处理函数与关机说明；方便时运行核心及完整扩展回归并把新日志发回，之后我们共同通读并正式提交。
