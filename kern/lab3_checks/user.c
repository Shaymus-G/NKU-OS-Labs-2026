// Isolated U/S experiment, 2412456. Never enabled in the default kernel.
#include <pmm.h>
#include <trap.h>
#include <stdio.h>
static uintptr_t root[512] __attribute__((aligned(4096)));
static uintptr_t middle[512] __attribute__((aligned(4096)));
static uintptr_t leaf[512] __attribute__((aligned(4096)));
static char user_stack[4096] __attribute__((aligned(4096)));
uintptr_t c_supervisor_sp, c_supervisor_ra;
extern char c_user_code[], c_user_resume[];
extern void c_user_enter(void), c_user_vector(void);
extern uintptr_t boot_page_table_sv39[];
static unsigned calls;
static unsigned faults;

void c_user_trap(struct trapframe *tf) {
    assert(!(tf->status&SSTATUS_SPP));
    assert(tf->gpr.sp==0x3000);
    assert(tf->epc>=0x1000 && tf->epc<0x2000);
    if(tf->cause!=CAUSE_USER_ECALL) {
        const uintptr_t expected[]={2,13,15};
        assert(faults<3 && calls==1 && tf->cause==expected[faults]);
        if(faults==1) assert(tf->badvaddr==KERNBASE);
        if(faults==2) assert(tf->badvaddr==0x1000);
        faults++;
        tf->epc+=4; // These three test instructions are explicitly uncompressed.
        return;
    }
    calls++;
    if(calls==1) {
        assert(tf->gpr.a7==1);
        tf->gpr.a0=0x567;
        tf->epc+=4;
    } else {
        assert(calls==2 && faults==3 && tf->gpr.a7==2 && tf->gpr.a0==0x567);
        assert(tf->gpr.a1==0x123);
        tf->status|=SSTATUS_SPP;
        tf->epc=(uintptr_t)c_user_resume;
        tf->gpr.sp=c_supervisor_sp;
    }
}

void c_user_check(void) {
    uintptr_t old_status=read_csr(sstatus), old_satp=read_csr(satp);
    uintptr_t old_vector=read_csr(stvec), old_sie=read_csr(sie);
    uintptr_t old_scratch=read_csr(sscratch);
    write_csr(sie,0);
    clear_csr(sstatus,SSTATUS_SIE|SSTATUS_SPIE|SSTATUS_SPP);
    root[511]=boot_page_table_sv39[511];
    root[0]=((PADDR(middle)>>12)<<10)|1;
    middle[0]=((PADDR(leaf)>>12)<<10)|1;
    // V,R,X,U,A: user code is non-writable. V,R,W,U,A,D: stack is NX.
    leaf[1]=((PADDR(c_user_code)>>12)<<10)|0x5b;
    leaf[2]=((PADDR(user_stack)>>12)<<10)|0xd7;
    calls=0;
    faults=0;
    write_csr(stvec,c_user_vector);
    write_csr(satp,((uintptr_t)8<<60)|(PADDR(root)>>12));
    asm volatile("sfence.vma" ::: "memory");
    c_user_enter();
    write_csr(satp,old_satp);
    asm volatile("sfence.vma" ::: "memory");
    write_csr(stvec,old_vector);
    write_csr(sscratch,old_scratch);
    write_csr(sie,old_sie);
    write_csr(sstatus,old_status);
    assert(calls==2 && read_csr(satp)==old_satp && read_csr(stvec)==old_vector);
    cprintf("[C user] S->U->S->U->S, separate stack, ecall result: PASS\n");
    cprintf("[C user] user stack RW, supervisor CSR/kernel read/code write denied: PASS\n");
}
