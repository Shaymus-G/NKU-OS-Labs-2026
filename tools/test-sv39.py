#!/usr/bin/env python3
"""Independent hand-built page-table fixtures, not replayed GDB output."""
from sv39 import translate

satp = (8 << 60) | 1
count = 0


def check(va, table, physical=None, error=None, mode=satp, access=None, levels=None):
    global count
    result = translate(va, mode, table.__getitem__, access)
    if error:
        assert not result['ok'] and error in result['error'], result
    else:
        assert result['ok'] and result['physical'] == physical, result
    if levels is not None:
        assert len(result['steps']) == levels, result
    count += 1


high = 0xffffffffc02000d8
root_entry = 0x1000 + 511 * 8
check(high, {root_entry: (0x80000 << 10) | 0xcf}, 0x802000d8, levels=1)
for level in (0, 1, 2):
    va = 0x1234567
    table = {}
    for depth in range(2, level, -1):
        table[(3 - depth) * 0x1000 + ((va >> (12 + 9 * depth)) & 511) * 8] = ((4 - depth) << 10) | 1
    target_table = (3 - level) * 0x1000
    page_size = 1 << (12 + 9 * level)
    physical_base = 0x80000000
    table[target_table + ((va >> (12 + 9 * level)) & 511) * 8] = ((physical_base >> 12) << 10) | 0xcf
    check(va, table, physical_base | (va & (page_size - 1)), levels=3 - level)
check(high, {}, high, mode=0, levels=0)
check(1 << 38, {}, error='noncanonical')
check(-1, {}, error='uint64')
check(1 << 64, {}, error='uint64')
check(0, {}, error='unsupported', mode=9 << 60)
check(high, {root_entry: 0}, error='unmapped')
check(high, {root_entry: 5}, error='W=1')
check(high, {root_entry: (0x80001 << 10) | 0xcf}, error='misaligned')
check(high, {root_entry: (1 << 63) | 0xcf}, error='reserved high')
check(high, {root_entry: 1 | 64}, error='nonleaf')
check(high, {root_entry: (0x80000 << 10) | 3}, error='denies fetch', access='fetch')
check(high, {root_entry: (0x80000 << 10) | 0xcf}, 0x802000d8, access='write')
check(high, {}, error='cannot read')
print(f'PASS: {count} Sv39 fixtures; 4KiB/2MiB/1GiB, Bare, permissions and invalid mappings')
