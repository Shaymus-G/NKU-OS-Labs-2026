#!/usr/bin/env python3
"""Assemble the edited academic chapters; keep development notes separate."""
import argparse
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true', help='check without writing report.md')
args = parser.parse_args()
sources = ['01-overview.md', '02-memory.md', '03-fit.md', '04-buddy.md',
           '05-slub.md', '06-translation.md', '07-validation.md', '08-analysis.md']
excluded = {'a-build.log', 'a-qemu-pagewalk-retry.log',
            'a-qemu-pagewalk.log', 'a-ucore-pagewalk.log'}
chapters = []
for name in sources:
    source = root / 'docs/report' / name
    def relocate(match):
        image, label, link = match.groups()
        if '://' in link or link.startswith('#'):
            return match[0]
        path, separator, anchor = link.partition('#')
        target = (source.parent / path).resolve()
        if target.name in excluded:
            raise ValueError(f'{name}: non-delivered attachment: {link}')
        if not target.is_relative_to(root) or not target.exists():
            raise FileNotFoundError(f'{name}: invalid attachment: {link}')
        destination = target.relative_to(root).as_posix() + separator + anchor
        return image + '[' + label + '](' + destination + ')'
    fenced = False
    lines = []
    for line in source.read_text(encoding='utf-8').splitlines():
        if line.startswith('```'):
            fenced = not fenced
        if not fenced:
            line = re.sub(r'(!?)\[([^\]]*)\]\(([^)]+)\)', relocate, line)
            if line.endswith('  '):
                line = line.rstrip() + '<br>'
        lines.append(line)
    if fenced:
        raise ValueError(f'{name}: unterminated code fence')
    chapters.append('\n'.join(lines).strip())
result = '\n\n'.join(chapters) + '\n'
output = root / 'report.md'
if args.check:
    if not output.exists() or output.read_text(encoding='utf-8') != result:
        parser.exit(1, 'report.md is stale; run tools/assemble-lab2-report.py\n')
    print(f'PASS: report.md matches {len(sources)} academic source chapters')
else:
    output.write_text(result, encoding='utf-8')
    print(f'Assembled {len(sources)} academic chapters into report.md')
