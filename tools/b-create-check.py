#!/usr/bin/env python3
"""WSL/Linux B creation checks. All injection occurs in a fresh /tmp copy.

Normal-build failure is recorded while C placeholders remain. Never treat the
isolated ELF or its test stubs as a completed scheduling implementation.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run_log(command, log, cwd, timeout=90):
    with log.open('w', encoding='utf-8') as output:
        result = subprocess.run(command, cwd=cwd, stdin=subprocess.DEVNULL,
                                stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
    return result.returncode

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Missing or ambiguous test injection anchor: ' + old)
    return text.replace(old, new, 1)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    timestamp = datetime.datetime.now().astimezone().isoformat()
    out = (args.output or ROOT / 'docs/logs' /
           ('b-create-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
    out.mkdir(parents=True, exist_ok=False)
    metadata = {'started_at': timestamp, 'scope': 'B creation only; C is not validated',
                'passed': False, 'normal_build_passed': False, 'gdb_passed': False}
    watched = ['kern/process/proc.c', 'kern/init/init.c', 'kern/trap/trap.c',
               'kern/trap/trapentry.S', 'kern/process/switch.S', 'kern/schedule/sched.c',
               'libs/riscv.h', 'Makefile', 'tools/test_lab4.c', 'tools/test_lab4.h',
               'tools/inject.py', 'tools/grade.sh', 'tools/b-create-check.py',
               'tools/b-create-tests.inc', 'tools/b-create-stubs.c']
    metadata['source_sha256'] = {name: sha(ROOT / name) for name in watched}
    metadata['base_commit'] = subprocess.check_output(
        ['git', '-c', 'safe.directory=' + str(ROOT), 'rev-parse', 'HEAD'], cwd=ROOT,
        text=True).strip()
    for tool in ['riscv64-unknown-elf-gcc', 'riscv64-unknown-elf-gdb', 'qemu-system-riscv64']:
        metadata[tool] = subprocess.check_output([tool, '--version'], text=True).splitlines()[0]
    qemu = None
    try:
        print('Evidence:', out, flush=True)
        run_log(['make', 'clean'], out / 'normal-clean.log', ROOT)
        metadata['normal_build_exit'] = run_log(['make', '-j2'], out / 'normal-build.log', ROOT)
        metadata['normal_build_passed'] = metadata['normal_build_exit'] == 0
        with tempfile.TemporaryDirectory(prefix='lab4-b-create-') as temp:
            tree = Path(temp) / 'source'
            shutil.copytree(ROOT, tree, ignore=shutil.ignore_patterns('.git', 'obj', 'bin',
                                                                    'docs', '__pycache__'))
            proc = tree / 'kern/process/proc.c'
            text = proc.read_text(encoding='utf-8')
            prototypes = r"""
/* B test wrappers: temporary-copy injection only. */
static void *b_test_kmalloc(size_t);
static void b_test_kfree(void *);
static struct Page *b_test_stack_alloc(size_t);
static void b_test_stack_free(struct Page *, size_t);
static int b_test_copy_mm(uint32_t, struct proc_struct *);
"""
            text = replace_once(text, '#include <assert.h>', '#include <assert.h>\n' + prototypes)
            text = replace_once(text, 'kmalloc(sizeof(struct proc_struct))',
                                'b_test_kmalloc(sizeof(struct proc_struct))')
            text = replace_once(text, 'alloc_pages(KSTACKPAGE)', 'b_test_stack_alloc(KSTACKPAGE)')
            text = replace_once(text, 'free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE)',
                                'b_test_stack_free(kva2page((void *)(proc->kstack)), KSTACKPAGE)')
            text = replace_once(text, 'kfree(proc);', 'b_test_kfree(proc);')
            text = replace_once(text, 'ret = copy_mm(clone_flags, proc)',
                                'ret = b_test_copy_mm(clone_flags, proc)')
            text += '\n#include "b-create-tests.inc"\n'
            proc.write_text(text, encoding='utf-8')
            for name in ['b-create-tests.inc', 'b-create-stubs.c', 'test_lab4.c', 'test_lab4.h']:
                shutil.copyfile(tree / 'tools' / name, tree / 'kern/process' / name)
            init = tree / 'kern/init/init.c'
            text = init.read_text(encoding='utf-8')
            text = replace_once(text, 'int kern_init(void) __attribute__((noreturn));',
                                'void b_create_checkpoint(void);\nvoid b_create_tests(void);\n'
                                'int kern_init(void) __attribute__((noreturn));')
            first = text.index('    clock_init();')
            last = text.index('    cpu_idle();', first)
            last = text.index('\n', last)
            text = text[:first] + ('    b_create_checkpoint();\n    b_create_tests();\n'
                                   '    while (1) {}\n') + text[last:]
            init.write_text(text, encoding='utf-8')
            # Keep an exact patch and hashes describing what the isolated build changes.
            import difflib
            patch = ''
            for name in ['kern/process/proc.c', 'kern/init/init.c']:
                patch += ''.join(difflib.unified_diff(
                    (ROOT / name).read_text(encoding='utf-8').splitlines(True),
                    (tree / name).read_text(encoding='utf-8').splitlines(True),
                    fromfile='formal/' + name, tofile='isolated/' + name))
            (out / 'isolation.patch').write_text(patch, encoding='utf-8')
            metadata['teacher_test_copy_sha256'] = sha(tree / 'kern/process/test_lab4.c')
            assert metadata['teacher_test_copy_sha256'] == metadata['source_sha256']['tools/test_lab4.c']
            build = run_log(['make', '-j2'], out / 'isolated-build.log', tree)
            metadata['isolated_build_exit'] = build
            if build != 0:
                raise RuntimeError('Isolated build failed; inspect isolated-build.log')
            shutil.copyfile(tree / 'bin/kernel', out / 'isolated-kernel.elf')
            metadata['isolated_kernel_sha256'] = sha(out / 'isolated-kernel.elf')
            # Bind only loopback; use a free ephemeral port for this QEMU instance.
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                port = sock.getsockname()[1]
            qemu_command = ['qemu-system-riscv64', '-machine', 'virt', '-nographic',
                            '-bios', 'default', '-device', 'loader,file=bin/ucore.img,addr=0x80200000',
                            '-S', '-gdb', f'tcp:127.0.0.1:{port}']
            metadata['qemu_command'] = qemu_command
            gdb_commands = f"""set pagination off
