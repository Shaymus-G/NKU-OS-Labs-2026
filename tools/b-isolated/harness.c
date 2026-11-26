/* Appended to proc.c ONLY in the temporary test tree by run.py.
 * These C-owned interfaces are deliberately simulated, not implementations. */
#include <sbi.h>
static long fail_at = -1, allocation_calls;
void *__real_kmalloc(size_t size);
struct Page *b_real_alloc_pages(size_t n);
void *__wrap_kmalloc(size_t size)
{
    if (fail_at >= 0 && allocation_calls++ == fail_at) return NULL;
    return __real_kmalloc(size);
}
struct Page *alloc_pages(size_t n)
{
    if (fail_at >= 0 && allocation_calls++ == fail_at) return NULL;
    return b_real_alloc_pages(n);
}

static int fork_error, schedule_calls, schedule_action, exit_seen;
static struct proc_struct *scheduled_child;
static uintptr_t exit_jump[5];
int do_fork(uint32_t flags, uintptr_t stack, struct trapframe *tf)
{
    assert(flags & CLONE_VM); // test double implements kernel threads only
    if (fork_error) return fork_error;
    struct proc_struct *p = alloc_proc();
    assert(p != NULL && setup_kstack(p) == 0);
    p->parent = current;
    copy_thread(p, stack, tf);
    p->pid = get_pid();
    hash_proc(p);
    set_links(p);
    wakeup_proc(p);
    return p->pid;
}
int do_exit(int code)
{
    exit_seen = code;
    __builtin_longjmp((void *)exit_jump, 1);
}
int do_yield(void) { current->need_resched = 1; return 0; }
int do_kill(int pid) { return -E_INVAL; }
void proc_run(struct proc_struct *p) { panic("test must not switch processes"); }
void switch_to(struct context *a, struct context *b) { panic("test must not switch contexts"); }
void schedule(void)
{
    schedule_calls++;
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    assert(current->state == PROC_SLEEPING && current->wait_state == WT_CHILD);
    assert(current->mm == NULL || !current->mm->mm_lock);
    if (schedule_action == 1) {
        scheduled_child->state = PROC_ZOMBIE;
        scheduled_child->exit_code = -12345;
    } else if (schedule_action == 2) current->flags |= PF_EXITING;
    else assert(schedule_action == 3); // one spurious wake, then real exit
    wakeup_proc(current);
    if (schedule_action == 3) schedule_action = 1;
}

