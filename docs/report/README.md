# 正式报告编辑入口

`report.md` 由本目录的八份正文生成。本文只记录编辑方法，不进入报告。

| 文件 | 内容 | 事实复核 |
|---|---|---|
| 01-overview.md | 目标、环境和分工 | 全组 |
| 02-memory.md | 初始化与 DTB 扩展 | A、C |
| 03-fit.md | First-Fit、Best-Fit | B |
| 04-buddy.md | Buddy 与验证 | C，B复核 |
| 05-slub.md | 对象缓存扩展 | C，B复核 |
| 06-translation.md | 页表、TLB 与自动演示 | A、C |
| 07-validation.md | 综合测试与复现 | B、C |
| 08-analysis.md | 理论分析、范围和 AI 过程 | 全组 |

原始个人材料保留在 `docs/parts/`，交接事项在 `docs/notes/`，均不再自动拼入正式正文。若个人原稿增加新结果，由相应作者核对后同步到上述正文源；不能只改生成的根目录文件。

```bash
python3 tools/assemble-lab2-report.py
python3 tools/check-lab2-report.py
```

提交前重点核对数值与对应日志、引用有效性、实际完成状态，以及规格和原始交互的来源。
