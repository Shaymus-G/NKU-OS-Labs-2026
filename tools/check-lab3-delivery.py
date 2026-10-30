#!/usr/bin/env python3
"""Check report sources, all delivered Markdown links and restored artifact."""
from pathlib import Path
import re, subprocess
root=Path(__file__).resolve().parents[1]
subprocess.run(['python3','tools/assemble-lab3-report.py','--check'],cwd=root,check=True)
count=0
for document in [root/'report.md',*root.glob('docs/**/*.md')]:
    for target in re.findall(r'!?\[[^\]]*\]\(([^\s)]+)\)',document.read_text()):
        if '://' in target or target.startswith('#'): continue
        assert (document.parent/target.split('#')[0]).is_file(),(document,target)
        count+=1
symbols=subprocess.check_output(['riscv64-unknown-elf-nm','bin/kernel'],cwd=root,text=True)
for name in ['c_user_check','c_vector_check','c_checks','c_probe_0']:
    assert not re.search(r'\b'+name+r'$',symbols,re.M),name
run=(root/'docs/logs/c-final-20261030/default-run.log').read_text()
assert run.count('100 ticks\n')==10 and 'memory management: default_pmm_manager' in run
assert 'Total Score: 100/100' in (root/'docs/logs/c-final-20261030/grade.log').read_text()
assert not subprocess.check_output(['git','diff','f475900','--','kern/trap','kern/mm','Makefile','kern/init/init.c','tools/grade.sh','tools/inject.py','tools/test_lab3.c','tools/test_lab3_context.S'],cwd=root)
print(f'PASS: {count} Markdown links; core unchanged; final grade 100/100; default artifact excludes probes')
