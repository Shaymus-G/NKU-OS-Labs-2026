/* Included in proc.c only in the isolated verification build. */
static const uintptr_t c_base = USERBASE;
static const uintptr_t c_readonly = USERBASE + PTSIZE;

static struct mm_struct *
c_new_mm(void)
{
    struct mm_struct *mm = mm_create();
    assert(mm != NULL && setup_pgdir(mm) == 0);
    return mm;
}

static void
c_reserve_pages(list_entry_t *held)
{
    list_init(held);
    struct Page *page;
    while ((page = alloc_page()) != NULL) list_add(held, &page->page_link);
    assert(nr_free_pages() == 0);
}

static void
c_release_page(list_entry_t *held)
{
    list_entry_t *entry = list_next(held);
    assert(entry != held);
    list_del(entry);
    free_page(le2page(entry, page_link));
}

static int
c_shared_thread(void *arg)
{
    struct mm_struct *mm = arg;
    assert(current->mm == mm && current->pgdir == PADDR(mm->pgdir));
    assert(mm_count(mm) == 2);
    struct Page *page = get_page(mm->pgdir, c_base, NULL);
    *(unsigned char *)page2kva(page) = 41;
    return 83;
}

static void
c_vm_tests(void)
{
    size_t before = nr_free_pages();
    struct mm_struct *parent = c_new_mm();
    assert(mm_map(parent, c_base, 2 * PGSIZE, VM_READ | VM_WRITE, NULL) == 0);
    assert(mm_map(parent, c_readonly, PGSIZE, VM_READ, NULL) == 0);
    struct Page *data = pgdir_alloc_page(parent->pgdir, c_base, PTE_U | PTE_R | PTE_W);
    struct Page *ro = pgdir_alloc_page(parent->pgdir, c_readonly, PTE_U | PTE_R);
    assert(data != NULL && ro != NULL);
    memset(page2kva(data), 18, PGSIZE);
    memset(page2kva(ro), 52, PGSIZE);

    // Verify real CLONE_VM creation, scheduling, writes and reference release.
    set_mm_count(parent, 1);
    current->mm = parent;
    current->pgdir = PADDR(parent->pgdir);
    lsatp(current->pgdir);
    flush_tlb();
    int pid = kernel_thread(c_shared_thread, parent, 0);
    assert(pid > 0 && mm_count(parent) == 2);
    assert(do_wait(pid, NULL) == 0 && mm_count(parent) == 1);
    assert(*(unsigned char *)page2kva(data) == 41);
    current->mm = NULL;
    current->pgdir = boot_pgdir_pa;
    lsatp(boot_pgdir_pa);
    flush_tlb();
    assert(mm_count_dec(parent) == 0);
    cprintf("C VM: CLONE_VM visibility and refcount PASS\n");

    pte_t *parent_pte = get_pte(parent->pgdir, c_base, 0);
    pte_t original = *parent_pte;
    struct mm_struct *failed = c_new_mm();
    assert(get_pte(failed->pgdir, c_base, 1) != NULL);
    list_entry_t held;
    c_reserve_pages(&held);
#ifdef LAB5_EAGER_FORK
    c_release_page(&held);  // The first leaf needs a private physical page.
#endif
    lock_mm(parent);
    assert(dup_mmap(failed, parent) == -E_NO_MEM);
    unlock_mm(parent);
    assert(get_page(failed->pgdir, c_base, NULL) != NULL);
    assert(get_page(failed->pgdir, c_readonly, NULL) == NULL);
    assert(*parent_pte == original);
    destroy_private_mm(failed);
    assert(page_ref(data) == 1 && page_ref(ro) == 1);
    while (!list_empty(&held)) c_release_page(&held);
    cprintf("C VM: real page exhaustion and partial fork rollback PASS\n");

    struct mm_struct *child = c_new_mm();
    lock_mm(parent);
    assert(dup_mmap(child, parent) == 0);
    unlock_mm(parent);
    pte_t *child_pte;
    struct Page *mapped = get_page(child->pgdir, c_base, &child_pte);
    assert(mapped != NULL && ((unsigned char *)page2kva(mapped))[PGSIZE - 1] == 18);
    assert(get_page(child->pgdir, c_base + PGSIZE, NULL) == NULL);
    pte_t *readonly_pte = get_pte(child->pgdir, c_readonly, 0);
    assert(readonly_pte != NULL && (*readonly_pte & (PTE_W | PTE_COW)) == 0);
    lock_mm(child);
    assert(resolve_cow_write(child, c_readonly) == -E_FAULT);
    assert(resolve_cow_write(child, c_base + PGSIZE) == -E_FAULT);
    unlock_mm(child);
#ifdef LAB5_EAGER_FORK
    assert(mapped != data && page_ref(data) == 1 && *parent_pte == original);
    assert((*child_pte & (PTE_W | PTE_COW)) == PTE_W);
    cprintf("C VM: eager copy, read-only leaf and unmapped hole PASS\n");
#else
    assert(mapped == data && page_ref(data) == 2 && page_ref(ro) == 2);
    assert((*parent_pte & (PTE_W | PTE_COW)) == PTE_COW);
    assert((*child_pte & (PTE_W | PTE_COW)) == PTE_COW);
    c_reserve_pages(&held);
    pte_t shared_pte = *child_pte;
    lock_mm(child);
    assert(resolve_cow_write(child, c_base) == -E_NO_MEM);
    assert(*child_pte == shared_pte && page_ref(data) == 2);
    c_release_page(&held);
    assert(resolve_cow_write(child, c_base) == 0);
    unlock_mm(child);
    assert(pte2page(*child_pte) != data && page_ref(data) == 1);
    assert(nr_free_pages() == 0);
    lock_mm(parent);
    assert(resolve_cow_write(parent, c_base) == 0);
    unlock_mm(parent);
    assert(pte2page(*parent_pte) == data && nr_free_pages() == 0);
    assert((*parent_pte & (PTE_W | PTE_COW)) == PTE_W);
    while (!list_empty(&held)) c_release_page(&held);
    cprintf("C VM: shared PPNs, denied read-only/absent writes, COW OOM, split and re-use PASS\n");
#endif
    destroy_private_mm(child);
    destroy_private_mm(parent);
    assert(nr_free_pages() == before);
    cprintf("C VM: free pages restored %lu -> %lu PASS\n", before, nr_free_pages());
}
