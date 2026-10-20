"""source tools/gdb_lab2.py, then lab2-vtop ADDRESS [read|write|fetch]."""
from pathlib import Path
import sys
import json
import gdb

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sv39 import translate


def symbol(name):
    return int(gdb.parse_and_eval('(unsigned long)&' + name))


def offset():
    # Loader address is the lab's boot ABI, while linked VA comes from ELF.
    return symbol('kern_entry') - 0x80200000


def query(va, access=None):
    satp = int(gdb.parse_and_eval('$satp')) & ((1 << 64) - 1)
    def read_physical(pa):
        data = gdb.selected_inferior().read_memory(pa + offset(), 8)
        return int.from_bytes(bytes(data), 'little')
    return translate(va, satp, read_physical, access)


def registers():
    return {name: int(gdb.parse_and_eval('$' + name)) & ((1 << 64) - 1)
            for name in ('pc', 'sp', 'satp')}


class Vtop(gdb.Command):
    def __init__(self):
        super().__init__('lab2-vtop', gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        args = gdb.string_to_argv(argument)
        if not 1 <= len(args) <= 2:
            raise gdb.GdbError('usage: lab2-vtop ADDRESS [read|write|fetch]')
        address = int(gdb.parse_and_eval(args[0])) & ((1 << 64) - 1)
        result = query(address, args[1] if len(args) == 2 else None)
        gdb.write(json.dumps(result, indent=2) + '\n')


Vtop()
