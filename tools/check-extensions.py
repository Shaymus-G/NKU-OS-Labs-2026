#!/usr/bin/env python3
"""Run extension kernels on real QEMU; keep each invocation's evidence separate."""
import argparse
from datetime import datetime, timezone, timedelta
import hashlib
import os
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parent.parent
os.chdir(ROOT)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--log-dir', type=Path)
parser.add_argument('--negative-tests', action='store_true',
                    help='run isolated expected-panic tests in both PMMs')
args = parser.parse_args()
stamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d-%H%M%S-%f')
logs = (args.log_dir or Path('docs/logs/extensions-runs') / stamp).resolve()
if not logs.is_relative_to(ROOT):
    parser.error('log directory must be inside the workspace')
logs.mkdir(parents=True, exist_ok=False)
print('Evidence: ' + str(logs.relative_to(ROOT)), flush=True)


def provenance():
    text = 'Extension verification: ' + datetime.now(timezone(timedelta(hours=8))).isoformat() + '\n'
    text += 'HEAD: ' + subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True)
    for path in sorted(list(Path('kern').rglob('*.c')) + list(Path('kern').rglob('*.h'))):
        text += hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + str(path) + '\n'
    return text


def build(name, defs):
    with (logs / (name + '-build.log')).open('w') as out:
        out.write(provenance())
        out.flush()
        for command in (['make', 'clean'], ['make', 'DEFS=' + defs]):
            out.write('$ ' + ' '.join(command) + '\n')
            out.flush()
            subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, check=True)


def boot(name, defs, manager, markers, memory=128, expected_panic=False):
    command = ['make', 'qemu', 'DEFS=' + defs,
               'QEMU=qemu-system-riscv64 -m ' + str(memory) + 'M']
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               start_new_session=True)
    try:
        output, _ = process.communicate(timeout=12)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        output, _ = process.communicate(timeout=5)
    text = output.decode(errors='replace')
    (logs / (name + '-run.log')).write_text(provenance() + '$ ' + ' '.join(command) +
        '\nHarness terminates the intentionally idle kernel after 12 seconds.\n' + text)
    assert ('kernel panic' in text) == expected_panic, name
    assert 'memory management: ' + manager in text, name
    for marker in markers:
        assert marker in text, (name, marker)
    print(name + ': PASS', flush=True)


markers = ['[SLUB] aligned cache, ctor, reuse, full/partial/empty, shrink: PASS',
           '[SLUB] kmalloc class boundaries, large blocks, content isolation: PASS',
           '[SLUB] 64 small objects share one physical page: PASS',
           '[SLUB] 1024 mixed objects, nonoverlap and byte-pattern oracle: PASS',
           '[SLUB] cache metadata exhaustion and recovery: PASS',
           '[SLUB] real page exhaustion, allocation failure, full recovery: PASS',
           '[SLUB] state restored:', 'check_alloc_page() succeeded!']
for label, defs, manager in [
    ('slub-best-fit', '-DLAB2_EXTENSIONS', 'best_fit_pmm_manager'),
    ('slub-buddy', '-DLAB2_EXTENSIONS -DPMM_BUDDY', 'buddy_pmm_manager'),
]:
    build(label, defs)
    boot(label, defs, manager, markers)
    if args.negative_tests:
        for case in (1, 2, 3):
            negative = label + '-negative-' + str(case)
            negative_defs = defs + ' -DSLUB_NEGATIVE_TEST=' + str(case)
            build(negative, negative_defs)
            boot(negative, negative_defs, manager,
                 ['[SLUB negative] case ' + str(case) + ': expect assertion panic',
                  'assertion failed:'], expected_panic=True)
# Restore the familiar default artifact, with no extension test enabled.
build('default-restored', '')