set confirm off
set architecture riscv:rv64
set remotetimeout 15
file {out / 'isolated-kernel.elf'}
target remote 127.0.0.1:{port}
hbreak b_create_checkpoint
continue
printf "B_GDB_PROC_INIT_COMPLETE\\n"
p current
p idleproc
p initproc
p nr_process
p sizeof(struct proc_struct)
p sizeof(struct context)
p sizeof(struct trapframe)
p *idleproc
p *initproc
p/x boot_pgdir_pa
p/x initproc->pgdir
p/x initproc->kstack
p/x initproc->context
p/x *initproc->tf
p/x &forkret
p/x &kernel_thread_entry
x/36gx initproc->tf
if current != idleproc || nr_process != 2
  quit 1
end
if idleproc->pid != 0 || initproc->pid != 1 || initproc->parent != idleproc
  quit 1
end
if initproc->context.ra != (unsigned long)&forkret || initproc->context.sp != (unsigned long)initproc->tf
  quit 1
end
if initproc->tf->epc != (unsigned long)&kernel_thread_entry || initproc->pgdir != boot_pgdir_pa
  quit 1
end
if (initproc->tf->status & 0x122) != 0x120
  quit 1
end
printf "B_GDB_LAYOUT_CHECK_PASS\\n"
delete breakpoints
detach
quit
"""
            (out / 'gdb-commands.txt').write_text(gdb_commands, encoding='utf-8')
            with (out / 'isolated-qemu.log').open('w', encoding='utf-8') as qlog:
                qemu = subprocess.Popen(qemu_command, cwd=tree, stdin=subprocess.DEVNULL,
                                        stdout=qlog, stderr=subprocess.STDOUT)
                # GDB's remote connection has retry support; QEMU starts before this process.
                gdb_exit = run_log(['riscv64-unknown-elf-gdb', '--batch', '-x',
                                    str(out / 'gdb-commands.txt')], out / 'gdb-session.log', tree, 60)
                metadata['gdb_exit'] = gdb_exit
                metadata['gdb_passed'] = (gdb_exit == 0 and 'B_GDB_LAYOUT_CHECK_PASS' in
                                          (out / 'gdb-session.log').read_text())
                if not metadata['gdb_passed']:
                    raise RuntimeError('GDB checkpoint failed; inspect gdb-session.log')
                metadata['qemu_exit'] = qemu.wait(timeout=60)
                qemu = None
            log = (out / 'isolated-qemu.log').read_text()
            metadata['checks'] = [line for line in log.splitlines() if line.startswith('[B Create]')]
            metadata['passed'] = (metadata['qemu_exit'] == 0 and
                                  '[Test Process Create] PASS' in log and
                                  '[B Create] ALL PASS' in log and 'panic' not in log.lower())
            if not metadata['passed']:
                raise RuntimeError('QEMU tests failed; inspect isolated-qemu.log')
    finally:
        if qemu is not None:
            qemu.terminate()
            try:
                qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait()
        metadata['formal_sources_unchanged_during_test'] = all(
            sha(ROOT / name) == digest for name, digest in metadata['source_sha256'].items())
        metadata['finished_at'] = datetime.datetime.now().astimezone().isoformat()
        (out / 'result.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n',
                                       encoding='utf-8')
    assert metadata['formal_sources_unchanged_during_test']
    print('PASS: independent creation checks and GDB checkpoint; no scheduling claim.', flush=True)
    print('Normal build:', 'PASS' if metadata['normal_build_passed'] else
          'blocked by remaining C placeholders; see normal-build.log', flush=True)

if __name__ == '__main__':
    main()
