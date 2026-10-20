"""Small, side-effect-free Sv39 structural walker used by GDB and tests.

Reports PTE permissions; does not emulate privilege overrides, PMP, TLB,
hardware A/D updates, MPRV, SUM or MXR. No writes to guest memory.
"""


def translate(va, satp, read_u64, access=None):
    trace = {'va': va, 'satp': satp, 'steps': [], 'ok': False}

    def fail(reason):
        trace['error'] = reason
        return trace

    if not 0 <= va < 1 << 64:
        return fail('address outside uint64')
    mode = satp >> 60
    trace['mode'] = mode
    if mode == 0:
        trace.update(ok=True, physical=va, page_size=None, explanation='Bare: identity address calculation')
        return trace
    if mode != 8:
        return fail('unsupported satp mode')
    sign = (va >> 38) & 1
    if va >> 39 != ((1 << 25) - 1 if sign else 0):
        return fail('noncanonical Sv39 address')
    table = (satp & ((1 << 44) - 1)) << 12
    for level in (2, 1, 0):
        index = (va >> (12 + 9 * level)) & 511
        address = table + index * 8
        try:
            pte = read_u64(address)
        except Exception as error:
            return fail('cannot read physical PTE: ' + str(error))
        flags = ''.join(letter for bit, letter in enumerate('VRWXUGAD') if pte & (1 << bit))
        trace['steps'].append({'level': level, 'table_physical': table, 'index': index,
                               'pte_physical': address, 'pte': pte, 'flags': flags})
        if pte >> 54:
            return fail('reserved high PTE bits')
        if not pte & 1:
            return fail('unmapped: V=0')
        if (pte & 4) and not (pte & 2):
            return fail('invalid PTE: W=1 and R=0')
        ppn = (pte >> 10) & ((1 << 44) - 1)
        if pte & (2 | 8):
            if ppn & ((1 << (9 * level)) - 1):
                return fail('misaligned superpage')
            permissions = {'read': 2, 'write': 4, 'fetch': 8}
            if access is not None and access not in permissions:
                return fail('unknown access type')
            if access and not pte & permissions[access]:
                return fail('leaf denies ' + access)
            page_size = 1 << (12 + 9 * level)
            trace.update(ok=True, physical=(ppn << 12) | (va & (page_size - 1)),
                         leaf_level=level, page_size=page_size, permissions=flags,
                         ad_bits={'accessed': bool(pte & 64), 'dirty': bool(pte & 128)})
            return trace
        if pte & (16 | 64 | 128):
            return fail('reserved U/A/D bits in nonleaf PTE')
        table = ppn << 12
    return fail('nonleaf at final level')
