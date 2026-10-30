// Direct/Vectored live routing experiment, 2412456.
#include <pmm.h>
#include <stdio.h>
volatile uintptr_t c_sync_routes, c_soft_routes;
extern void c_vector_table(void);
void c_vector_check(void) {
    uintptr_t vector=read_csr(stvec), status=read_csr(sstatus), sie=read_csr(sie);
    clear_csr(sstatus,SSTATUS_SIE);
    assert(!(read_csr(sip)&2)); // Do not consume another caller's pending event.
    c_sync_routes=c_soft_routes=0;
    write_csr(stvec,(uintptr_t)c_vector_table|1);
    assert(read_csr(stvec)==((uintptr_t)c_vector_table|1));
    asm volatile(".option push\n.option norvc\nebreak\n.option pop" ::: "memory");
    assert(c_sync_routes==1 && c_soft_routes==0);
    write_csr(sie,2);
    set_csr(sip,2);
    set_csr(sstatus,SSTATUS_SIE);
    while(c_soft_routes==0) asm volatile("" ::: "memory");
    clear_csr(sstatus,SSTATUS_SIE);
    assert(c_sync_routes==1 && c_soft_routes==1 && !(read_csr(sip)&2));
    write_csr(stvec,vector);
    write_csr(sie,sie);
    write_csr(sstatus,status);
    assert(read_csr(stvec)==vector && read_csr(sie)==sie && read_csr(sstatus)==status);
    cprintf("[C vectors] sync at BASE, software IRQ at BASE+4, restored: PASS\n");
}
