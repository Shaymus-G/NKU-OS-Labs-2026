#!/usr/bin/env python3
"""Build B's tests in a fresh /tmp copy. Run in WSL with the lab tool PATH.

Never changes production startup, C's implementations, or teacher tests.
"""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
stamp = time.strftime('%Y%m%d-%H%M%S')
logs = root / 'docs' / 'logs' / ('b-isolated-' + stamp)
logs.mkdir(parents=True)
tree = Path(tempfile.mkdtemp(prefix='lab5-b-isolated-'))
for entry in root.iterdir():
    if entry.name in {'.git', 'obj', 'bin', 'docs', '.vscode'}:
        continue
    target = tree / entry.name
    if entry.is_dir(): shutil.copytree(entry, target)
    else: shutil.copy2(entry, target)

manifest = {'temporary_tree': str(tree), 'source_root': str(root), 'time': stamp,
            'scope': 'RV64 QEMU kernel-side tests with explicit C interface doubles',
            'sha256': {}}
for name in ['kern/process/proc.c', 'kern/mm/pmm.c', 'tools/b-isolated/harness.c',
             'tools/b-isolated/run.py']:
    manifest['sha256'][name] = hashlib.sha256((root / name).read_bytes()).hexdigest()
for label, args in [('gcc', ['riscv64-unknown-elf-gcc', '--version']),
                    ('qemu', ['qemu-system-riscv64', '--version'])]:
    manifest[label] = subprocess.check_output(args, text=True).splitlines()[0]
manifest['base_commit'] = subprocess.check_output(
    ['git', '-c', 'safe.directory=' + str(root), 'rev-parse', 'HEAD'], cwd=root, text=True).strip()

proc = tree / 'kern/process/proc.c'
# Keep the kernel-side doubles usable after C's implementations land. Rename
# definitions only, so the loader and wait still call the harness doubles.
text = proc.read_text()
for name in ('do_fork', 'do_exit', 'do_yield', 'do_kill', 'proc_run'):
    text = re.sub(r'^' + name + r'(?=\()', 'b_unused_' + name, text, flags=re.M)
proc.write_text(text + '\n' + (root / 'tools/b-isolated/harness.c').read_text())
sched = tree / 'kern/schedule/sched.c'
sched.write_text(sched.read_text().replace('void schedule(', 'void b_unused_schedule('))
switch = tree / 'kern/process/switch.S'
switch.write_text(switch.read_text().replace('switch_to', 'b_unused_switch_to'))
init = tree / 'kern/init/init.c'
text = init.read_text().replace('    proc_init(); // init process table',
    '    extern void b_isolated_tests(void);\n    b_isolated_tests();\n    for (;;) ;')
init.write_text(text)
makefile = tree / 'Makefile'
makefile.write_text(makefile.read_text().replace('LDFLAGS\t+= -nostdlib --gc-sections',
    'LDFLAGS\t+= -nostdlib --gc-sections --wrap=kmalloc'))
# ld --wrap cannot interpose calls resolved inside pmm.c itself. Rename only
# its definition in the copy, so ALL page allocation calls reach our counter.
pmm = tree / 'kern/mm/pmm.c'
text, count = re.subn(r'\balloc_pages(?=\(size_t n\)\s*\{)',
                      'b_real_alloc_pages', pmm.read_text())
assert count == 1
pmm.write_text(text)
env = os.environ.copy()
env.pop('QEMU', None)
commands = [['make', '-j2'], ['qemu-system-riscv64', '-machine', 'virt', '-nographic',
    '-bios', 'default', '-device', 'loader,file=bin/ucore.img,addr=0x80200000']]
manifest['commands'] = commands
try:
    for label, command in zip(['build', 'qemu'], commands):
        with (logs / (label + '.log')).open('w') as output:
            result = subprocess.run(command, cwd=tree, env=env, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=180 if label == 'build' else 90)
        manifest[label + '_exit'] = result.returncode
        print(label, 'exit', result.returncode, flush=True)
        if result.returncode != 0: raise RuntimeError(label + ' failed; inspect ' + str(logs))
    output = (logs / 'qemu.log').read_text()
    if '[B isolated] ALL CHECKS PASS' not in output or 'kernel panic' in output:
        raise RuntimeError('Missing success marker or kernel panic')
    print(output[output.find('[B isolated]'):], flush=True)
finally:
    (logs / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Evidence:', logs, '\nTemporary tree:', tree, flush=True)
