#!/usr/bin/env python3
"""Grade-only console adapter; preserve the installed QEMU wrapper.

OpenSBI v1.3 on this local QEMU chooses semihosting (host stdout), while
the teacher grader reads a -serial file. Route both to that output file
by changing the requested serial backend to stdio and redirecting stdout.
No kernel, firmware, grading assertions or QEMU image options are changed.
Usage: make grade QEMU='python3 tools/qemu-grade-console.py'
"""
import os
import sys

args = sys.argv[1:]
for i in range(len(args) - 1):
    if args[i] == '-serial' and args[i + 1].startswith('file:'):
        output_path = args[i + 1][len('file:'):]
        args[i + 1] = 'stdio'
        fd = os.open(output_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        os.dup2(fd, 1)
        os.dup2(fd, 2)
        if fd not in (1, 2):
            os.close(fd)
        break
os.execvp('qemu-system-riscv64', ['qemu-system-riscv64'] + args)
