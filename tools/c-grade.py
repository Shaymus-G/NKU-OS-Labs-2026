#!/usr/bin/env python3
"""Run unmodified grading with a local PATH console adapter for OpenSBI 1.3."""
from pathlib import Path
import os, shutil, subprocess, tempfile
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
    env=dict(os.environ,PATH=tmp+os.pathsep+os.environ['PATH'],C_REAL_QEMU=real)
    raise SystemExit(subprocess.call(['make','grade'],cwd=root,env=env))
