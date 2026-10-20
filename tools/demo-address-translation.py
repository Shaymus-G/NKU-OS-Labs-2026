#!/usr/bin/env python3
"""Rebuild a default kernel; record real Bare/Sv39 single steps and PTE reads."""
import argparse
from datetime import datetime, timezone, timedelta
import hashlib
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import time

root = Path(__file__).resolve().parent.parent
os.chdir(root)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
stamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d-%H%M%S-%f')
directory = (args.output_dir or Path('docs/logs/address-demo') / stamp).resolve()
if not directory.is_relative_to(root):
    parser.error('output directory must be inside workspace')
directory.mkdir(parents=True, exist_ok=False)
with (directory / 'build.log').open('w') as out:
    subprocess.run(['make', 'clean'], stdout=out, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['make'], stdout=out, stderr=subprocess.STDOUT, check=True)
symbols = subprocess.check_output(['riscv64-unknown-elf-nm', 'bin/kernel'], text=True)
entry = int(re.search(r'^([0-9a-f]+) \w kern_entry$', symbols, re.M)[1], 16)
relocation = entry - 0x80200000
disassembly = subprocess.check_output(
    ['riscv64-unknown-elf-objdump', '-d', '--disassemble=kern_entry', 'bin/kernel'], text=True)
instructions = re.findall(r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+(.+)$', disassembly, re.M)
position = next(i for i, (_, instruction) in enumerate(instructions)
                if 'csrw' in instruction and 'satp' in instruction)
assert position > 0
previous = int(instructions[position - 1][0], 16) - relocation
(directory / 'entry-disassembly.log').write_text(disassembly)
with socket.socket() as socket_probe:
    socket_probe.bind(('127.0.0.1', 0))
    port = socket_probe.getsockname()[1]
env = dict(os.environ, LAB2_DEMO_DIR=str(directory), LAB2_DEMO_PORT=str(port),
           LAB2_PRE_SATP_PA=hex(previous))
command = ['qemu-system-riscv64', '-machine', 'virt', '-nographic', '-bios', 'default',
           '-device', 'loader,file=bin/ucore.img,addr=0x80200000',
           '-gdb', 'tcp:127.0.0.1:' + str(port), '-S']
with (directory / 'qemu.log').open('wb') as out:
    qemu = subprocess.Popen(command, stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        time.sleep(0.5)
        gdb = subprocess.run(['riscv64-unknown-elf-gdb', '-nx', '-batch', 'bin/kernel',
            '-ex', 'source tools/gdb_demo_driver.py'], env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (directory / 'gdb.log').write_bytes(gdb.stdout)
        assert gdb.returncode == 0 and b'LAB2 LIVE DEMO: PASS' in gdb.stdout, gdb.stdout.decode(errors='replace')
    finally:
        if qemu.poll() is None:
            os.killpg(qemu.pid, signal.SIGTERM)
        qemu.wait(timeout=5)
provenance = 'HEAD: ' + subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True)
for path in ['bin/kernel', 'tools/sv39.py', 'tools/gdb_lab2.py', 'tools/gdb_demo_driver.py',
             'tools/demo-address-translation.py']:
    provenance += hashlib.sha256(Path(path).read_bytes()).hexdigest() + '  ' + path + '\n'
(directory / 'provenance.log').write_text(provenance)
subprocess.run([sys.executable, 'tools/render-address-demo.py', str(directory / 'trace.json')], check=True)
print('Live GDB demo: PASS; evidence in ' + str(directory.relative_to(root)))
