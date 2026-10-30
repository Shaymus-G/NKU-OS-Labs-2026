# Lab3 C交接

> 本文保留2026-10-29初次交接。2026-10-30已完成进一步扩展、错误注入及最终回归，当前统一交接见 [C最终交接](c-final-handover-2026-10-30.md)。下文“没有实现用户态”仅描述初次交付，不代表最新状态。

负责人：尚文轩，2412456。日期：2026-10-29。分支：lab3。

## 成果与提交

- 规格提交 `a8ca059`；测试及运行证据提交 `2aeec0f`。
- 独立测试基于A/B内核 `f475900`，未改动A/B源码、教师测试、正式Makefile/init.c或OSopt包装器。未建立kern/test。
- 测试代码：kern/lab3_checks/checks.c、context.S；临时接入：tools/c-regression.py；C环境评分控制台适配：tools/c-grade.py。
- First-Fit、Best-Fit各64次真实异常、32寄存器槽位快照、PMM成功/失败与嵌套SIE保护通过；真实时钟跨200，十次统计后正常退出。C教师评分100/100。默认First-Fit镜像已重建。
- 环境：WSL Ubuntu22.04、GCC10.2、QEMU6.2/OpenSBI1.3；与A/B原环境分开记录。

## 证据与复现

最终运行目录：[c-runs/20261029-034244](../logs/c-runs/20261029-034244/manifest.log)，其default/best_fit构建和运行日志均已提交。早期目录分别是首次未保存运行输出、链接入口失败和CRLF判据失败，不能作为最终成功证据。[教师评分](../logs/c-grade.log)、[默认恢复](../logs/c-default-restored-build.log)。本轮无新增截图，使用原始文本日志。

在仓库根目录已激活工具链的WSL中：

```bash
python3 tools/c-regression.py
python3 tools/c-grade.py      # C环境，A/B可以普通make grade
make
python3 tools/assemble-lab3-report.py --check
```

## 报告入口

正式源稿为 [docs/report/lab3.md](../report/lab3.md)，运行tools/assemble-lab3-report.py生成根目录report.md。不要只修改生成文件。详细个人材料保留在docs/parts，未整份覆盖A/B报告。C规格见 [c-validation](../prompts/c-validation.md)，个人章节见 [独立验收](../parts/c-validation.md)。

## 剩余协作

1. A核对正式报告初始化、TrapFrame和GDB章节；当前已经写明A完成真实时钟调试，不沿用早期“尚未调试”。
2. B核对异常长度和关机说明；1000 ticks仍是当前实现选择。如助教指定其他阈值，需要同步代码、运行器预期和报告。
3. A/B可在共同环境运行C脚本；现有C结果不冒充A/B已复测本轮新增探针。
4. 用户审阅并push本地提交后通知队友同步，全组三人通读、核对原手册与平台提交要求并正式提交。

没有实现用户态Challenge；分工材料明确其没有完整必做要求，当前记录也不声称完成用户态切换。若课程另有明确补充，再扩展范围。本次没有发送群消息或执行push。

## 可复制的群消息

以下文字应在用户实际push后使用，并补入实际提交号：

> A、B，我已完成C的独立验收和总报告，已推送lab3，最新提交号为【填写实际HEAD】，请同步整个分支。新增测试在临时工程接入，正式Makefile/init.c、你们的实现和教师测试都没改。First-Fit与Best-Fit各通过64次真实异常、寄存器快照、中断保护、持续时钟和1000 ticks关机；C本机评分100/100。正式报告在report.md，修改入口为docs/report/lab3.md。A请重点核对初始化、汇编和GDB部分；B请核对处理函数和关机阈值说明。详细证据、命令及限制见docs/notes/c-handover.md。你们方便时在原环境跑一下python3 tools/c-regression.py，完成后我们三人通读并正式提交。
