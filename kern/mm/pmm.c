#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <dtb.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// The kernel image is mapped at VA=KERNBASE and PA=info.base
uint_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
pde_t *boot_pgdir_va = NULL;
// physical address of boot-time page directory
uintptr_t boot_pgdir_pa;

// physical memory management
const struct pmm_manager *pmm_manager;

static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void)
{
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n)
{
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n)
{
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void)
{
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

//lab2 请补充你在lab2中的代码
static void
page_init(void)
{
    extern char end[];
    const uint64_t mem_base = get_memory_base();
    const uint64_t mem_size = get_memory_size();
    const uint64_t reported_end = mem_base + mem_size;
    if (mem_base != DRAM_BASE || mem_size == 0 || reported_end < mem_base) {
        panic("page_init: invalid DTB memory range");
    }

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    const uintptr_t limit = PADDR(KERNTOP);
    uintptr_t managed_end = reported_end < limit ? reported_end : limit;
    managed_end = ROUNDDOWN(managed_end, PGSIZE);
    if (managed_end <= PADDR(KERNBASE)) {
        panic("page_init: insufficient memory for kernel");
    }
    npage = PPN(managed_end);

    const uintptr_t pages_va = ROUNDUP((uintptr_t)end, PGSIZE);
    const uintptr_t pages_pa = PADDR(pages_va);
    const size_t count = npage - nbase;
    if (pages_pa >= managed_end ||
        count > (managed_end - pages_pa) / sizeof(struct Page)) {
        panic("page_init: insufficient memory for page descriptors");
    }

    const uintptr_t free_begin =
        ROUNDUP(pages_pa + count * sizeof(struct Page), PGSIZE);
    if (free_begin >= managed_end) {
        panic("page_init: no usable physical pages");
    }

    pages = (struct Page *)pages_va;
    for (size_t i = 0; i < count; i++) {
        struct Page *p = &pages[i];
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
        list_init(&p->pra_page_link);
        p->pra_vaddr = 0;
        SetPageReserved(p);
    }
    const size_t free_count = (managed_end - free_begin) / PGSIZE;
    init_memmap(pa2page(free_begin), free_count);
    cprintf("PMM: DRAM [0x%lx, 0x%lx), managed end 0x%lx\n",
            (unsigned long)mem_base, (unsigned long)reported_end,
            (unsigned long)managed_end);
    cprintf("PMM: %lu descriptors, sizeof(Page)=%lu, free=[0x%lx, 0x%lx), %lu pages\n",
            (unsigned long)count, (unsigned long)sizeof(struct Page),
            (unsigned long)free_begin, (unsigned long)managed_end,
            (unsigned long)free_count);
}

// boot_map_segment - setup&enable the paging mechanism
// parameters
//  la:   linear address of this memory need to map (after x86 segment map)
//  size: memory size
//  pa:   physical address of this memory
//  perm: permission of this memory
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm)
{
    assert(PGOFF(la) == PGOFF(pa));
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE)
    {
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
    }
}

// boot_alloc_page - allocate one page using pmm->alloc_pages(1)
// return value: the kernel virtual address of this allocated page
// note: this function is used to get the memory for PDT(Page Directory
// Table)&PT(Page Table)
static void *boot_alloc_page(void)
{
    struct Page *p = alloc_page();
    if (p == NULL)
    {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

// pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup
// paging mechanism
//         - check the correctness of pmm & paging mechanism, print PDT&PT
void pmm_init(void)
{
    // We need to alloc/free the physical memory (granularity is 4KB or other
    // size).
    // So a framework of physical memory manager (struct pmm_manager)is defined
    // in pmm.h
    // First we should init a physical memory manager(pmm) based on the
    // framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a
    // pmm
    check_alloc_page();

    // create boot_pgdir, an initial page directory(Page Directory Table, PDT)
    extern char boot_page_table_sv39[];
    boot_pgdir_va = (pte_t *)boot_page_table_sv39;
    boot_pgdir_pa = PADDR(boot_pgdir_va);

    check_pgdir();

    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    // now the basic virtual memory map(see memalyout.h) is established.
    // check the correctness of the basic virtual memory map.
    check_boot_pgdir();

    kmalloc_init();
}

// get_pte - get pte and return the kernel virtual address of this pte for la
//        - if the PT contians this pte didn't exist, alloc a page for PT
// parameter:
//  pgdir:  the kernel virtual base address of PDT
//  la:     the linear address need to map
//  create: a logical value to decide if alloc a page for PT
// return vaule: the kernel virtual address of this pte
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }

    pde_t *pdep0 = &((pde_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    if (!(*pdep0 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// get_page - get related Page struct for linear address la using PDT pgdir
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL)
    {
        *ptep_store = ptep;
    }
    if (ptep != NULL && *ptep & PTE_V)
    {
        return pte2page(*ptep);
    }
    return NULL;
}

// page_remove_pte - free an Page sturct which is related linear address la
//                - and clean(invalidate) pte which is related linear address la
// note: PT is changed, so the TLB need to be invalidate
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    if (*ptep & PTE_V)
    {
        struct Page *page = pte2page(*ptep);
        page_ref_dec(page);
        if (page_ref(page) == 0)
        {
            free_page(page);
        }
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}

void unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));

    do
    {
        pte_t *ptep = get_pte(pgdir, start, 0);
        if (ptep == NULL)
        {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue;
        }
        if (*ptep != 0)
        {
            page_remove_pte(pgdir, start, ptep);
        }
        start += PGSIZE;
    } while (start != 0 && start < end);
}

