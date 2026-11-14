#!/usr/bin/env python3
"""Run unmodified grading with a local PATH console adapter for OpenSBI 1.3."""
from pathlib import Path
import os, shutil, subprocess, tempfile
real_gdb=shutil.which('riscv64-unknown-elf-gdb')
root=Path(__file__).resolve().parents[1]
real=shutil.which('qemu-system-riscv64')
assert real
with tempfile.TemporaryDirectory(prefix='lab3-grade-console-') as tmp:
    wrapper=Path(tmp)/'qemu-system-riscv64'
    wrapper.write_text('''#!/usr/bin/env python3
import os, subprocess, sys
args=sys.argv[1:]
output=None
if '-serial' in args:
    i=args.index('-serial')
    if args[i+1].startswith('file:'):
        output=args[i+1][5:]
        del args[i:i+2]
if output:
    with open(output,'wb') as f:
        p=subprocess.run([os.environ['C_REAL_QEMU'],*args],stdout=f,stderr=subprocess.STDOUT)
else:
    p=subprocess.run([os.environ['C_REAL_QEMU'],*args])
sys.exit(p.returncode)
''')
    wrapper.chmod(0o755)
    gdb_wrapper=Path(tmp)/'riscv64-unknown-elf-gdb'
    gdb_wrapper.write_text('''#!/usr/bin/env python3
import os, subprocess, sys
with open(os.environ['C_GDB_LOG'],'ab') as log:
    p=subprocess.run([os.environ['C_REAL_GDB'],*sys.argv[1:]],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    log.write(p.stdout)
    # With an attached debugger OpenSBI semihosting output arrives via GDB's
    # remote console packets, rather than QEMU's serial/stdout stream.
    with open('.qemu.out','ab') as console:
        console.write(p.stdout)
sys.exit(p.returncode)
''')
    gdb_wrapper.chmod(0o755)
    env=dict(os.environ,PATH=tmp+os.pathsep+os.environ['PATH'],C_REAL_QEMU=real,C_REAL_GDB=real_gdb,C_GDB_LOG=str(root/'docs/logs/c-grade-gdb.log'))
    raise SystemExit(subprocess.call(['make','grade'],cwd=root,env=env))
