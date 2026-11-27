#!/usr/bin/env python3
"""Run B's integrated checks on the real kernel, recording reviewable evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


def command(args, log, timeout=180):
    with log.open('w') as output:
        output.write('COMMAND: ' + ' '.join(map(str, args)) + '\n')
        output.flush()
        result = subprocess.run(args, cwd=ROOT, stdout=output,
                                stderr=subprocess.STDOUT, timeout=timeout)
        output.write('\nEXIT: ' + str(result.returncode) + '\n')
    print(log.name, 'exit', result.returncode, flush=True)
    if result.returncode:
        raise RuntimeError('See ' + str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log-dir', type=Path, required=True)
    parser.add_argument('--guest-only', action='store_true', help='debug the already built hello; preserve earlier runtime logs')
    parser.add_argument('--boundary-only', action='store_true', help='real U-mode invalid syscall and exec cases in temporary copies')
    args = parser.parse_args()
    logs = args.log_dir.resolve()
    logs.mkdir(parents=True, exist_ok=True)
    if args.boundary_only:
        boundary_checks(logs)
        return
    if args.guest_only:
        guest_roundtrip(logs)
        return
    manifest = {'time': time.strftime('%Y-%m-%d %H:%M:%S %Z'), 'root': str(ROOT),
                'commit': subprocess.check_output(['git', '-c', 'safe.directory=' + str(ROOT),
                    'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(), 'sha256': {}}
    for directory in ['kern', 'libs', 'user']:
        for path in sorted((ROOT / directory).rglob('*')):
            if path.is_file():
                manifest['sha256'][str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in ['riscv64-unknown-elf-gcc', 'riscv64-unknown-elf-gdb', 'qemu-system-riscv64', 'gdb']:
        manifest[name] = subprocess.check_output([name, '--version'], text=True).splitlines()[0]
    (logs / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    command(['make', 'clean'], logs / 'clean.log')
    command(['make', '-j4'], logs / 'build.log')
    # The standard kernel has an idle loop, so stop it after real init cleanup.
    from importlib.util import spec_from_file_location, module_from_spec
    spec = spec_from_file_location('c_checks', ROOT / 'tools/check-lab5-c.py')
    checks = module_from_spec(spec)
    spec.loader.exec_module(checks)
    text = checks.run_guest(ROOT, logs / 'hello-qemu.log')
    assert all(s in text for s in ['hello pass.', 'init check memory pass.'])
    assert 'kernel panic' not in text and 'user panic' not in text
    (logs / 'hello-stop.txt').write_text('Observer terminated QEMU after real init cleanup marker; kernel did not auto-shutdown.\n')
    print('real hello: PASS', flush=True)
    command(['python3', 'tools/check-lab5-c.py', '--log-dir', str(logs / 'suite')],
            logs / 'suite.log', 600)
    command(['make', 'grade'], logs / 'grade.log', 600)
    if 'Total Score: 130/130' not in (logs / 'grade.log').read_text():
        raise RuntimeError('Grade did not reach 130/130; see log')
    command(['make', '-j4'], logs / 'restored-build.log')
    guest_roundtrip(logs)
    print('Integrated runtime, suite, teacher grade and guest GDB: PASS', flush=True)


def guest_roundtrip(logs):
    guest_log = (logs / 'guest-qemu.log').open('w')
    qemu = subprocess.Popen(['qemu-system-riscv64', '-machine', 'virt', '-nographic',
        '-bios', 'default', '-device', 'loader,file=bin/ucore.img,addr=0x80200000',
        '-gdb', 'tcp:127.0.0.1:1234', '-S'], cwd=ROOT,
        stdout=guest_log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    try:
        time.sleep(0.5)
        command(['riscv64-unknown-elf-gdb', '-q', '-nx', '-batch', '-x',
                 'tools/lab5-guest-roundtrip.gdb'], logs / 'guest-gdb.log', 90)
        assert 'back in user code:' in (logs / 'guest-gdb.log').read_text()
    finally:
        qemu.terminate()
        try: qemu.wait(timeout=3)
        except subprocess.TimeoutExpired: qemu.kill(); qemu.wait()
        guest_log.close()
    print('Guest GDB: PASS', flush=True)


def boundary_checks(logs):
    from importlib.util import spec_from_file_location, module_from_spec
    spec = spec_from_file_location('c_checks', ROOT / 'tools/check-lab5-c.py')
    checks = module_from_spec(spec); spec.loader.exec_module(checks)
    with tempfile.TemporaryDirectory(prefix='lab5-b-boundary-') as work:
        for mode in ['cow', 'eager']:
            tree = Path(work) / mode
            tree.mkdir()
            for name in ['Makefile', 'kern', 'libs', 'tools', 'user']:
                source = ROOT / name
                if source.is_dir(): shutil.copytree(source, tree / name, ignore=shutil.ignore_patterns('__pycache__'))
                else: shutil.copy2(source, tree / name)
            shutil.copy2(ROOT / 'tools/b-lab5-boundary.c', tree / 'user/b_boundary.c')
            flags = '-DTEST=b_boundary -DTESTSTART=_binary_obj___user_b_boundary_out_start -DTESTSIZE=_binary_obj___user_b_boundary_out_size'
            if mode == 'eager': flags += ' -DLAB5_EAGER_FORK'
            with (logs / (mode + '-boundary-build.log')).open('w') as output:
                result = subprocess.run(['make', '-j4', 'DEFS=' + flags], cwd=tree,
                    stdout=output, stderr=subprocess.STDOUT, timeout=180)
            assert result.returncode == 0
            text = checks.run_guest(tree, logs / (mode + '-boundary.log'))
            for marker in ['invalid syscall numbers return to U mode PASS',
                           'invalid exec preserves old program PASS',
                           'user-buffer exec enters new ELF and exits 42 PASS', 'init check memory pass.']:
                assert marker in text, marker
            assert 'panic' not in text
            print(mode, 'real U-mode syscall/exec boundaries: PASS', flush=True)


if __name__ == '__main__':
    main()
