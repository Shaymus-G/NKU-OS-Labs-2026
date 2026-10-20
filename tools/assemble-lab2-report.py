#!/usr/bin/env python3
"""Assemble existing authors' sections, fixing relative evidence links."""
import argparse
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true',
                    help='check report.md matches its sources without writing')
args = parser.parse_args()
not_delivered = {
    'a-build.log', 'a-qemu-pagewalk-retry.log',
    'a-qemu-pagewalk.log', 'a-ucore-pagewalk.log',
}
intro = '''# 操作系统 Lab2：物理内存管理与地址翻译

汇总日期：2026-10-20。成员：国峻赫（A，学号待本人确认）、陈铭宇（B，2412559）、尚文轩（C，2412456）。

## 范围、分工与版本说明

实验包含物理页初始化、First-Fit 分析、Best-Fit、Buddy、QEMU 双重 GDB 和报告材料。A 负责初始化与地址翻译；B 负责 First/Best-Fit 及基础测试；C 负责 Buddy、测试和本文汇总。教师框架、教师检查、组员实现与 AI 辅助范围见各章，不将教师代码记为本组从零实现。

C 的 Buddy 成果已提交为 b5c05e9，并包含在远程 lab2。A 的 cc646e0 完成联合回归，6baf620 补足分页切换和软件 TLB 直接命中；B 的 c3c4dd9、cf881a9 完成独立联合验证和交接。本次汇总以已同步的 cf881a9 及其后的本地文档提交为基础，源码及运行证据按各日志记录的 HEAD、SHA256 关联。

| 验证阶段 | 环境 | 实际结果 |
|---|---|---|
| C 初次实现 | Ubuntu 22.04 / GCC 10.2.0 / QEMU 6.2.0 / OpenSBI 1.3 | Buddy 六组自检、1024 次混合操作和 Best-Fit 行为通过，恢复 31928 页；本机控制台转接后评分 20/20 |
| A 联合回归 | Ubuntu 20.04 / GCC 10.2.0 / QEMU 4.1.1 / OpenSBI 0.4 | Buddy、Best-Fit 通过，普通 make grade 为 20/20；分页前后对照和后续取指查询直接命中 TLB 已补录 |
| B 独立联合验收 | Ubuntu 24.04.5 / GCC 10.2.0 / QEMU 4.1.1 / OpenSBI 0.4 | 已复核 Buddy 契约，切换后两种模式通过，普通 make grade 为 20/20，最后恢复默认构建 |

教师 20/20 只覆盖 Page Init 和 Best-Fit；Buddy 由独立自检支持，地址翻译由 A 调试记录支持。A 的直接命中记录证明后续取指地址查询跳过 tlb_fill()，不能表述为“CPU 第二次执行 kern_init”。C 本轮核对并汇总这些已交付证据，没有声称在本机重新完成 A 的调试。

各章保留历史版本和证据限定：First-Fit 旧版为 31929 页、根页表 PA 0x80205000；联合版为 31928 页、根页表 PA 0x80206000。不同构建地址不能混用。a-build.log、a-qemu-pagewalk-retry.log、a-qemu-pagewalk.log、a-ucore-pagewalk.log 均为本地历史记录，未随本次提交交付，不提供下载链接；成功页表遍历引用已交付的 a-qemu-pagewalk-body.log。

剩余人工核对：A 的 page_init() 学号标记仍为 YOUR CODE，初始化实际 AI 交互记录仍有占位内容，需要本人确认和补齐；全组三人通读及最终提交版本签认尚待进行。Challenge SLUB 未实现，多核等未测边界见各章。这些真实限制不因更新进度文字而删除。

个人章节是维护源，使用 `python3 tools/assemble-lab2-report.py` 重新汇总，或加 `--check` 只核对一致性。当前源码在仓库根目录，与计划中的 labcodes/lab2 目录不同。C 本次按子任务在本地 commit，push 由用户执行；这不改变 Buddy 成果已在远程的事实。各章中的初次开发、失败日志和带日期的交接过程按当时状态理解。

## 快速复现与材料入口

```bash
# 先按各自环境激活工具链；C 本机执行 source ~/OSopt/env.sh
# 手动启动 Buddy：
make clean
make DEFS=-DPMM_BUDDY
make qemu DEFS=-DPMM_BUDDY
# 退出 QEMU 后切回默认：
make clean
make
make qemu
# 再退出 QEMU 后，A/B 环境普通评分：
make grade
# C 本机 QEMU 6.2.0 / OpenSBI 1.3 使用：
make grade QEMU='python3 tools/qemu-grade-console.py'
```

切换编译宏必须 clean 后重建并重启。两条评分命令按环境选择其一。A 旧章节的临时排除 best_fit_pmm.c 命令仅作历史记录，不适用于当前集成版本。本机已有 QEMU 包装器保持原样。

- [A 个人原稿](docs/parts/a-memory-debug.md)
- [B 算法原稿](docs/parts/b-first-best-fit.md)、[B 测试原稿](docs/parts/b-best-fit-tests.md)
- [C 个人原稿](docs/parts/c-buddy.md)、[C 交接清单](docs/notes/c-progress.md)
- [C 实现规格与提示词](docs/prompts/c-buddy.md)、[C 测试规格与提示词](docs/prompts/c-buddy-tests.md)
- [C Buddy 验证](docs/logs/c-buddy-run.log)、[C 默认回归](docs/logs/c-best-fit-run.log)、[C 教师评分](docs/logs/c-grade.log)
- [A 联合回归](docs/logs/a-after-c-grade.log)、[A 分页切换补录](docs/logs/a-paging-transition-confirm.log)、[A TLB 直接命中](docs/logs/a-qemu-tlb-hit-after-c.log)
- [B 联合环境与摘要](docs/logs/b-integration-environment.log)、[B Buddy](docs/logs/b-integration-buddy-run.log)、[B Best-Fit](docs/logs/b-integration-best-fit-run.log)、[B 普通评分](docs/logs/b-integration-grade.log)

以下为各成员章节。所有“本次”“当前”等叙述依各章说明的日期和基线理解；历史待办不覆盖上文的本轮验证结论。

'''
parts = ['a-memory-debug.md', 'b-first-best-fit.md',
         'b-best-fit-tests.md', 'c-buddy.md']
