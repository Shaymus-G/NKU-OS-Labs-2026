#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

/* 2^15 pages = 128 MiB, the entire RAM supported by this lab. */
#define BUDDY_MAX_ORDER 15

/* alloc(n) owns ceil_pow2(n) pages; free(base, n) uses the original n.
 * Partial frees and freeing with the rounded size are not supported.
 * Like the other lab managers, calls require external serialization. */
extern const struct pmm_manager buddy_pmm_manager;

#endif
