#!/usr/bin/env python3
"""Run C scheduling checks and capture first-run GDB evidence in disposable trees."""
from pathlib import Path
import datetime, hashlib, os, shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
logs=root/'docs/logs'/('c-schedule-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
logs.mkdir(exist_ok=False)
def run(cmd,cwd,name,seconds=60):
    p=subprocess.run(['timeout',str(seconds),*cmd],cwd=cwd,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (logs/name).write_bytes(p.stdout)
    assert p.returncode==0,(name,p.returncode,p.stdout[-1500:])
    return p.stdout.decode(errors='replace')
manifest=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode()
for file in sorted([*root.glob('kern/**/*.c'),*root.glob('kern/**/*.S'),*root.glob('tools/c-*')]):
    if file.is_file(): manifest+=hashlib.sha256(file.read_bytes()).hexdigest()+' '+str(file.relative_to(root))+'\n'
(logs/'manifest.log').write_text(manifest)
with tempfile.TemporaryDirectory(prefix='lab4-c-') as temp:
    tree=Path(temp)/'src'
    shutil.copytree(root,tree,ignore=shutil.ignore_patterns('.git','docs','bin','obj','__pycache__'))
    # Append a new assembly object after the boot entry, never before it.
    shutil.copy(root/'tools/c-context-probe.S',tree/'kern/process/c_probe.S')
    path=tree/'kern/process/proc.c'
    source=path.read_text()
    source='#include <sbi.h>\n#include <intr.h>\n'+source
    pos=source.index('// init_main -')
    source=source[:pos]+(root/'tools/c-schedule-tests.inc').read_text()+'\n'+source[pos:]
    start=source.index('static int\ninit_main(')
    body=source.index('{',start)
    source=source[:body+1]+'\n    return c_integration(arg);\n'+source[body+1:]
    body=source.index('{',source.index('void proc_run('))
    source=source[:body+1]+'''
    if(c_expect_idle) {
        assert(proc==idleproc && current->state==PROC_SLEEPING);
        cprintf("[C] no runnable candidate selects idle fallback: PASS\\n");
        cprintf("[C] ALL PASS\\n");
        sbi_shutdown();
    }
'''+source[body+1:]
    path.write_text(source)
    (logs/'isolated-proc.c.txt').write_text(source)
    run(['make','-j2'],tree,'build.log')
    text=run(['make','qemu'],tree,'run.log',30)
    assert '[C] ALL PASS' in text and 'kernel panic' not in text
print('PASS; evidence:',logs.relative_to(root))
