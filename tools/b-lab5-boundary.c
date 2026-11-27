/* Copied to a temporary user/ directory by verify-lab5-b.py --boundary-only. */
#include <ulib.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>
#include <error.h>
#include <unistd.h>

static int raw_call(uint64_t number, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4)
{
    register uint64_t a0 asm("a0") = number;
    register uint64_t a1 asm("a1") = p1;
    register uint64_t a2 asm("a2") = p2;
    register uint64_t a3 asm("a3") = p3;
    register uint64_t a4 asm("a4") = p4;
    register uint64_t a5 asm("a5") = 0;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5) : "memory");
    return (int)a0;
}

int main(void)
{
    assert(raw_call(0, 0, 0, 0, 0) == -E_INVAL);
    assert(raw_call((uint64_t)-1, 0, 0, 0, 0) == -E_INVAL);
    assert(getpid() > 0);
    cprintf("B user: invalid syscall numbers return to U mode PASS\n");
    unsigned char binary[512];
    memset(binary, 0, sizeof(binary));
    struct elfhdr *elf = (void *)binary;
    elf->e_magic = ELF_MAGIC; elf->e_elf[0] = 2; elf->e_elf[1] = 1; elf->e_elf[2] = 1;
    elf->e_type = 2; elf->e_machine = 243; elf->e_version = 1;
    elf->e_ehsize = sizeof(*elf); elf->e_phoff = sizeof(*elf);
    elf->e_phentsize = sizeof(struct proghdr); elf->e_phnum = 1;
    elf->e_entry = 0x800100;
    struct proghdr *ph = (void *)(binary + sizeof(*elf));
    ph->p_type = ELF_PT_LOAD; ph->p_flags = ELF_PF_R | ELF_PF_X;
    ph->p_offset = 256; ph->p_va = elf->e_entry;
    ph->p_filesz = ph->p_memsz = 16; ph->p_align = 1;
    const uint32_t code[] = {0x00100513, 0x02a00593, 0x00000073, 0x0000006f};
    memcpy(binary + 256, code, sizeof(code)); // SYS_exit(42), then an unreachable loop
    const char name[] = "b-exec";
    int child = fork();
    assert(child >= 0);
    if (child == 0) {
        assert(raw_call(SYS_exec, (uintptr_t)name, 6, 0xffffffffc0200000ULL, 512) == -E_INVAL_ELF);
        elf->e_magic = 0;
        assert(raw_call(SYS_exec, (uintptr_t)name, 6, (uintptr_t)binary, sizeof(binary)) == -E_INVAL_ELF);
        elf->e_magic = ELF_MAGIC;
        cprintf("B user: invalid exec preserves old program PASS\n");
        raw_call(SYS_exec, (uintptr_t)name, 6, (uintptr_t)binary, sizeof(binary));
        panic("successful exec returned to old program");
    }
    int status = 0;
    assert(waitpid(child, &status) == 0 && status == 42);
    cprintf("B user: user-buffer exec enters new ELF and exits 42 PASS\n");
    return 0;
}
