/* Test-only: appended to proc.c in a temporary copy, never built by normal make. */
#include <sbi.h>
#include <test_lab4.h>

static int b_fail_pcb, b_fail_stack, b_fail_mm;
static int b_live_pcb, b_live_stack;
static int b_pcb_allocs, b_pcb_frees;
static uint32_t b_last_clone_flags;

static void *b_test_kmalloc(size_t size)
{
    if (b_fail_pcb) return NULL;
    void *p = kmalloc(size);
    if (p != NULL) {
        memset(p, 0xa5, size); /* Detect missing PCB initialization. */
        b_live_pcb++;
        b_pcb_allocs++;
    }
    return p;
}
static void b_test_kfree(void *p)
{
    assert(p != NULL && b_live_pcb > 0);
    b_live_pcb--;
    b_pcb_frees++;
    kfree(p);
}
static struct Page *b_test_stack_alloc(size_t n)
{
    assert(n == KSTACKPAGE);
    if (b_fail_stack) return NULL;
    struct Page *p = alloc_pages(n);
    if (p != NULL) b_live_stack++;
    return p;
}
static void b_test_stack_free(struct Page *p, size_t n)
{
    assert(b_live_stack > 0 && n == KSTACKPAGE);
    b_live_stack--;
    free_pages(p, n);
}
static int b_test_copy_mm(uint32_t flags, struct proc_struct *proc)
{
    b_last_clone_flags = flags;
    if (b_fail_mm) return -E_NO_MEM;
    return copy_mm(flags, proc);
}