static struct trapframe test_tf;
static unsigned char image[PGSIZE * 3];
static struct proghdr *image_ph(void) { return (void *)(image + sizeof(struct elfhdr)); }
static void make_image(void)
{
    memset(image, 0, sizeof(image));
    struct elfhdr *e = (void *)image;
    e->e_magic = ELF_MAGIC; e->e_elf[0] = 2; e->e_elf[1] = 1; e->e_elf[2] = 1;
    e->e_type = 2; e->e_machine = 243; e->e_version = 1;
    e->e_ehsize = sizeof(*e); e->e_phoff = sizeof(*e);
    e->e_phentsize = sizeof(struct proghdr); e->e_phnum = 2;
    e->e_entry = UTEXT + 0x123;
    struct proghdr *p = image_ph();
    p[0].p_type = ELF_PT_LOAD; p[0].p_flags = ELF_PF_R | ELF_PF_X;
    p[0].p_offset = 0x123; p[0].p_va = UTEXT + 0x123;
    p[0].p_filesz = PGSIZE + 30; p[0].p_memsz = PGSIZE * 2 + 77; p[0].p_align = PGSIZE;
    p[1].p_type = ELF_PT_LOAD; p[1].p_flags = ELF_PF_R | ELF_PF_W;
    p[1].p_offset = 0x2000; p[1].p_va = UTEXT + 0x4000;
    p[1].p_filesz = 17; p[1].p_memsz = 100; p[1].p_align = PGSIZE;
    for (unsigned i = 0; i < p[0].p_filesz; i++) image[p[0].p_offset + i] = (i % 251) + 1;
    memset(image + 0x2000, 0x5a, 17);
}
static void reset_mm(void)
{
    struct mm_struct *mm = current->mm;
    current->mm = NULL; current->pgdir = boot_pgdir_pa;
    lsatp(boot_pgdir_pa); flush_tlb();
    if (mm != NULL && mm_count_dec(mm) == 0) destroy_private_mm(mm);
    memset(&test_tf, 0, sizeof(test_tf)); test_tf.status = SSTATUS_SPP;
    current->tf = &test_tf;
}
static void check_loaded(void)
{
    struct mm_struct *mm = current->mm;
    assert(mm_count(mm) == 1 && current->pgdir == PADDR(mm->pgdir));
    assert((read_csr(satp) & (((uintptr_t)1 << 44) - 1)) == (current->pgdir >> PGSHIFT));
    assert(test_tf.epc == UTEXT + 0x123 && test_tf.gpr.sp == USTACKTOP);
    assert(test_tf.status == SSTATUS_SPIE);
    struct proghdr *ph = image_ph();
    for (int i = 0; i < 2; i++) {
        for (size_t n = 0; n < ph[i].p_memsz; n++) {
            uintptr_t va = ph[i].p_va + n;
            unsigned char *alias = page2kva(get_page(mm->pgdir, va, NULL));
            assert(alias[PGOFF(va)] == (n < ph[i].p_filesz ? image[ph[i].p_offset + n] : 0));
        }
    }
    pte_t *pte = get_pte(mm->pgdir, UTEXT, 0);
    assert((*pte & (PTE_U | PTE_R | PTE_X | PTE_W)) == (PTE_U | PTE_R | PTE_X));
    pte = get_pte(mm->pgdir, UTEXT + 0x4000, 0);
    assert((*pte & (PTE_U | PTE_R | PTE_X | PTE_W)) == (PTE_U | PTE_R | PTE_W));
    uintptr_t bottom = USTACKTOP - USTACKSIZE;
    assert(get_page(mm->pgdir, bottom, NULL) == NULL);
    assert(!user_mem_check(mm, bottom, sizeof(int), 1));
    for (uintptr_t va = bottom + PGSIZE; va < USTACKTOP; va += PGSIZE) {
        struct Page *p = get_page(mm->pgdir, va, &pte);
        assert(p && (*pte & (PTE_U | PTE_R | PTE_W | PTE_X)) == (PTE_U | PTE_R | PTE_W));
        for (unsigned j = 0; j < PGSIZE; j++) assert(((char *)page2kva(p))[j] == 0);
    }
}
static struct proc_struct *child_new(int code, bool zombie)
{
    struct proc_struct *p = alloc_proc(); assert(p && setup_kstack(p) == 0);
    p->parent = current; p->pid = get_pid(); hash_proc(p); set_links(p);
    p->state = zombie ? PROC_ZOMBIE : PROC_RUNNABLE; p->exit_code = code;
    return p;
}

