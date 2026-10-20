#include <dtb.h>
#include <stdio.h>
#include <memlayout.h>
#include <riscv.h>

/* Public starter interface retained; bounds-aware parsing by 2412456. */
static uint64_t memory_base;
static uint64_t memory_size;

void dtb_init(void) {
    memory_base = memory_size = 0;
    cprintf("DTB Init\nHartID: %ld\nDTB Address: 0x%lx\n", boot_hartid, boot_dtb);
    /* entry.S maps this 1-GiB DRAM window. A 256-MiB machine's DTB may
     * lie beyond the PMM's 128-MiB cap. The firmware pointer is trusted
     * to designate real RAM; parser offsets and sizes remain checked. */
    const uintptr_t mapped_end = DRAM_BASE + (1UL << 30);
    if (boot_dtb < DRAM_BASE || boot_dtb > mapped_end - 40) {
        cprintf("Error: DTB pointer outside boot mapping\n");
        return;
    }
    size_t readable = mapped_end - boot_dtb;
    if (readable > (16UL << 20)) { readable = 16UL << 20; }
    uint64_t base, size;
    int result = dtb_parse_memory((const void *)(boot_dtb + PHYSICAL_MEMORY_OFFSET),
                                  readable, &base, &size);
    if (result != 0) {
        cprintf("Error: DTB memory parse failed (%d)\n", result);
        return;
    }
    memory_base = base;
    memory_size = size;
    cprintf("Physical Memory from DTB:\n");
    cprintf("  Base: 0x%016lx\n", base);
    cprintf("  Size: 0x%016lx (%ld MB)\n", size, size / (1024 * 1024));
    cprintf("  End:  0x%016lx\n", base + size - 1);
    cprintf("DTB init completed\n");
}

uint64_t get_memory_base(void) { return memory_base; }
uint64_t get_memory_size(void) { return memory_size; }
