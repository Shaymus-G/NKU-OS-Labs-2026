#!/usr/bin/env python3
"""Generate the academic report and verify every linked local attachment."""
from pathlib import Path
import argparse, re
root=Path(__file__).resolve().parents[1]
source=root/'docs/report/lab3.md'
parser=argparse.ArgumentParser()
parser.add_argument('--check',action='store_true')
args=parser.parse_args()
pattern=re.compile(r'(!?\[[^\]]*\]\()([^\s)]+)(\))')
def link(match):
    target=match[2]
    if '://' in target or target.startswith('#'): return match[0]
    file=(source.parent/target.split('#')[0]).resolve()
    assert file.is_file(),file
    return match[1]+file.relative_to(root).as_posix()+match[3]
text=pattern.sub(link,source.read_text())
report=root/'report.md'
if args.check:
    assert report.read_text()==text,'report.md stale: regenerate from docs/report/lab3.md'
else:
    report.write_text(text)
print('PASS: report source consistent; all report attachments exist')
