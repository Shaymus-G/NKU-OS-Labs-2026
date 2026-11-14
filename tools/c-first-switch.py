#!/usr/bin/env python3
"""Observe real first switch and synthesized trap return using current ELF."""
from pathlib import Path
import socket, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
with socket.socket() as sock:
    sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
commands=f'''set pagination off
set remotetimeout 10
target remote localhost:{port}
hbreak *switch_to
continue
python assert int(gdb.parse_and_eval('current->pid')) == 1
info registers a0 a1 sp satp
x/14gx $a1
delete breakpoints
hbreak *forkret
continue
python assert int(gdb.parse_and_eval('current->pid')) == 1
p *current->tf
delete breakpoints
hbreak *kernel_thread_entry
continue
python assert int(gdb.parse_and_eval('$sp')) == int(gdb.parse_and_eval('current->tf'))
info registers pc sp s0 s1 satp sstatus
x/s $s1
delete breakpoints
hbreak *init_main
continue
python assert gdb.parse_and_eval('(char *)$a0').string() == 'Hello world!!'
echo C FIRST SWITCH GDB: PASS\n
detach
quit
'''
(root/'docs/logs/c-first-switch-commands.txt').write_text(commands)
with tempfile.NamedTemporaryFile(mode='w') as script:
    script.write(commands); script.flush()
    with (root/'docs/logs/c-first-switch-qemu.log').open('wb') as out:
        qemu=subprocess.Popen(['qemu-system-riscv64','-machine','virt','-nographic','-bios','default','-device','loader,file=bin/ucore.img,addr=0x80200000','-S','-gdb',f'tcp:127.0.0.1:{port}'],cwd=root,stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT)
        try:
            p=subprocess.run(['riscv64-unknown-elf-gdb','-nx','-batch','bin/kernel','-x',script.name],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            (root/'docs/logs/c-first-switch-gdb.log').write_bytes(p.stdout)
            assert p.returncode==0 and b'C FIRST SWITCH GDB: PASS' in p.stdout,p.stdout[-2000:]
        finally:
            if qemu.poll() is None: qemu.terminate()
            qemu.wait(timeout=5)
print('First switch GDB: PASS')
