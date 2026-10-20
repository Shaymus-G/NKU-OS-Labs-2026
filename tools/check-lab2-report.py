#!/usr/bin/env python3
"""Check report regeneration, local attachments and delivery boundaries."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent.parent
subprocess.run(['python3', str(root / 'tools/assemble-lab2-report.py'), '--check'],
               cwd=root, check=True)
excluded = {'a-build.log', 'a-qemu-pagewalk-retry.log',
            'a-qemu-pagewalk.log', 'a-ucore-pagewalk.log'}
documents = [root / 'report.md']
for folder in ('docs/parts', 'docs/notes'):
    documents.extend(sorted((root / folder).glob('*.md')))
count = 0
for path in documents:
    fenced = False
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('```'):
            fenced = not fenced
        if fenced:
            continue
        for link in re.findall(r'\]\(([^)]+)\)', line):
            if '://' in link or link.startswith('#'):
                continue
            target = (path.parent / link.split('#')[0]).resolve()
            if target.name in excluded:
                raise SystemExit(f'Non-delivered attachment linked: {path}: {link}')
            if not target.is_relative_to(root) or not target.exists():
                raise SystemExit(f'Missing or external attachment: {path}: {link}')
            count += 1
report = (root / 'report.md').read_text(encoding='utf-8')
for token in ('31929', '31928', '0x80205000', '0x80206000',
              'b5c05e9', 'cc646e0', '6baf620', 'c3c4dd9', 'cf881a9',
              'a-paging-transition-confirm.log', 'a-qemu-tlb-hit-after-c.log',
              'b-integration-grade.log', '2412082', '2412559', '2412456'):
    if token not in report:
        raise SystemExit('Required evidence/version/limitation missing: ' + token)
subprocess.run(['git', 'diff', '--check'], cwd=root, check=True)
print(f'PASS: {len(documents)} documents, {count} local links, no non-delivered attachments linked')
print('PASS: old/new layouts, joint evidence and genuine owner-confirmation items retained')
