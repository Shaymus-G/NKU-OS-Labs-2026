// Independent integration checks, 2412456; enabled only in disposable copies.
#include <pmm.h>
#include <clock.h>
#include <stdio.h>
#include <intr.h>
#include <../sync/sync.h>
extern void c_probe_0(uintptr_t *), c_probe_1(uintptr_t *);
extern void c_probe_2(uintptr_t *), c_probe_3(uintptr_t *);

void c_checks(void) {
    bool original;
    local_intr_save(original);
    void (*probes[])(uintptr_t *) = {c_probe_0,c_probe_1,c_probe_2,c_probe_3};
    uintptr_t snapshot[65];
    for (unsigned k=0;k<4;k++) {
        for (unsigned round=0;round<16;round++) {
            snapshot[64]=1;
            probes[k](snapshot);
            assert(snapshot[64]==0);
            for(unsigned r=0;r<32;r++) assert(snapshot[r]==snapshot[32+r]);
            assert(!(read_csr(sstatus)&SSTATUS_SIE));
        }
        cprintf("[C] encoding %u: 16 returns, 32 register slots: PASS\n",k);
    }
    size_t before=nr_free_pages();
    for(unsigned enabled=0;enabled<2;enabled++) {
        if(enabled) intr_enable(); else intr_disable();
        struct Page *p=alloc_page();
        assert(p!=NULL);
        assert(!!(read_csr(sstatus)&SSTATUS_SIE)==enabled);
        assert(alloc_pages(before+1)==NULL);
        assert(!!(read_csr(sstatus)&SSTATUS_SIE)==enabled);
        free_page(p);
        assert(nr_free_pages()==before);
        assert(!!(read_csr(sstatus)&SSTATUS_SIE)==enabled);
        bool outer,inner;
        local_intr_save(outer);
        local_intr_save(inner);
        assert(!inner && !(read_csr(sstatus)&SSTATUS_SIE));
        local_intr_restore(inner);
        assert(!(read_csr(sstatus)&SSTATUS_SIE));
        local_intr_restore(outer);
        assert(!!(read_csr(sstatus)&SSTATUS_SIE)==enabled);
    }
    intr_disable();
    local_intr_restore(original);
    cprintf("[C] PMM success/failure, nested SIE preservation: PASS\n");
    cprintf("[C] synchronous checks complete; real timer follows\n");
}

void c_timer_checks(void) {
    while (ticks < 201) asm volatile("" ::: "memory");
    assert(read_csr(sstatus)&SSTATUS_SIE);
    cprintf("[C] real timer passed 100 and 200, SIE restored: PASS\n");
}