void b_isolated_tests(void)
{
    intr_disable();
    proc_init(); // uses only the explicit do_fork test double above
    assert(current == idleproc && initproc->pid == 1 && nr_process == 2);
    assert(idleproc->tf == NULL && idleproc->mm == NULL && initproc->parent == idleproc);
    assert(initproc->exit_code == 0 && initproc->wait_state == 0 && initproc->cptr == NULL);
    assert((uintptr_t)initproc->tf == initproc->kstack + KSTACKSIZE - sizeof(test_tf));
    assert(((uintptr_t)initproc->tf & 15) == 0 && initproc->tf->gpr.sp == (uintptr_t)initproc->tf);
    assert(initproc->tf->gpr.s0 == (uintptr_t)init_main);
    struct trapframe tf = *initproc->tf;
    tf.status = 0; tf.gpr.sp = USTACKTOP - 64; tf.gpr.a0 = 123; tf.epc = UTEXT;
    copy_thread(initproc, 0, &tf);
    assert(initproc->tf->gpr.sp == tf.gpr.sp && initproc->tf->gpr.a0 == 0 && initproc->tf->epc == UTEXT);
    copy_thread(initproc, USTACKTOP - 128, &tf);
    assert(initproc->tf->gpr.sp == USTACKTOP - 128);
    fork_error = -E_NO_MEM; assert(kernel_thread(user_main, NULL, 0) == -E_NO_MEM); fork_error = 0;
    fail_at = 0; allocation_calls = 0; assert(alloc_proc() == NULL); fail_at = -1;
    struct proc_struct *failed = alloc_proc(); assert(failed != NULL);
    fail_at = 0; allocation_calls = 0;
    assert(setup_kstack(failed) == -E_NO_MEM && failed->kstack == 0);
    fail_at = -1; kfree(failed);
    current = initproc;
    tf.status = SSTATUS_SPP; copy_thread(initproc, 0, &tf);
    intr_enable();
    assert(kernel_execve("invalid", image, sizeof(image)) == -E_INVAL_ELF);
    assert(read_csr(sstatus) & SSTATUS_SIE);
    intr_disable(); current = idleproc;
    cprintf("[B isolated] PCB, proc_init and copy_thread PASS (fork double)\n");

    // Use a separate simulated parent, preserving idle/init lifetime rules.
    struct proc_struct parent;
    memset(&parent, 0, sizeof(parent)); parent.pid = 3000; parent.state = PROC_RUNNABLE;
    current = &parent; reset_mm(); make_image();
    // Warm up mm/VMA heap blocks before exact free-page comparisons.
    assert(do_execve("synthetic", 9, image, sizeof(image)) == 0); check_loaded(); reset_mm();
    size_t baseline = nr_free_pages();
    for (int i = 0; i < 16; i++) {
        assert(do_execve("synthetic", 9, image, sizeof(image)) == 0);
        check_loaded(); reset_mm(); assert(nr_free_pages() == baseline);
    }
    cprintf("[B isolated] ELF cross-page data, BSS, permissions, stack/guard, satp and 16 cleanups PASS\n");

    for (int i = 0; i < 15; i++) {
        make_image(); struct elfhdr *e = (void *)image; struct proghdr *p = image_ph();
        size_t size = sizeof(image);
        switch (i) {
        case 0: e->e_magic = 0; break;
        case 1: e->e_elf[0] = 1; break;
        case 2: e->e_machine = 0; break;
        case 3: size = sizeof(*e) - 1; break;
        case 4: e->e_phoff = (size_t)-8; break;
        case 5: e->e_phnum = 65535; break;
        case 6: p[0].p_filesz = p[0].p_memsz + 1; break;
        case 7: p[0].p_offset = (size_t)-1; break;
        case 8: p[0].p_memsz = (size_t)-1; break;
        case 9: p[0].p_va = USERBASE - 1; break;
        case 10: p[0].p_va = USTACKTOP - USTACKSIZE; break;
        case 11: p[1].p_va = UTEXT + PGSIZE; break;
        case 12: e->e_entry = UTEXT + 0x4000; break;
        case 13: p[0].p_align = 3; break;
        case 14: p[1].p_flags = ELF_PF_W; break;
        }
        struct trapframe before = test_tf;
        assert(do_execve("bad", 3, image, size) < 0);
        assert(current->mm == NULL && memcmp(&test_tf, &before, sizeof(before)) == 0);
        assert(nr_free_pages() == baseline);
    }
    make_image();
    assert(do_execve(NULL, 1, image, sizeof(image)) == -E_INVAL);
    assert(do_execve("x", 16, image, sizeof(image)) == -E_INVAL);
    assert(do_execve((char *)UTEXT, 1, image, sizeof(image)) == -E_INVAL);
    assert(do_execve("x", 1, (void *)(uintptr_t)-8, 64) == -E_INVAL_ELF);
    cprintf("[B isolated] 15 malformed ELF variants and invalid exec arguments PASS\n");

    long failures = 0;
    for (long n = 0; n < 400; n++) {
        fail_at = n; allocation_calls = 0;
        int ret = do_execve("oom", 3, image, sizeof(image));
        fail_at = -1;
        if (ret == 0) { cprintf("[B isolated] sweep first success n=%ld calls=%ld\n", n, allocation_calls); reset_mm(); assert(nr_free_pages() == baseline); break; }
        assert(ret == -E_NO_MEM && current->mm == NULL && test_tf.status == SSTATUS_SPP);
        assert(nr_free_pages() == baseline); failures++;
    }
    assert(failures > USTACKPAGE);
    cprintf("[B isolated] allocation failure sweep PASS: %ld failing positions\n", failures);

    list_entry_t held;
    list_init(&held);
    struct Page *held_page;
    size_t held_count = 0;
    while ((held_page = alloc_page()) != NULL) {
        list_add(&held, &held_page->page_link); held_count++;
    }
    assert(nr_free_pages() == 0 && held_count > 0);
    assert(do_execve("exhausted", 9, image, sizeof(image)) == -E_NO_MEM);
    while (!list_empty(&held)) {
        list_entry_t *le = list_next(&held);
        list_del(le); free_page(le2page(le, page_link));
    }
    assert(nr_free_pages() == baseline && current->mm == NULL);
    cprintf("[B isolated] real page exhaustion and recovery PASS: %lu held pages\n", held_count);

    // Preserve a live sibling PT outside the requested cleanup range.
    struct mm_struct *range_mm = mm_create(); assert(range_mm && setup_pgdir(range_mm) == 0);
    assert(mm_map(range_mm, UTEXT, PGSIZE, VM_READ | VM_WRITE, NULL) == 0);
    assert(mm_map(range_mm, UTEXT + PTSIZE, PGSIZE, VM_READ | VM_WRITE, NULL) == 0);
    assert(pgdir_alloc_page(range_mm->pgdir, UTEXT, PTE_U | PTE_R | PTE_W));
    struct Page *survivor = pgdir_alloc_page(range_mm->pgdir, UTEXT + PTSIZE, PTE_U | PTE_R | PTE_W);
    assert(survivor != NULL);
    unmap_range(range_mm->pgdir, UTEXT, UTEXT + PGSIZE);
    exit_range(range_mm->pgdir, UTEXT, UTEXT + PGSIZE);
    assert(get_page(range_mm->pgdir, UTEXT + PTSIZE, NULL) == survivor);
    destroy_private_mm(range_mm); assert(nr_free_pages() == baseline);
    cprintf("[B isolated] partial exit_range preserves sibling PT and frees empty ancestors PASS\n");

    // Actual embedded ELFs are loaded and inspected, never executed here.
#define CHECK_REAL(x) do { \
    extern unsigned char _binary_obj___user_##x##_out_start[], _binary_obj___user_##x##_out_size[]; \
    assert(do_execve(#x, sizeof(#x)-1, _binary_obj___user_##x##_out_start, \
           (size_t)_binary_obj___user_##x##_out_size) == 0); \
    assert(!(test_tf.status & SSTATUS_SPP)); reset_mm(); \
    assert(nr_free_pages() == baseline); \
} while (0)
    CHECK_REAL(hello); CHECK_REAL(testbss); CHECK_REAL(exit);
    cprintf("[B isolated] actual hello/testbss/exit ELF loading PASS (no U-mode execution)\n");

    int code = 0, count = nr_process;
    struct proc_struct *a = child_new(-99, 1), *b = child_new(42, 1);
    int apid = a->pid, bpid = b->pid;
    assert(do_wait(-1, NULL) == -E_BAD_PROC && do_wait(MAX_PID, NULL) == -E_BAD_PROC);
    assert(do_wait(initproc->pid, NULL) == -E_BAD_PROC);
    assert(do_wait(apid, (int *)USTACKTOP) == -E_INVAL);
    assert(do_wait(apid, &code) == 0 && code == -99 && find_proc(apid) == NULL);
    assert(do_wait(apid, NULL) == -E_BAD_PROC);
    assert(do_wait(0, &code) == 0 && code == 42 && find_proc(bpid) == NULL);
    assert(do_wait(0, NULL) == -E_BAD_PROC && nr_process == count);
    a = child_new(1, 0); scheduled_child = a; apid = a->pid;
    schedule_action = 3; schedule_calls = 0;
    assert(do_wait(apid, &code) == 0 && code == -12345 && schedule_calls == 2);
    assert(parent.state == PROC_RUNNABLE && parent.wait_state == 0);
    assert(!(read_csr(sstatus) & SSTATUS_SIE));
    a = child_new(0, 0); scheduled_child = a; schedule_action = 1;
    intr_enable();
    assert(do_wait(a->pid, NULL) == 0 && (read_csr(sstatus) & SSTATUS_SIE));
    intr_disable();
    a = child_new(0, 0); scheduled_child = a; schedule_action = 2;
    if (__builtin_setjmp((void *)exit_jump) == 0) {
        do_wait(a->pid, NULL); panic("kill did not exit wait");
    }
    assert(exit_seen == -E_KILLED); parent.flags = 0;
    a->state = PROC_ZOMBIE; assert(do_wait(a->pid, NULL) == 0);
    cprintf("[B isolated] wait selection, codes, reaping, spurious wake and kill PASS (schedule/exit doubles)\n");

    // Exercise real user-pointer translations and transactional user exec.
    make_image(); assert(do_execve("first", 5, image, sizeof(image)) == 0);
    struct mm_struct *old = current->mm;
    uintptr_t source = 0x1000000;
    assert(mm_map(old, source, sizeof(image) + PGSIZE, VM_READ | VM_WRITE, NULL) == 0);
    for (uintptr_t va = source; va < source + sizeof(image) + PGSIZE; va += PGSIZE)
        assert(pgdir_alloc_page(old->pgdir, va, PTE_U | PTE_R | PTE_W) != NULL);
    assert(proc_mem_copy(old, image, source, sizeof(image), 1));
    char uname[] = "user-exec";
    uintptr_t nameva = source + sizeof(image);
    assert(proc_mem_copy(old, uname, nameva, sizeof(uname), 1));
    a = child_new(0x12345678, 1); apid = a->pid;
    assert(do_wait(apid, (int *)(UTEXT + 0x123)) == -E_INVAL);
    pte_t *write_pte = get_pte(old->pgdir, source, 0);
    pte_t saved_pte = *write_pte;
    *write_pte &= ~PTE_W;
    assert(do_wait(apid, (int *)source) == -E_INVAL && find_proc(apid) == a);
    *write_pte = saved_pte;
    pte_t *next_pte = get_pte(old->pgdir, source + PGSIZE, 0);
    saved_pte = *next_pte; *next_pte = 0;
    unsigned short prior, after;
    assert(proc_mem_copy(old, &prior, source + PGSIZE - 2, sizeof(prior), 0));
    assert(do_wait(apid, (int *)(source + PGSIZE - 2)) == -E_INVAL && find_proc(apid) == a);
    assert(proc_mem_copy(old, &after, source + PGSIZE - 2, sizeof(after), 0) && prior == after);
    *next_pte = saved_pte;
    assert(do_wait(apid, (int *)(source + PGSIZE - 2)) == 0);
    code = 0; assert(proc_mem_copy(old, &code, source + PGSIZE - 2, sizeof(code), 0));
    assert(code == 0x12345678);
    // Restore the overwritten image bytes after the cross-page wait store.
    assert(proc_mem_copy(old, image, source, sizeof(image), 1));
    assert(do_execve("kernel-name", 11, image, sizeof(image)) == -E_INVAL);
    assert(do_execve((void *)nameva, 9, (void *)source, (size_t)-1) < 0 && current->mm == old);
    struct trapframe before = test_tf;
    fail_at = 4; allocation_calls = 0;
    assert(do_execve((void *)nameva, 9, (void *)source, sizeof(image)) == -E_NO_MEM);
    fail_at = -1;
    assert(current->mm == old && memcmp(&test_tf, &before, sizeof(before)) == 0);
    // Keep a simulated sharing reference; exec must detach, not destroy it.
    mm_count_inc(old);
    assert(do_execve((void *)nameva, 9, (void *)source, sizeof(image)) == 0);
    assert(current->mm != old && mm_count(old) == 1 && strcmp(current->name, uname) == 0);
    assert(mm_count_dec(old) == 0); destroy_private_mm(old);
    // The same user-source exec with no sharing must reclaim its old mm itself.
    old = current->mm;
    assert(mm_map(old, source, sizeof(image) + PGSIZE, VM_READ | VM_WRITE, NULL) == 0);
    for (uintptr_t va = source; va < source + sizeof(image) + PGSIZE; va += PGSIZE)
        assert(pgdir_alloc_page(old->pgdir, va, PTE_U | PTE_R | PTE_W) != NULL);
    assert(proc_mem_copy(old, image, source, sizeof(image), 1));
    assert(proc_mem_copy(old, uname, nameva, sizeof(uname), 1));
    assert(do_execve((void *)nameva, 9, (void *)source, sizeof(image)) == 0);
    assert(current->mm != old);
    check_loaded(); reset_mm(); assert(nr_free_pages() == baseline);
    cprintf("[B isolated] user pointers, cross-page wait, exec rollback/source lifetime/shared mm PASS\n");
    cprintf("[B isolated] ALL CHECKS PASS; no real fork/schedule/exit or U-mode run claimed\n");
    sbi_shutdown();
    for (;;) ;
}
