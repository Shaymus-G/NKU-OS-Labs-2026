# C：最终汇总与提交前核验

日期：2026-10-20。负责人：尚文轩，2412456。

## 本轮子任务

| 子任务 | 完成内容 | 本地提交 |
|---|---|---|
| 1. 作者源与交接状态 | C 成果状态更新为已在远程；记录 A/B 联合验收；按 A 确认说明四份日志不交付 | `884c098` |
| 2. 汇总入口和正文 | 先改汇总脚本，再生成报告；纳入 A 新章节、B 最新原稿和 C 更新；增加只读 `--check` | `fa90737` |
| 3. 最终核验与复现工具 | 校验附件和版本边界；新回归日志独立保存；读取真实 Git 状态；本机重新回归 | 本文件、工具与新日志随第三个子任务提交，完整提交号见 Git 历史 |

所有本轮提交使用现有个人 Git 身份 `shangwenxuan <shangwenxuan.nku@gmail.com>`，不添加 AI 共同作者。用户负责最终 push；本次未执行 push。

## 报告与附件核验

- 总报告来自最新四份作者源章节；B 的两个章节与交接原稿保持原样。
- A 的旧版 31929 页 / 根页表 PA `0x80205000` 与联合版 31928 页 / `0x80206000` 均保留，未统一替换。
- A 分页切换、TLB 直接命中已纳入总报告，限定为后续取指地址查询直接命中，未写成“CPU 第二次执行 kern_init”。
- 四份不交付历史日志只以纯文本说明，不提供下载链接；汇总器即使发现本地同名文件也不会生成其附件链接。
- `tools/check-lab2-report.py` 核对生成一致性、`docs/parts`、`docs/notes` 和总报告的仓库内链接、历史版本与证据入口。外部网页内容不在本次自动检查范围。
- 不批量删除“待补、学号、未完成”等词；历史过程、测试边界和真实待确认项继续保留。

核验命令：

```bash
python3 tools/assemble-lab2-report.py --check
python3 tools/check-lab2-report.py
git diff --check
```

## 本机最新回归

不改算法、教师测试、公共接口或 Makefile。使用已有 WSL Ubuntu 22.04 / GCC 10.2.0 / QEMU 6.2.0 / OpenSBI 1.3：

```bash
source ~/OSopt/env.sh
python3 tools/check-buddy.py --log-dir docs/logs/c-runs/2026-10-20-final
```

该目录用于本次已完成运行，重复验证应省略参数使用自动时间目录，或指定另外一个新目录。已有目录会被拒绝，避免覆盖历史证据。

实际结果：Buddy 各组断言及 1024 次混合操作通过，31928 → 31928；默认 Best-Fit 行为通过；本机通过控制台转接运行未修改的教师评分，两项 OK，20/20。评分后已恢复默认 Best-Fit 构建。每份新日志记录实际 HEAD、工作树状态和源码 SHA256，旧 c-*、a-*、b-* 证据保持不变。

- [Buddy 构建](../logs/c-runs/2026-10-20-final/c-buddy-build.log)、[Buddy 运行](../logs/c-runs/2026-10-20-final/c-buddy-run.log)
- [Best-Fit 构建](../logs/c-runs/2026-10-20-final/c-best-fit-build.log)、[Best-Fit 运行](../logs/c-runs/2026-10-20-final/c-best-fit-run.log)
- [评分 20/20](../logs/c-runs/2026-10-20-final/c-grade.log)、[本轮核验记录](../logs/c-report-review-2026-10-20.log)

教师评分仍不覆盖 Buddy、调试和最终报告。A/B 已在各自 QEMU 4.1.1 环境完成普通评分，不需要使用 C 本机的控制台转接脚本。

## 仍需本人或全组确认

1. 学号事项已完成：2026-10-20 用户确认 A 为 2412082、B 为 2412559，C 为此前确认的 2412456；已核对并填写三人代码标记。
2. `docs/prompts/a-page-init.md` 的实际交互、验证和问题记录仍有占位文字；应由 A 根据真实过程补齐，不能由 C 编造。
3. 全组三人通读及最终提交版本签认由本人完成；C 的自动核验不代表已代替三人确认。
4. 此清单原收尾时 SLUB 尚未实现；随后用户明确要求全部已知扩展，现已完成，见 [SLUB章节](../parts/slub.md)。多核并发等未测限制仍保留，最新责任清单见 [最终待办](final-actions.md)。
