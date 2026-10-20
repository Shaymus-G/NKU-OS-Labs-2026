#!/usr/bin/env python3
"""Run in the repository root after source ~/OSopt/env.sh.

Rebuild each manager from scratch, validate real QEMU output, run the
teacher grade unchanged, and leave a normal Best-Fit build behind.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import os
from pathlib import Path
import re
import signal
import subprocess


ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--log-dir', type=Path,
                    help='new workspace directory; existing directories are refused')
args = parser.parse_args()
stamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d-%H%M%S-%f')
LOGS = (args.log_dir or Path('docs/logs/c-runs') / stamp).resolve()
if not LOGS.is_relative_to(ROOT):
    parser.error('--log-dir must stay within the repository workspace')
LOGS.mkdir(parents=True, exist_ok=False)
print('New verification logs: ' + str(LOGS.relative_to(ROOT)), flush=True)


def provenance():
    text = 'C verification: ' + datetime.now(timezone(timedelta(hours=8))).isoformat() + '\n'
    text += 'HEAD: ' + subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True)
    status = subprocess.check_output(
        ['git', 'status', '--porcelain', '--untracked-files=no'], text=True)
    text += 'Tracked worktree status: ' + ('clean\n' if not status else '\n' + status)
    for name in ('kern/mm/buddy_pmm.c', 'kern/mm/buddy_pmm.h', 'kern/mm/pmm.c',
                 'kern/mm/best_fit_pmm.c', 'kern/mm/best_fit_test.c', 'Makefile',
                 'tools/check-buddy.py', 'tools/qemu-grade-console.py'):
        text += hashlib.sha256(Path(name).read_bytes()).hexdigest() + '  ' + name + '\n'
    return text


def build(name, defs):
    with (LOGS / name).open('w') as out:
        out.write(provenance())
        out.flush()
        for args in (['make', 'clean'], ['make'] + defs):
            out.write('$ ' + ' '.join(args) + '\n')
            out.flush()
            subprocess.run(args, stdout=out, stderr=subprocess.STDOUT, check=True)


def boot(name, defs, manager, expected):
    args = ['make', 'qemu'] + defs
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               start_new_session=True)
    try:
        output, _ = process.communicate(timeout=8)
    except subprocess.TimeoutExpired:
        # kern_init intentionally loops forever. Stop only our QEMU process group.
        os.killpg(process.pid, signal.SIGTERM)
        output, _ = process.communicate(timeout=5)
    text = output.decode(errors='replace')
    (LOGS / name).write_text(provenance() + '$ ' + ' '.join(args) + '\n' +
                            'Harness ends the idle kernel after 8 seconds.\n' + text)
    assert 'kernel panic' not in text, name
    assert 'memory management: ' + manager in text, name
    assert 'check_alloc_page() succeeded!' in text, name
    for marker in expected:
        assert marker in text, (name, marker)
    if manager == 'buddy_pmm_manager':
        match = re.search(r'state restored: (\d+) -> (\d+) free pages: PASS', text)
        assert match and match[1] == match[2], name
    print(name + ': PASS', flush=True)


build('c-buddy-build.log', ['DEFS=-DPMM_BUDDY'])
boot('c-buddy-run.log', ['DEFS=-DPMM_BUDDY'], 'buddy_pmm_manager', [
    'split six levels and merge back: PASS',
    'powers of two and rounded requests (3 -> 4): PASS',
    'non-buddy neighbours, fragmentation, exhaustion: PASS',
    'unaligned 37-page init, multiple ranges, no lost pages: PASS',
    '1024 mixed operations with occupancy oracle: PASS',
])
build('c-best-fit-build.log', [])
boot('c-best-fit-run.log', [], 'best_fit_pmm_manager', [
    '[Best-Fit behavior] ALL PASS',
])
with (LOGS / 'c-grade.log').open('w') as out:
    args = ['make', 'grade', 'QEMU=python3 tools/qemu-grade-console.py']
    out.write(provenance() + '$ ' + ' '.join(args) + '\n')
    out.flush()
    subprocess.run(args, stdout=out, stderr=subprocess.STDOUT, check=True)
assert 'Total Score: 20/20' in (LOGS / 'c-grade.log').read_text()
print('c-grade.log: 20/20', flush=True)
# grade cleans the root build. Restore the normal default artifact.
subprocess.run(['make'], stdout=subprocess.DEVNULL, check=True)
