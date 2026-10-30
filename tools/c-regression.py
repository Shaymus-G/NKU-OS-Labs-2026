#!/usr/bin/env python3
"""Independent Lab3 checks in disposable copies, no teacher/source mutation."""
from pathlib import Path
import argparse, datetime, hashlib, os, shutil, subprocess, tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--user-mode',action='store_true',help='optional exploration; legacy Challenge is not required this year')
args=parser.parse_args()

root=Path(__file__).resolve().parents[1]
logs=root/'docs/logs/c-runs'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
logs.mkdir(parents=True,exist_ok=False)
def run(cmd,cwd,name,timeout=120,env=None):
    p=subprocess.run(['timeout',str(timeout),*cmd],cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (logs/name).write_bytes(p.stdout)
    assert p.returncode==0,(name,p.returncode,p.stdout[-2000:])
    return p.stdout.decode(errors='replace').replace('\r\n','\n')
manifest=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode()
for file in sorted([*root.glob('kern/**/*.c'),*root.glob('kern/**/*.S'),root/'tools/c-regression.py']):
    manifest+=hashlib.sha256(file.read_bytes()).hexdigest()+'  '+str(file.relative_to(root))+'\n'
(logs/'manifest.log').write_text(manifest)
for manager in ['default','best_fit']:
    with tempfile.TemporaryDirectory(prefix='lab3-c-') as tmp:
        tree=Path(tmp)/'src'
        shutil.copytree(root,tree,ignore=shutil.ignore_patterns('.git','obj','bin','docs','__pycache__'))
        make=tree/'Makefile'
        make.write_text(make.read_text().replace('KSRCDIR\t\t+= kern/init','KSRCDIR\t\t+= kern/init kern/lab3_checks'))
        init=tree/'kern/init/init.c'
        text=init.read_text()
        assert text.count('    clock_init();')==1
        hook='    extern void c_checks(void);\n    c_checks();\n'
        if args.user_mode:
            hook+='    extern void c_user_check(void);\n    c_user_check();\n'
        text=text.replace('    clock_init();',hook+'    clock_init();')
        init.write_text(text.replace('    /* do nothing */','    extern void c_timer_checks(void);\n    c_timer_checks();\n    /* do nothing */'))
        if manager=='best_fit':
            pmm=tree/'kern/mm/pmm.c'
            text=pmm.read_text()
            assert 'pmm_manager = &default_pmm_manager;' in text
            pmm.write_text(text.replace('pmm_manager = &default_pmm_manager;','pmm_manager = &best_fit_pmm_manager;'))
        run(['make'],tree,manager+'-build.log')
        output=run(['make','qemu'],tree,manager+'-run.log',45)
        assert output.count('100 ticks\n')==10
        assert output.count('Breakpoint at')==32
        assert output.count('Illegal instruction at')==32
        assert output.count('32 register slots: PASS')==4
        assert '[C] PMM success/failure, nested SIE preservation: PASS' in output
        assert '[C] real timer passed 100 and 200, SIE restored: PASS' in output
        if args.user_mode:
            assert '[C user] S->U->S->U->S, separate stack, ecall result: PASS' in output
        else:
            assert '[C user]' not in output
        assert 'panic' not in output.lower()
        assert 'memory management: '+manager+'_pmm_manager' in output
        print(manager+': independent checks and 1000-tick shutdown PASS',flush=True)
print('Evidence:',logs.relative_to(root))
