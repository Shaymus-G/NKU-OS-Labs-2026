"""Executed by GDB Python, not the host Python interpreter."""
from pathlib import Path
import json
import os
import sys
import gdb

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gdb_lab2 import query, registers, symbol, offset

directory = Path(os.environ['LAB2_DEMO_DIR'])
port = os.environ['LAB2_DEMO_PORT']
pre = int(os.environ['LAB2_PRE_SATP_PA'], 16)
gdb.execute('set pagination off')
gdb.execute('set confirm off')
gdb.execute('set remotetimeout 10')
gdb.execute('target remote 127.0.0.1:' + port)
gdb.execute('hbreak *' + hex(pre))
gdb.execute('continue')
before_step = registers()
gdb.execute('x/i $pc')
gdb.execute('delete breakpoints')
gdb.execute('stepi')
before_satp = registers()
gdb.execute('x/i $pc')
assert before_step['satp'] == 0 and before_satp['satp'] == 0
assert before_satp['pc'] > before_step['pc']
# Do not single-step across the satp write. Drop the low-address breakpoint
# before paging, then wait at the linked high entry as in A's successful fix.
gdb.execute('hbreak kern_init')
gdb.execute('continue')
after_paging = registers()
gdb.execute('x/i $pc')
gdb.execute('delete breakpoints')
gdb.execute('stepi')
after_high_step = registers()
assert after_paging['pc'] == symbol('kern_init')
assert after_paging['satp'] >> 60 == 8
assert after_high_step['pc'] > after_paging['pc']

queries = [query(symbol('kern_init'), 'fetch'),
           query(symbol('bootstacktop'), 'read'), query(0), query(1 << 38)]
target = queries[0]
assert target['ok'] and target['physical'] == symbol('kern_init') - offset()
assert target['page_size'] == 1 << 30 and len(target['steps']) == 1
assert not queries[2]['ok'] and not queries[3]['ok']
result = {'before_bare_step': before_step, 'before_satp_write': before_satp,
          'high_entry': after_paging, 'after_high_step': after_high_step,
          'linked_root_physical': symbol('boot_page_table_sv39') - offset(),
          'queries': queries,
          'scope': 'Live guest GDB registers and PTE reads; no host QEMU TLB observation.'}
assert (after_paging['satp'] & ((1 << 44) - 1)) << 12 == result['linked_root_physical']
(directory / 'trace.json').write_text(json.dumps(result, indent=2) + '\n')
gdb.write('LAB2 LIVE DEMO: PASS\n' + json.dumps(result, indent=2) + '\n')
gdb.execute('detach')
