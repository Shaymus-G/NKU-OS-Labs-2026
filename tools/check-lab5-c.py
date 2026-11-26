#!/usr/bin/env python3
"""Run the Lab5 lifecycle and VM checks from isolated source copies."""
import argparse
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import tempfile
from time import monotonic


ROOT = Path(__file__).resolve().parent.parent
DONE = b"init check memory pass."
CASES = {
    "hello": ("hello pass.",),
    "exit": ("exit pass.",),
    "yield": ("yield pass.",),
    "spin": ("spinning", "kill returns 0", "wait returns 0", "spin may pass."),
    "forktest": ("forktest pass.",),
    "forktree": ("I am ''",),
    "waitkill": ("kill parent ok.", "kill child1 ok."),
    "c_lifecycle": (
        "C VM: CLONE_VM visibility and refcount PASS",
        "C VM: real page exhaustion and partial fork rollback PASS",
        "C VM: free pages restored",
        "C user: fork isolation, cross-page wait output and repeat wait PASS",
        "C user: adopted running child finished",
        "C user: orphan handoff and invalid kill PASS",
    ),
}
COPIED = ("Makefile", "kern", "libs", "tools", "user")


def run_guest(source, log):
    command = ["qemu-system-riscv64", "-machine", "virt", "-nographic", "-bios", "default",
               "-device", "loader,file=bin/ucore.img,addr=0x80200000"]
    output = bytearray()
    with subprocess.Popen(command, cwd=source, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as guest:
        try:
            with selectors.DefaultSelector() as reader:
                reader.register(guest.stdout, selectors.EVENT_READ)
                deadline = monotonic() + 30
                while DONE not in output and reader.select(max(0, deadline - monotonic())):
                    chunk = os.read(guest.stdout.fileno(), 65536)
                    if not chunk:
                        break
                    output.extend(chunk)
                    if monotonic() >= deadline:
                        break
        finally:
            if guest.poll() is None:
                guest.terminate()
            try:
                tail, _ = guest.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                guest.kill()
                tail, _ = guest.communicate()
            output.extend(tail)
            result = output.decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n")
            log.write_text(result, encoding="utf-8")
    return result


def prepare(source):
    source.mkdir()
    for name in COPIED:
        path = ROOT / name
        if path.is_dir():
            shutil.copytree(path, source / name, ignore=shutil.ignore_patterns("__pycache__"))
        else:
            shutil.copy2(path, source / name)
    shutil.copy2(ROOT / "tools/c-lab5-user.c", source / "user/c_lifecycle.c")
    proc = source / "kern/process/proc.c"
    original = proc.read_text()
    entry = "static int\ninit_main(void *arg)\n{"
    if original.count(entry) != 1:
        raise RuntimeError("could not locate init_main injection point")
    harness = '#ifdef C_LAB5_VM_TESTS\n#include "../../tools/c-lab5-vm.inc"\n#endif\n'
    call = "\n#ifdef C_LAB5_VM_TESTS\n    c_vm_tests();\n#endif"
    proc.write_text(original.replace(entry, harness + entry + call))


def run_mode(mode, source, logs, cases):
    prepare(source)
    proc = source / "kern/process/proc.c"
    for name in cases:
        flags = ["-DLAB5_EAGER_FORK"] if mode == "eager" else []
        if name == "c_lifecycle":
            flags.append("-DC_LAB5_VM_TESTS")
        binary = f"_binary_obj___user_{name}_out"
        flags.extend([f"-DTEST={name}", f"-DTESTSTART={binary}_start", f"-DTESTSIZE={binary}_size"])
        proc.touch()
        build_log = logs / f"{mode}-{name}-build.log"
        with build_log.open("wb") as build:
            command = ["make", "-j4", "DEFS=" + " ".join(flags)]
            build.write((" ".join(command) + "\n").encode())
            build.flush()
            subprocess.run(command, cwd=source, stdout=build, stderr=subprocess.STDOUT,
                           check=True, timeout=90)
        run_log = logs / f"{mode}-{name}.log"
        result = run_guest(source, run_log)
        expected = (*CASES[name], DONE.decode())
        if name == "c_lifecycle":
            expected += (("COW OOM, split and re-use PASS" if mode == "cow"
                          else "eager copy, read-only leaf and unmapped hole PASS"),)
        missing = [item for item in expected if item not in result]
        if missing or "kernel panic" in result or "user panic" in result:
            raise RuntimeError(f"{mode}/{name} failed; missing {missing}; see {run_log}")
        print(f"{mode}/{name}: PASS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("cow", "eager", "both"), default="both")
    parser.add_argument("--case", choices=CASES, action="append", help="default: all cases")
    parser.add_argument("--log-dir", required=True, type=Path)
    args = parser.parse_args()
    args.log_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nku-lab5-c-") as work:
        for mode in ("cow", "eager") if args.mode == "both" else (args.mode,):
            run_mode(mode, Path(work) / mode, args.log_dir, args.case or CASES)


if __name__ == "__main__":
    main()