// B 2412559: reclaim a directory only after checking ALL of its entries.
static bool page_table_empty(pde_t *table)
{
    for (unsigned i = 0; i < NPTEENTRY; i++)
        if (table[i] & PTE_V) return 0;
    return 1;
}

void exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));
    for (uintptr_t va = ROUNDDOWN(start, PTSIZE); va < end; va += PTSIZE) {
        pde_t *root = &pgdir[PDX1(va)];
        if (!(*root & PTE_V)) continue;
        // User address spaces in this lab use 4 KiB leaves only.
        assert(!(*root & (PTE_R | PTE_W | PTE_X)));
        pde_t *middle = page2kva(pde2page(*root));
        pde_t *entry = &middle[PDX0(va)];
        if (*entry & PTE_V) {
            assert(!(*entry & (PTE_R | PTE_W | PTE_X)));
            pte_t *leaf = page2kva(pde2page(*entry));
            if (page_table_empty(leaf)) {
                free_page(pde2page(*entry));
                *entry = 0;
            }
        }
        if (page_table_empty(middle)) {
            free_page(pde2page(*root));
            *root = 0;
        }
    }
}


//lab5 function3 2412456
int copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end, bool share)
{
    assert(start <= end && start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));
    while (start < end) {
        pte_t *pte = get_pte(from, start, 0);
        if (pte == NULL) {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue;
        }
        if (*pte & PTE_V) {
            const uint32_t perm = *pte & (PTE_U | PTE_R | PTE_W | PTE_X);
            struct Page *source = pte2page(*pte), *page = source;
            if (!share) {
                if ((page = alloc_page()) == NULL) return -E_NO_MEM;
                memcpy(page2kva(page), page2kva(source), PGSIZE);
            }
            int ret = page_insert(to, page, start, perm);
            if (ret != 0) {
                if (!share) free_page(page);
                return ret;
            }
        }
        start += PGSIZE;
    }
    return 0;
}


//lab4 请补充你在lab4中的代码
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }

    struct Page *old = (*ptep & PTE_V) ? pte2page(*ptep) : NULL;
    /* In this lab, perm=0 denotes a readable leaf, not a non-leaf. */
    if ((perm & PTE_W) || !(perm & (PTE_R | PTE_W | PTE_X))) {
        perm |= PTE_R;
    }

    if (old != page) {
        page_ref_inc(page);
    }
    *ptep = pte_create(page2ppn(page), perm);
    tlb_invalidate(pgdir, la);

    if (old != NULL && old != page && page_ref_dec(old) == 0) {
        free_page(old);
    }
    return 0;
}

void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep == NULL || !(*ptep & PTE_V)) {
        return;
    }

    struct Page *page = pte2page(*ptep);
    *ptep = 0;
    tlb_invalidate(pgdir, la);
    if (page_ref_dec(page) == 0) {
        free_page(page);
    }
}


// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    asm volatile("sfence.vma %0" : : "r"(la));
}

// pgdir_alloc_page - call alloc_page & page_insert functions to
//                  - allocate a page size memory & setup an addr map
//                  - pa<->la with linear address la and the PDT pgdir
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm)
{
    struct Page *page = alloc_page();
    if (page != NULL)
    {
        if (page_insert(pgdir, page, la, perm) != 0)
        {
            free_page(page);
            return NULL;
        }
        // swap_map_swappable(check_mm_struct, la, page, 0);
        page->pra_vaddr = la;
        assert(page_ref(page) == 1);
        // cprintf("get No. %d  page: pra_vaddr %x, pra_link.prev %x,
        // pra_link_next %x in pgdir_alloc_page\n", (page-pages),
        // page->pra_vaddr,page->pra_page_link.prev,
        // page->pra_page_link.next);
    }

    return page;
}

