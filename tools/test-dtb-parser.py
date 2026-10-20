#!/usr/bin/env python3
"""Compile the actual pure C decoder and test independent FDT fixtures."""
import ctypes
from pathlib import Path
import random
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
u32 = lambda n: struct.pack('>I', n)
strings = b'#address-cells\0#size-cells\0reg\0other\0'


def node(name):
    raw = name.encode() + b'\0'
    return u32(1) + raw + b'\0' * (-len(raw) % 4)


def prop(name, value):
    return u32(3) + u32(len(value)) + u32(strings.index(name.encode() + b'\0')) + value + b'\0' * (-len(value) % 4)


def make_blob(ac=2, sc=2, base=0x80000000, size=0x8000000,
              banks=1, memory=True, name='memory@80000000', nested=False):
    body = node('') + prop('#address-cells', u32(ac)) + prop('#size-cells', u32(sc))
    cells = lambda n, c: b''.join(u32((n >> (32 * i)) & 0xffffffff) for i in reversed(range(c)))
    if memory:
        body += node(name)
        if nested:
            body += node('child')
        body += prop('reg', (cells(base, ac) + cells(size, sc)) * banks)
        if nested:
            body += u32(2)
        body += u32(2)
    body += u32(2) + u32(9)
    header = struct.pack('>10I', 0xd00dfeed, 56 + len(body) + len(strings),
                         56, 56 + len(body), 40, 17, 16, 0, len(strings), len(body))
    return header + b'\0' * 16 + body + strings


with tempfile.TemporaryDirectory(prefix='lab2-fdt-') as directory:
    library = Path(directory) / 'decoder.so'
    # OSopt intentionally exposes bare RISC-V as/ld names; select native
    # binutils explicitly for this host-only shared-library test.
    subprocess.run(['/usr/bin/gcc', '-B/usr/bin/', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
        '-fno-builtin', '-nostdinc', '-I' + str(root / 'libs'),
        '-I' + str(root / 'kern/driver'), str(root / 'kern/driver/fdt_memory.c'),
        '-o', str(library)], check=True)
    decoder = ctypes.CDLL(str(library)).dtb_parse_memory
    decoder.argtypes = [ctypes.c_void_p, ctypes.c_uint64,
                        ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_uint64)]
    decoder.restype = ctypes.c_int
    count = 0

    def parse(blob, expected=None, values=None):
        global count
        # One-byte prefix also exercises an intentionally unaligned buffer.
        storage = ctypes.create_string_buffer(b'X' + blob)
        base, size = ctypes.c_uint64(123), ctypes.c_uint64(456)
        result = decoder(ctypes.addressof(storage) + 1, len(blob),
                         ctypes.byref(base), ctypes.byref(size))
        if expected is not None:
            assert result == expected, (count, result, expected)
        if result == 0:
            assert size.value > 0 and base.value + size.value < 1 << 64
            if values:
                assert (base.value, size.value) == values
        else:
            assert (base.value, size.value) == (123, 456)
        count += 1
        return result

    for ac in (1, 2):
        for sc in (1, 2):
            for mb in (64, 96, 128, 256):
                parse(make_blob(ac, sc, size=mb << 20), 0, (0x80000000, mb << 20))
    valid = make_blob()
    for length in range(len(valid)):
        assert parse(valid[:length]) < 0
    for offset, value in [(0, 0), (4, 0xffffffff), (8, 57), (8, 0xffffffff),
                          (12, 0xffffffff), (20, 16), (24, 18),
                          (32, 0xffffffff), (36, 0xffffffff)]:
        blob = bytearray(valid)
        blob[offset:offset + 4] = u32(value)
        parse(bytes(blob), -1)
    parse(make_blob(memory=False), -2)
    parse(make_blob(name='memoryfake'), -2)
    parse(make_blob(nested=True), -2)
    parse(make_blob(size=0), -1)
    parse(make_blob(base=0xfffffffffffff000, size=0x2000), -1)
    parse(make_blob(ac=3), -3)
    parse(make_blob(banks=2), -3)
    # Explicit malformed property/name/token/termination fixtures.
    for offset, value in [(56 + 4, 0xffffffff), (56 + 8, 0xdeadbeef),
                          (56 + 12, 0xffffffff), (56 + 16, 0xffffffff)]:
        blob = bytearray(valid)
        blob[offset:offset + 4] = u32(value)
        assert parse(bytes(blob)) < 0
    blob = bytearray(valid)
    blob[-len(strings):] = b'x' * len(strings)
    parse(bytes(blob), -1)
    # Mutation corpus: accepted changes must preserve success invariants;
    # rejected changes must preserve caller outputs. No fake PASS strings.
    rng = random.Random(2412456)
    for _ in range(2000):
        blob = bytearray(valid)
        for _ in range(rng.randint(1, 4)):
            blob[rng.randrange(len(blob))] = rng.randrange(256)
        parse(bytes(blob))
    print(f'PASS: {count} C FDT parser checks; 1/2-cell layouts, every truncation, malformed bounds, unsupported banks, 2000 mutations')
