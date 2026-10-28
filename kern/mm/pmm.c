#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <../sync/sync.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
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
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
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
page_init(void) {
    extern char end[];

    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();

    /*
     * 当前框架的 pages[] 以固定的 DRAM_BASE 为编号起点。
     * 因此这里明确检查起点，避免错误地描述不存在的物理页。
     */
    if (mem_base != DRAM_BASE || mem_size == 0) {
        panic("page_init: invalid physical memory range");
    }

    /* 防止计算内存末端时发生无符号整数溢出。 */
    uint64_t reported_end = mem_base + mem_size;
    if (reported_end < mem_base) {
        panic("page_init: physical memory range overflow");
    }

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    /*
     * reported_end 是 DTB 报告的末端；
     * managed_end 是本框架实际管理的末端。
     */
    uintptr_t managed_end = reported_end;
    if (managed_end > PHYSICAL_MEMORY_END) {
        managed_end = PHYSICAL_MEMORY_END;
    }
    managed_end = ROUNDDOWN(managed_end, PGSIZE);

    if (managed_end <= KERNEL_BEGIN_PADDR) {
        panic("page_init: insufficient memory for kernel");
    }

    npage = PPN(managed_end);
    if (npage <= nbase) {
        panic("page_init: invalid physical page range");
    }

    /* 将描述符数组放在内核结束位置之后，并按页对齐。 */
    uintptr_t pages_va = ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t pages_pa = PADDR(pages_va);
    size_t page_count = npage - nbase;

    /*
     * 先检查空间，再写描述符数组。
     * 用除法比较避免 page_count * sizeof(struct Page) 先溢出。
     */
    if (pages_pa >= managed_end ||
        page_count > (managed_end - pages_pa) / sizeof(struct Page)) {
        panic("page_init: insufficient memory for page descriptors");
    }

    pages = (struct Page *)pages_va;

    for (size_t i = 0; i < page_count; i++) {
        struct Page *p = &pages[i];

        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
        SetPageReserved(p);
    }

    uintptr_t descriptors_end_pa =
        pages_pa + page_count * sizeof(struct Page);

    uintptr_t free_begin =
        ROUNDUP(descriptors_end_pa, PGSIZE);

    cprintf("physical memory map:\n");
    cprintf("  DTB range: [0x%016lx, 0x%016lx)\n",
            (unsigned long)mem_base,
            (unsigned long)reported_end);
    cprintf("  managed end: 0x%016lx\n",
            (unsigned long)managed_end);
    cprintf("  kernel end VA: 0x%016lx\n",
            (unsigned long)(uintptr_t)end);
    cprintf("  pages VA: 0x%016lx, PA: 0x%016lx\n",
            (unsigned long)pages_va,
            (unsigned long)pages_pa);
    cprintf("  descriptor count: %lu, bytes: %lu\n",
            (unsigned long)page_count,
            (unsigned long)(page_count * sizeof(struct Page)));

    if (free_begin < managed_end) {
        size_t free_count =
            (managed_end - free_begin) / PGSIZE;

        init_memmap(pa2page(free_begin), free_count);

        cprintf("  free range: [0x%016lx, 0x%016lx), pages: %lu\n",
                (unsigned long)free_begin,
                (unsigned long)managed_end,
                (unsigned long)free_count);
    } else {
        cprintf("  no complete free physical pages\n");
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
