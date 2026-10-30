#!/usr/bin/env python3
"""Independent Lab3 checks in disposable copies, no teacher/source mutation."""
from pathlib import Path
import argparse, datetime, hashlib, json, os, shutil, subprocess, tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--user-mode',action='store_true',help='optional exploration; legacy Challenge is not required this year')
parser.add_argument('--vectors',action='store_true',help='exercise the handbook stvec extension using real traps')
parser.add_argument('--mutant',choices=['drop-t0','epc-always4'],help='verify the tests reject a deliberately broken disposable kernel')
args=parser.parse_args()

root=Path(__file__).resolve().parents[1]
logs=root/'docs/logs/c-runs'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
logs.mkdir(parents=True,exist_ok=False)
def run(cmd,cwd,name,timeout=120,env=None,expect_failure=False):
    p=subprocess.run(['timeout',str(timeout),*cmd],cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (logs/name).write_bytes(p.stdout)
    if expect_failure:
        assert p.returncode!=0 and b'assertion failed' in p.stdout,(name,p.returncode,p.stdout[-2000:])
    else:
        assert p.returncode==0,(name,p.returncode,p.stdout[-2000:])
    return p.stdout.decode(errors='replace').replace('\r\n','\n')
manifest=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode()
for file in sorted([*root.glob('kern/**/*.c'),*root.glob('kern/**/*.S'),*root.glob('kern/**/*.h'),*root.glob('libs/*'),root/'Makefile',root/'tools/kernel.ld',root/'tools/c-regression.py']):
    manifest+=hashlib.sha256(file.read_bytes()).hexdigest()+'  '+str(file.relative_to(root))+'\n'
(logs/'manifest.log').write_text(manifest)
(logs/'invocation.json').write_text(json.dumps(vars(args),indent=2))
results=[]
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
        if args.vectors:
            hook+='    extern void c_vector_check(void);\n    c_vector_check();\n'
        text=text.replace('    clock_init();',hook+'    clock_init();')
        init.write_text(text.replace('    /* do nothing */','    extern void c_timer_checks(void);\n    c_timer_checks();\n    /* do nothing */'))
        if manager=='best_fit':
            pmm=tree/'kern/mm/pmm.c'
            text=pmm.read_text()
            assert 'pmm_manager = &default_pmm_manager;' in text
            pmm.write_text(text.replace('pmm_manager = &default_pmm_manager;','pmm_manager = &best_fit_pmm_manager;'))
        if args.mutant:
            target=tree/('kern/trap/trapentry.S' if args.mutant=='drop-t0' else 'kern/trap/trap.c')
            old=('LOAD x5, 5*REGBYTES(sp)' if args.mutant=='drop-t0' else 'tf->epc += (instruction & 0x3) == 0x3 ? 4 : 2;')
            new=('nop # deliberate test mutation' if args.mutant=='drop-t0' else 'tf->epc += 4;')
            text=target.read_text()
            assert text.count(old)==1
            target.write_text(text.replace(old,new))
        for relative in ['Makefile','kern/init/init.c','kern/mm/pmm.c','kern/trap/trapentry.S','kern/trap/trap.c']:
            (logs/(manager+'-'+Path(relative).name+'.txt')).write_text((tree/relative).read_text())
        run(['make'],tree,manager+'-build.log')
        (logs/(manager+'-elf.sha256')).write_text(hashlib.sha256((tree/'bin/kernel').read_bytes()).hexdigest()+'\n')
        if args.mutant:
            output=run(['make','qemu'],tree,manager+'-run.log',10,expect_failure=True)
            results.append({'manager':manager,'mutant':args.mutant,'rejected':True})
            print(manager+': deliberate '+args.mutant+' rejected by assertion',flush=True)
            continue
        output=run(['make','qemu'],tree,manager+'-run.log',45)
        assert output.count('100 ticks\n')==10
        assert output.count('Breakpoint at')==32+int(args.vectors)
        assert output.count('Illegal instruction at')==32
        assert output.count('32 register slots: PASS')==4
        assert '[C] PMM success/failure, nested SIE preservation: PASS' in output
        assert '[C] real timer passed 100 and 200, SIE restored: PASS' in output
        if args.user_mode:
            assert '[C user] S->U->S->U->S, separate stack, ecall result: PASS' in output
            assert '[C user] stack execute denied, kernel stack and CSR restoration: PASS' in output
        else:
            assert '[C user]' not in output
        if args.vectors:
            assert '[C vectors] sync at BASE, software IRQ at BASE+4, restored: PASS' in output
        assert 'panic' not in output.lower()
        assert 'memory management: '+manager+'_pmm_manager' in output
        results.append({'manager':manager,'pass':True,'exception_probes':64,'timer_summaries':10})
        print(manager+': independent checks and 1000-tick shutdown PASS',flush=True)
(logs/'results.json').write_text(json.dumps(results,indent=2))
print('Evidence:',logs.relative_to(root))