// The debugger stops here immediately after proc_init; no test thread has run.
void __attribute__((noinline, noclone)) b_create_checkpoint(void)
{
    asm volatile("nop" ::: "memory");
}
static int b_dummy(void *arg)
{
    panic("B creation-only test must not execute a thread");
}
static void b_check_registry(void)
{
    int listed = 0, hashed = 0;
    list_entry_t *le;
    assert(list_empty(&idleproc->list_link));
    assert(list_empty(&idleproc->hash_link));
    for (le = list_next(&proc_list); le != &proc_list; le = list_next(le)) {
        struct proc_struct *p = le2proc(le, list_link);
        assert(p != idleproc && p->pid > 0 && p->pid < MAX_PID);
        assert(find_proc(p->pid) == p && p->state == PROC_RUNNABLE);
        assert(p->list_link.next->prev == &p->list_link);
        assert(p->list_link.prev->next == &p->list_link);
        list_entry_t *other;
        for (other = list_next(le); other != &proc_list; other = list_next(other)) {
            struct proc_struct *q = le2proc(other, list_link);
            assert(q->pid != p->pid);
            assert(p->kstack + KSTACKSIZE <= q->kstack ||
                   q->kstack + KSTACKSIZE <= p->kstack);
        }
        listed++;
    }
    for (int i = 0; i < HASH_LIST_SIZE; i++) {
        for (le = list_next(&hash_list[i]); le != &hash_list[i]; le = list_next(le)) {
            struct proc_struct *p = le2proc(le, hash_link);
            assert(pid_hashfn(p->pid) == i && find_proc(p->pid) == p);
            hashed++;
        }
    }
    assert(listed == nr_process - 1 && hashed == listed);
}
static void b_check_new_thread(struct proc_struct *p, void *arg)
{
    assert(p != NULL && p->parent == current && p->mm == NULL);
    assert(p->pgdir == boot_pgdir_pa && p->runs == 0 && p->flags == 0);
    assert(!p->need_resched && p->state == PROC_RUNNABLE);
    assert(p->tf == (void *)(p->kstack + KSTACKSIZE - sizeof(struct trapframe)));
    assert(((uintptr_t)p->tf & 15) == 0);
    assert(p->context.ra == (uintptr_t)forkret && p->context.sp == (uintptr_t)p->tf);
    uintptr_t *ctx = (uintptr_t *)&p->context;
    for (int i = 2; i < 14; i++) assert(ctx[i] == 0);
    struct trapframe expected;
    memset(&expected, 0, sizeof(expected));
    expected.gpr.s0 = (uintptr_t)b_dummy;
    expected.gpr.s1 = (uintptr_t)arg;
    expected.gpr.sp = (uintptr_t)p->tf;
    expected.epc = (uintptr_t)kernel_thread_entry;
    expected.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    assert(memcmp(p->tf, &expected, sizeof(expected)) == 0);
}
static void b_delete_test_thread(struct proc_struct *p)
{
    assert(p != idleproc && p != initproc && p != current);
    list_del_init(&p->list_link);
    list_del_init(&p->hash_link);
    nr_process--;
    put_kstack(p);
    b_test_kfree(p);
}
static void b_check_failure(int mode)
{
    int count = nr_process, pcbs = b_live_pcb, stacks = b_live_stack;
    size_t pages_before = nr_free_pages();
    int allocs = b_pcb_allocs, frees = b_pcb_frees;
    list_entry_t *first = proc_list.next, *last = proc_list.prev;
    b_fail_pcb = mode == 1;
    b_fail_stack = mode == 2;
    b_fail_mm = mode == 3;
    assert(kernel_thread(b_dummy, (void *)0x1234, 0) == -E_NO_MEM);
    b_fail_pcb = b_fail_stack = b_fail_mm = 0;
    assert(nr_process == count && b_live_pcb == pcbs && b_live_stack == stacks);
    assert(nr_free_pages() == pages_before);
    assert(proc_list.next == first && proc_list.prev == last);
    assert(b_pcb_allocs - allocs == (mode == 1 ? 0 : 1));
    assert(b_pcb_frees - frees == (mode == 1 ? 0 : 1));
    b_check_registry();
}
void b_create_tests(void)
{
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    assert(sizeof(struct context) == 14 * sizeof(uintptr_t));
    assert(sizeof(struct trapframe) == 36 * sizeof(uintptr_t));
    test_lab4_process_create(); /* Original teacher assertions, unmodified. */
    assert(nr_process == 2 && find_proc(1) == initproc && find_proc(0) == NULL);
    assert(strcmp(idleproc->name, "idle") == 0 && strcmp(initproc->name, "init") == 0);
    assert(idleproc->need_resched && idleproc->pgdir == boot_pgdir_pa);
    assert(initproc->tf->gpr.s0 == (uintptr_t)init_main);
    assert(strcmp((char *)initproc->tf->gpr.s1, "Hello world!!") == 0);
    b_check_registry();
    cprintf("[B Create] idle/init and original teacher assertions PASS\n");

    struct proc_struct *blank = alloc_proc();
    assert(blank != NULL && blank->state == PROC_UNINIT && blank->pid == -1);
    assert(blank->pgdir == boot_pgdir_pa && list_empty(&blank->list_link));
    assert(list_empty(&blank->hash_link));
    struct proc_struct expected;
    memset(&expected, 0, sizeof(expected));
    expected.pid = -1;
    expected.pgdir = boot_pgdir_pa;
    expected.list_link = blank->list_link;
    expected.hash_link = blank->hash_link;
    assert(memcmp(blank, &expected, sizeof(expected)) == 0);
    b_test_kfree(blank);
    cprintf("[B Create] poisoned PCB fully initialized PASS\n");

    struct proc_struct *threads[16];
    int count_before = nr_process;
    for (int i = 0; i < 16; i++) {
        void *arg = (void *)(uintptr_t)(0x1000 + i);
        int pid = kernel_thread(b_dummy, arg, 0);
        assert(pid > 1 && (b_last_clone_flags & CLONE_VM));
        threads[i] = find_proc(pid);
        b_check_new_thread(threads[i], arg);
    }
    b_check_registry();
    for (int i = 0; i < 16; i++) b_delete_test_thread(threads[i]);
    assert(nr_process == count_before);
    b_check_registry();
    cprintf("[B Create] 16 unique PIDs, parents, disjoint stacks, frames, registry PASS\n");

    struct trapframe source;
    memset(&source, 0x5a, sizeof(source));
    struct trapframe original = source;
    int pid = do_fork(0, 0x12345000, &source);
    struct proc_struct *p = find_proc(pid);
    assert(pid > 0 && p != NULL);
    assert(p->tf->gpr.sp == 0x12345000 && p->tf->gpr.a0 == 0);
    original.gpr.sp = 0x12345000;
    original.gpr.a0 = 0;
    assert(memcmp(p->tf, &original, sizeof(original)) == 0);
    assert(source.gpr.sp == (uintptr_t)0x5a5a5a5a5a5a5a5aULL);
    b_delete_test_thread(p);
    cprintf("[B Create] explicit stack, child return zero, source frame unchanged PASS\n");

    for (int i = 0; i < 64; i++) {
        b_check_failure(1);
        b_check_failure(2);
        b_check_failure(3);
    }
    cprintf("[B Create] injected PCB/stack/mm failures x64: no published child or leak PASS\n");

    // Exhaust real PMM pages without assuming that the free range is contiguous.
    size_t reserve_count = nr_free_pages(), held = 0;
    list_entry_t reserved;
    list_init(&reserved);
    struct Page *page;
    while ((page = alloc_page()) != NULL) {
        list_add(&reserved, &page->pra_page_link);
        held++;
    }
    assert(held == reserve_count && nr_free_pages() == 0);
    b_check_failure(0); /* No injected failure: setup_kstack sees zero free pages. */
    while (!list_empty(&reserved)) {
        list_entry_t *le = list_next(&reserved);
        page = to_struct(le, struct Page, pra_page_link);
        list_del_init(le);
        free_page(page);
    }
    assert(nr_free_pages() == reserve_count);
    cprintf("[B Create] real physical-page exhaustion and rollback PASS\n");

    int real_count = nr_process, allocations_before = b_pcb_allocs;
    nr_process = MAX_PROCESS; /* Synthetic limit fixture; restored before registry checks. */
    assert(kernel_thread(b_dummy, NULL, 0) == -E_NO_FREE_PROC);
    assert(b_pcb_allocs == allocations_before);
    nr_process = real_count;
    b_check_registry();
    cprintf("[B Create] process-count limit rejected before allocation PASS\n");

    // No timer was configured. Exercise both incoming SIE states without scheduling.
    intr_enable();
    pid = kernel_thread(b_dummy, (void *)0x8888, 0);
    assert(read_csr(sstatus) & SSTATUS_SIE);
    p = find_proc(pid);
    b_check_new_thread(p, (void *)0x8888);
    b_delete_test_thread(p);
    b_check_failure(2);
    assert(read_csr(sstatus) & SSTATUS_SIE);
    intr_disable();
    b_check_failure(3);
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    assert(current == idleproc && nr_process == 2);
    assert(b_live_pcb == 2 && b_live_stack == 1);
    b_check_registry();
    cprintf("[B Create] incoming interrupt state preserved on success/failure PASS\n");
    cprintf("[B Create] ALL PASS (creation only; no thread execution or scheduling)\n");
    sbi_shutdown();
    panic("B test shutdown unexpectedly returned");
}
