#!/usr/bin/env python3
"""Assemble existing authors' sections, fixing relative evidence links."""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
intro = '''# 操作系统 Lab2：物理内存管理与地址翻译

汇总日期：2026-10-19。成员：国峻赫（A）、陈铭宇（B，2412559）、尚文轩（C，2412456）。

## 范围、分工与版本说明

实验包含物理页初始化、First-Fit 分析、Best-Fit、Buddy、QEMU 双重 GDB 和报告材料。A 负责初始化与地址翻译；B 负责 First/Best-Fit 及基础测试；C 负责 Buddy、测试和本文汇总。教师框架、教师检查、组员实现与 AI 辅助范围见各章，不将教师代码记为本组从零实现。

本轮 C 以 lab2 的 a3574e2 为起点完成未提交修改，运行代码以日志 SHA256 标识。2026-10-19 在 C 的 WSL Ubuntu 22.04、GCC 10.2.0、QEMU 6.2.0、OpenSBI 1.3 上，Buddy 六组自检通过并恢复 31928 空闲页；默认 Best-Fit 行为回归通过，教师 Page Init/Best-Fit 共 20/20。评分采用本机控制台转接参数，见 C 章。此评分不包含 Buddy、双重 GDB 或报告。

A、B 下文章节按原作者阶段记录汇入，保留原始版本、命令及证据限定。历史章节引用但未随仓库交付的附件在汇总正文标注为缺失，不使用其他日志替代。A 的 First-Fit 旧版本为 31929 页，B/C 为 31928 页；不同构建地址不能混用。C 本轮没有重做 A 的双重 GDB，不将旧版调试地址改成新值。A 的分页前后完整实测对照和独立后续 TLB 命中仍待补证据，A 学号标记仍需本人核对，最终共同环境与全组通读验收尚未完成。Challenge SLUB 未实现。

个人章节是维护源，使用 `python3 tools/assemble-lab2-report.py` 重新汇总。当前源码在仓库根目录，与计划中的 labcodes/lab2 目录不同，复现请在根目录执行。本文尚未提交或推送。

## 快速复现与材料入口

```bash
source ~/OSopt/env.sh
python3 tools/check-buddy.py
# 单独手动启动 Buddy：
make clean
make DEFS=-DPMM_BUDDY
make qemu DEFS=-DPMM_BUDDY
# 退出 QEMU 后切回默认并评分：
make clean
make
make grade QEMU='python3 tools/qemu-grade-console.py'
```

切换编译宏必须 clean 后重建。A 旧章节的临时排除 best_fit_pmm.c 命令仅作历史记录，不适用于当前集成版本。本机已有 QEMU 包装器保持原样。

- [A 个人原稿](docs/parts/a-memory-debug.md)
- [B 算法原稿](docs/parts/b-first-best-fit.md)、[B 测试原稿](docs/parts/b-best-fit-tests.md)
- [C 个人原稿](docs/parts/c-buddy.md)、[C 交接清单](docs/notes/c-progress.md)
- [C 实现规格与提示词](docs/prompts/c-buddy.md)、[C 测试规格与提示词](docs/prompts/c-buddy-tests.md)
- [C Buddy 验证](docs/logs/c-buddy-run.log)、[C 默认回归](docs/logs/c-best-fit-run.log)、[C 教师评分](docs/logs/c-grade.log)

以下为各成员章节。所有“本次”“当前”等叙述依各章说明的日期和基线理解；历史待办不覆盖上文的本轮验证结论。

'''
parts = ['a-memory-debug.md', 'b-first-best-fit.md',
         'b-best-fit-tests.md', 'c-buddy.md']
chunks = [intro]
for name in parts:
    text = (root / 'docs/parts' / name).read_text(encoding='utf-8')
    # Source links are relative to docs/parts, output is at repository root.
    text = re.sub(r'\]\(\.\./\.\./', '](', text)
    text = re.sub(r'\]\(\.\./', '](docs/', text)
    def validate_link(match):
        image, label, link = match.groups()
        if '://' in link or link.startswith('#') or (root / link.split('#')[0]).exists():
            return match[0]
        source_relative = root / 'docs/parts' / link.split('#')[0]
        if source_relative.exists():
            return image + '[' + label + '](' + source_relative.relative_to(root).as_posix() + ')'
        print('Missing historical attachment: ' + link)
        return label + '（缺失附件：`' + link + '`）'
    text = re.sub(r'(!?)\[([^\]]*)\]\(([^)]+)\)', validate_link, text)
    fenced = False
    lines = []
    for line in text.splitlines():
        if line.startswith('```'):
            fenced = not fenced
        if not fenced and re.match(r'^#{1,5} ', line):
            line = '#' + line
        lines.append(line)
    chunks.append('\n'.join(lines) + '\n\n')
(root / 'report.md').write_text(''.join(chunks), encoding='utf-8')
print('report.md assembled from four authored sections')