static void check_alloc_page(void)
{
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store = nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(boot_pgdir_va[0] & PTE_U);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store = nr_free_pages();

    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE)
    {
        assert((ptep = get_pte(boot_pgdir_va, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }

    assert(boot_pgdir_va[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}

// perm2str - use string 'u,r,w,-' to present the permission
static const char *perm2str(int perm)
{
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

// get_pgtable_items - In [left, right] range of PDT or PT, find a continuous
// linear addr space
//                  - (left_store*X_SIZE~right_store*X_SIZE) for PDT or PT
//                  - X_SIZE=PTSIZE=4M, if PDT; X_SIZE=PGSIZE=4K, if PT
// paramemters:
//  left:        no use ???
//  right:       the high side of table's range
//  start:       the low side of table's range
//  table:       the beginning addr of table
//  left_store:  the pointer of the high side of table's next range
//  right_store: the pointer of the low side of table's next range
//  return value: 0 - not a invalid item range, perm - a valid item range with
//  perm permission
static int get_pgtable_items(size_t left, size_t right, size_t start,
                             uintptr_t *table, size_t *left_store,
                             size_t *right_store)
{
    if (start >= right)
    {
        return 0;
    }
    while (start < right && !(table[start] & PTE_V))
    {
        start++;
    }
    if (start < right)
    {
        if (left_store != NULL)
        {
            *left_store = start;
        }
        int perm = (table[start++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm)
        {
            start++;
        }
        if (right_store != NULL)
        {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}

/*
 * print_pgdir: 输出当前 satp 指向的 Sv39 页表。
 *
 * 这是调试用途的有限输出：
 * 最多打印 PGDIR_PRINT_LIMIT 个有效页表项，避免输出过多。
 */
#define PGDIR_PRINT_LIMIT 128

static uintptr_t
sv39_canonical_va(uintptr_t va)
{
    /*
     * Sv39 的虚拟地址位 63:39 必须等于位 38。
     * 如果位 38 为 1，则进行符号扩展。
     */
    if (va & ((uintptr_t)1 << 38)) {
        va |= ~(((uintptr_t)1 << 39) - 1);
    }
    return va;
}

static bool
pgdir_table_pa_valid(uintptr_t pa)
{
    /*
     * 下级页表应当位于当前 PMM 管理的物理页范围内。
     * 只有检查通过后才通过 KADDR 转为内核虚拟地址。
     */
    size_t ppn = PPN(pa);

    return (pa % PGSIZE == 0) &&
           (ppn >= nbase) &&
           (ppn < npage);
}

static void
print_pgdir_level(pte_t *table, int level,
                  uintptr_t va_prefix, size_t *printed)
{
    for (size_t i = 0; i < NPTEENTRY; i++) {
        pte_t entry = table[i];

        if (!(entry & PTE_V)) {
            continue;
        }

        if (*printed >= PGDIR_PRINT_LIMIT) {
            return;
        }

        uintptr_t va =
            va_prefix | ((uintptr_t)i << (PGSHIFT + 9 * level));

        uintptr_t pa = PTE_ADDR(entry);
        uintptr_t flags = entry & 0x3ff;
        bool leaf = (entry & (PTE_R | PTE_W | PTE_X)) != 0;

        cprintf("  L%d VA 0x%016lx PTE 0x%016lx PA 0x%016lx flags 0x%03lx%s\n",
                level,
                (unsigned long)sv39_canonical_va(va),
                (unsigned long)entry,
                (unsigned long)pa,
                (unsigned long)flags,
                leaf ? " leaf" : " table");

        (*printed)++;

        /*
         * 叶子项可以出现在 Sv39 的任一级：
         * L2 为 1 GiB，L1 为 2 MiB，L0 为 4 KiB。
         */
        if (leaf) {
            continue;
        }

        if (level == 0) {
            cprintf("    invalid non-leaf entry at L0\n");
            continue;
        }

        if (!pgdir_table_pa_valid(pa)) {
            cprintf("    next-level table PA is outside managed memory\n");
            continue;
        }

        pte_t *next = (pte_t *)KADDR(pa);

        print_pgdir_level(next, level - 1, va, printed);
    }
}

void
print_pgdir(void)
{
    uintptr_t satp = read_csr(satp);

    /*
     * RV64 satp：
     * MODE 位于 63:60；Sv39 的 MODE 为 8；
     * 根页表 PPN 位于 43:0。
     */
    uintptr_t mode = satp >> 60;
    uintptr_t root_ppn =
        satp & ((((uintptr_t)1) << 44) - 1);
    uintptr_t root_pa = root_ppn << PGSHIFT;

    cprintf("Sv39 page table: satp=0x%016lx root PA=0x%016lx\n",
            (unsigned long)satp,
            (unsigned long)root_pa);

    if (mode != 8) {
        cprintf("  current address translation mode is not Sv39\n");
        return;
    }

    if (!pgdir_table_pa_valid(root_pa)) {
        cprintf("  root page table PA is outside managed memory\n");
        return;
    }

    size_t printed = 0;
    pte_t *root = (pte_t *)KADDR(root_pa);

    print_pgdir_level(root, 2, 0, &printed);

    if (printed >= PGDIR_PRINT_LIMIT) {
        cprintf("  output limited to %d valid entries\n",
                PGDIR_PRINT_LIMIT);
    }
}
