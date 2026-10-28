#!/usr/bin/env python3
"""Run B's targeted checks in disposable copies; leave the shared tree intact."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / "docs" / "logs"
TEST_SOURCE = r'''
#include <assert.h>
#include <clock.h>
#include <intr.h>
#include <pmm.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <../sync/sync.h>

extern void interrupt_handler(struct trapframe *tf);
extern void exception_handler(struct trapframe *tf);

/* The instruction after each trap is explicitly 32 bits. A bad EPC increment
 * must not silently skip it. Host-side checks also count the actual traps. */
#define CHECK_TRAP(encoding) do { \
    uintptr_t continued; \
    asm volatile(".option push\n.option norvc\n" \
                 "li %0, 0\n" encoding "\naddi %0, %0, 1\n" \
                 ".option pop\n" : "=&r"(continued) : : "memory"); \
    assert(continued == 1); \
} while (0)

void b_selfcheck(void) {
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    for (int i = 0; i < 3; i++) {
        CHECK_TRAP(".2byte 0x9002");
        CHECK_TRAP(".4byte 0x00100073");
        CHECK_TRAP(".2byte 0x0000");
        CHECK_TRAP(".4byte 0x0000007b");
    }
    cprintf("[B check] repeated 16/32-bit exception returns: PASS\n");

    struct trapframe tf;
    memset(&tf, 0, sizeof(tf));
    const uintptr_t flag = (uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1);
    const uintptr_t causes[] = {
        IRQ_U_SOFT, IRQ_S_SOFT, IRQ_H_SOFT, IRQ_M_SOFT,
        IRQ_U_TIMER, IRQ_H_TIMER, IRQ_M_TIMER,
        IRQ_U_EXT, IRQ_S_EXT, IRQ_H_EXT, IRQ_M_EXT, 15,
    };
    for (size_t i = 0; i < sizeof(causes) / sizeof(causes[0]); i++) {
        tf.cause = flag | causes[i];
        interrupt_handler(&tf);
        assert(tf.epc == 0 && ticks == 0 && tf.cause == (flag | causes[i]));
    }
    tf.cause = 15;
    interrupt_handler(&tf);
    assert(tf.epc == 0 && ticks == 0);
    tf.cause = 20;
    exception_handler(&tf);
    assert(tf.epc == 0 && tf.cause == 20);
    cprintf("[B check] diagnostic branches with null EPC: PASS\n");

    tf.cause = flag | IRQ_S_TIMER;
    tf.epc = 0x1234;
    ticks = 98;
    interrupt_handler(&tf);
    assert(ticks == 99 && tf.epc == 0x1234);
    interrupt_handler(&tf);
    assert(ticks == 100 && tf.epc == 0x1234);
    ticks = 199;
    interrupt_handler(&tf);
    assert(ticks == 200 && tf.epc == 0x1234);
    ticks = 0;
    cprintf("[B check] timer counts once and preserves EPC: PASS\n");

    /* Exercise the existing PMM interrupt wrapper from both initial states. */
    size_t before = nr_free_pages();
    struct Page *p = alloc_page();
    assert(p != NULL && !(read_csr(sstatus) & SSTATUS_SIE));
    free_page(p);
    assert(nr_free_pages() == before && !(read_csr(sstatus) & SSTATUS_SIE));
    bool enabled;
    intr_enable();
    p = alloc_page();
    local_intr_save(enabled);
    assert(enabled && p != NULL);
    intr_enable();
    free_page(p);
    local_intr_save(enabled);
    assert(enabled && nr_free_pages() == before);
    cprintf("[B check] PMM preserves interrupt enable state: PASS\n");
    cprintf("[B check] ALL PASS\n");
}
'''


def run_logged(command, cwd, log, timeout):
    with log.open("w", encoding="utf-8") as stream:
        stream.write("Command: " + " ".join(command) + "\n")
        stream.flush()
        try:
            result = subprocess.run(
                command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                timeout=timeout, check=False,
            )
        except subprocess.TimeoutExpired:
            stream.write("\nFAILED: timeout\n")
            raise
        stream.write("\nExit code: " + str(result.returncode) + "\n")
    if result.returncode != 0:
        raise RuntimeError(f"Command failed; see {log}")
    return log.read_text(encoding="utf-8")


def check_output(text):
    required = [
        "check_alloc_page() succeeded!",
        "[B check] repeated 16/32-bit exception returns: PASS",
        "[B check] diagnostic branches with null EPC: PASS",
        "[B check] timer counts once and preserves EPC: PASS",
        "[B check] PMM preserves interrupt enable state: PASS",
        "[B check] ALL PASS",
    ]
    for marker in required:
        if marker not in text:
            raise AssertionError("Missing output: " + marker)
    for prefix in ["Breakpoint at", "Illegal instruction at"]:
        count = sum(line.startswith(prefix) for line in text.splitlines())
        if count != 6:
            raise AssertionError(f"Expected six {prefix} events, got {count}")
    # Two synthetic timer summaries precede the normal 1000-tick run.
    if text.splitlines().count("100 ticks") != 12:
        raise AssertionError("Unexpected timer summary count")
    if "kernel panic" in text or "FAIL" in text:
        raise AssertionError("Kernel reported failure")


def main():
    LOGS.mkdir(parents=True, exist_ok=True)
    manifest = [
        "B self-check: isolated First-Fit and Best-Fit builds",
        "Expected SHUTDOWN_AFTER: 10; normal shutdown after 1000 ticks",
    ]
    for name in ["kern/mm/best_fit_pmm.c", "kern/trap/trap.c", "tools/b-selfcheck.py"]:
        manifest.append(name + " SHA256 " + hashlib.sha256((ROOT / name).read_bytes()).hexdigest())
    for name in ["riscv64-unknown-elf-gcc", "qemu-system-riscv64"]:
        manifest.append(subprocess.check_output([name, "--version"], text=True).splitlines()[0])
    (LOGS / "b-selfcheck-manifest.log").write_text("\n".join(manifest) + "\n", encoding="utf-8")

    for manager in ["default", "best_fit"]:
        print(f"B checks: {manager} (temporary copy)", flush=True)
        with tempfile.TemporaryDirectory(prefix="lab3_b_") as temporary:
            work = Path(temporary) / "source"
            shutil.copytree(
                ROOT, work,
                ignore=shutil.ignore_patterns(".git", "obj", "bin", "docs", "__pycache__"),
            )
            # Only the disposable copy receives a probe and a different PMM.
            init = work / "kern/init/init.c"
            text = init.read_text(encoding="utf-8")
            text = "void b_selfcheck(void);\n" + text
            position = text.rfind("idt_init();")
            if position < 0:
                raise RuntimeError("Could not locate final idt_init()")
            position += len("idt_init();")
            text = text[:position] + "\n    b_selfcheck();\n" + text[position:]
            init.write_text(text, encoding="utf-8")
            (work / "kern/mm/b_selfcheck.c").write_text(TEST_SOURCE, encoding="utf-8")
            pmm = work / "kern/mm/pmm.c"
            text = pmm.read_text(encoding="utf-8")
            original = "pmm_manager = &default_pmm_manager;"
            if text.count(original) != 1:
                raise RuntimeError("Unexpected PMM selection")
            pmm.write_text(text.replace(original, f"pmm_manager = &{manager}_pmm_manager;"), encoding="utf-8")
            build_log = LOGS / f"b-selfcheck-{manager}-build.log"
            run_log = LOGS / f"b-selfcheck-{manager}.log"
            run_logged(["make"], work, build_log, 90)
            command = [
                "qemu-system-riscv64", "-machine", "virt", "-nographic",
                "-bios", "default", "-device", "loader,file=bin/ucore.img,addr=0x80200000",
                "-monitor", "none", "-no-reboot",
            ]
            text = run_logged(command, work, run_log, 25)
            check_output(text)
            if f"memory management: {manager}_pmm_manager" not in text:
                raise AssertionError("Wrong manager was exercised")
            print(f"  PASS: {manager}; log: {run_log}", flush=True)
    print("B self-checks: ALL PASS", flush=True)


if __name__ == "__main__":
    main()