chunks = [intro]
for name in parts:
    text = (root / 'docs/parts' / name).read_text(encoding='utf-8')
    def validate_link(match):
        image, label, link = match.groups()
        if '://' in link or link.startswith('#'):
            return match[0]
        path, separator, anchor = link.partition('#')
        if Path(path).name in not_delivered:
            return label + '（本地历史记录，未随本次提交交付：`' + Path(path).name + '`）'
        target = (root / 'docs/parts' / path).resolve()
        if not target.is_relative_to(root) or not target.exists():
            raise FileNotFoundError(f'{name}: invalid or missing attachment: {link}')
        destination = target.relative_to(root).as_posix() + separator + anchor
        return image + '[' + label + '](' + destination + ')'
    fenced = False
    lines = []
    for line in text.splitlines():
        if line.startswith('```'):
            fenced = not fenced
        if not fenced:
            line = re.sub(r'(!?)\[([^\]]*)\]\(([^)]+)\)', validate_link, line)
            if re.match(r'^#{1,5} ', line):
                line = '#' + line
            if line.endswith('  '):
                line = line.rstrip() + '<br>'
        lines.append(line)
    chunks.append('\n'.join(lines) + '\n\n')
result = ''.join(chunks)
output = root / 'report.md'
if args.check:
    if not output.exists() or output.read_text(encoding='utf-8') != result:
        parser.exit(1, 'report.md is stale; run tools/assemble-lab2-report.py\n')
    print('report.md matches all four authored sections and introduction')
else:
    output.write_text(result, encoding='utf-8')
    print('report.md assembled from four authored sections')
