// C isolated integration assertions, 2412456.
extern int c_context_yield(void);
static volatile unsigned c_counts[2];
volatile int c_expect_idle;
static int c_worker(void *arg) {
    unsigned id=(uintptr_t)arg;
    volatile uintptr_t guard[8];
    for(unsigned j=0;j<8;j++) guard[j]=0x123400+id*16+j;
    for(unsigned i=0;i<100;i++) {
        assert(current->state==PROC_RUNNABLE);
        assert(read_csr(satp)==(((uintptr_t)8<<60)|(current->pgdir>>12)));
        assert(read_csr(sstatus)&SSTATUS_SIE);
        assert(c_context_yield()==0);
        for(unsigned j=0;j<8;j++) assert(guard[j]==0x123400+id*16+j);
        c_counts[id]++;
    }
    current->state=PROC_SLEEPING;
    schedule();
    panic("sleeping worker unexpectedly resumed");
}
static int c_integration(void *arg) {
    (void)arg;
    int pids[2];
    for(unsigned i=0;i<2;i++) {
        pids[i]=kernel_thread(c_worker,(void *)(uintptr_t)i,0);
        assert(pids[i]>0);
    }
    for(unsigned i=0;i<110;i++) schedule();
    assert(c_counts[0]==100 && c_counts[1]==100);
    assert(find_proc(pids[0])->state==PROC_SLEEPING);
    assert(find_proc(pids[1])->state==PROC_SLEEPING);
    cprintf("[C] two workers x100, saved registers, private stacks, satp: PASS\n");
    for(unsigned enabled=0;enabled<2;enabled++) {
        if(enabled) intr_enable(); else intr_disable();
        struct proc_struct *self=current;
        int runs=self->runs;
        self->need_resched=1;
        schedule();
        assert(current==self && self->runs==runs+1 && !self->need_resched);
        assert(!!(read_csr(sstatus)&SSTATUS_SIE)==enabled);
    }
    cprintf("[C] sole runnable, scan wrap, SIE on/off preserved: PASS\n");
    intr_disable();
    current->state=PROC_SLEEPING;
    c_expect_idle=1;
    schedule();
    panic("fallback returned unexpectedly");
}
