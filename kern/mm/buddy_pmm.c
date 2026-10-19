#include <buddy_pmm.h>
#include <stdio.h>

//lab2 function2-3 2412456

/* Private flag: only the head of a live allocation carries this bit.
 * Free head: PG_property + property=block pages.
 * Allocated head: BUDDY_ALLOCATED + property=original requested pages.
 * Interior: neither bit, property=0. No change to struct Page is needed. */
#define BUDDY_ALLOCATED (1UL << 2)

struct buddy_state {
    list_entry_t lists[BUDDY_MAX_ORDER + 1];
    size_t nr_free;
    size_t min_ppn, end_ppn;
};

static struct buddy_state main_state;
static struct buddy_state *state = &main_state;

static size_t block_pages(unsigned order) { return (size_t)1 << order; }

static unsigned request_order(size_t n) {
    unsigned order = 0;
    while (order < BUDDY_MAX_ORDER && block_pages(order) < n) {
        order++;
    }
    return order;
}

static void buddy_init(void) {
    for (unsigned i = 0; i <= BUDDY_MAX_ORDER; i++) {
        list_init(&state->lists[i]);
    }
    state->nr_free = 0;
    state->min_ppn = (size_t)-1;
    state->end_ppn = 0;
}

static void add_block(struct Page *p, unsigned order) {
    p->property = block_pages(order);
    SetPageProperty(p);
    list_add(&state->lists[order], &p->page_link);
}

static void remove_block(struct Page *p) {
    list_del_init(&p->page_link);
    ClearPageProperty(p);
    p->property = 0;
}

/* XOR the physical page number, never a virtual address or an offset
 * from the free range. Equal size and adjacency alone do not suffice. */
static struct Page *buddy_of(struct Page *p, unsigned order) {
    size_t ppn = page2ppn(p) ^ block_pages(order);
    if (ppn < nbase || ppn >= npage || ppn < state->min_ppn ||
        ppn >= state->end_ppn || block_pages(order) > state->end_ppn - ppn ||
        block_pages(order) > npage - ppn) {
        return NULL;
    }
    return pages + (ppn - nbase);
}

static void insert_merge(struct Page *p, unsigned order) {
    while (order < BUDDY_MAX_ORDER) {
        struct Page *buddy = buddy_of(p, order);
        if (buddy == NULL || !PageProperty(buddy) ||
            buddy->property != block_pages(order)) {
            break;
        }
        remove_block(buddy);
        if (buddy < p) {
            p = buddy;
        }
        order++;
    }
    add_block(p, order);
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0 && base >= pages && base < pages + (npage - nbase));
    assert(n <= (size_t)(pages + (npage - nbase) - base));
    for (struct Page *p = base; p < base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
    }
    state->nr_free += n;
    if (page2ppn(base) < state->min_ppn) {
        state->min_ppn = page2ppn(base);
    }
    if (page2ppn(base) + n > state->end_ppn) {
        state->end_ppn = page2ppn(base) + n;
    }
    /* Greedy aligned decomposition preserves every page, even for an
     * unaligned start and non-power-of-two length. Merge across calls. */
    while (n != 0) {
        unsigned order = BUDDY_MAX_ORDER;
        while (block_pages(order) > n ||
               (page2ppn(base) & (block_pages(order) - 1)) != 0) {
            order--;
        }
        size_t span = block_pages(order);
        insert_merge(base, order);
        base += span;
        n -= span;
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > block_pages(BUDDY_MAX_ORDER)) {
        return NULL;
    }
    unsigned order = request_order(n), found = order;
    size_t span = block_pages(order);
    if (span > state->nr_free) {
        return NULL;
    }
    while (found <= BUDDY_MAX_ORDER && list_empty(&state->lists[found])) {
        found++;
    }
    if (found > BUDDY_MAX_ORDER) {
        return NULL;
    }
    struct Page *p = le2page(list_next(&state->lists[found]), page_link);
    remove_block(p);
    while (found > order) {
        found--;
        add_block(p + block_pages(found), found);
    }
    p->flags |= BUDDY_ALLOCATED;
    p->property = n;
    state->nr_free -= span;
    return p;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0 && n <= block_pages(BUDDY_MAX_ORDER));
    assert(base >= pages && base < pages + (npage - nbase));
    assert((base->flags & BUDDY_ALLOCATED) && base->property == n);
    unsigned order = request_order(n);
    size_t span = block_pages(order);
    assert((page2ppn(base) & (span - 1)) == 0);
    assert(span <= (size_t)(pages + (npage - nbase) - base));
    for (struct Page *p = base; p < base + span; p++) {
        assert(!PageReserved(p) && !PageProperty(p) && page_ref(p) == 0);
        assert(p == base || !(p->flags & BUDDY_ALLOCATED));
    }
    base->flags &= ~BUDDY_ALLOCATED;
    base->property = 0;
    state->nr_free += span;
    insert_merge(base, order);
}

static size_t buddy_nr_free_pages(void) { return state->nr_free; }

/* Tests below use the same public alloc/free wrappers as kernel callers.
 * The isolated arena is obtained from the real manager before switching
 * private list state; no fabricated Page array or changed global pages. */
static void buddy_check(void);

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

/* Verify links, bounds, alignment, accounting, non-overlap and maximal
 * coalescing. Pairwise interval checks are acceptable in boot self-tests. */
static void verify_state(void) {
    size_t total = 0;
    for (unsigned order = 0; order <= BUDDY_MAX_ORDER; order++) {
        list_entry_t *head = &state->lists[order];
        size_t visited = 0;
        for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
            assert(++visited <= npage - nbase);
            assert(list_prev(list_next(le)) == le);
            assert(list_next(list_prev(le)) == le);
            struct Page *p = le2page(le, page_link);
            size_t span = block_pages(order);
            assert(p >= pages && p < pages + (npage - nbase));
            assert(span <= (size_t)(pages + (npage - nbase) - p));
            assert(PageProperty(p) && p->property == span);
            assert((page2ppn(p) & (span - 1)) == 0);
            for (size_t j = 0; j < span; j++) {
                assert(!PageReserved(p + j) && page_ref(p + j) == 0);
                assert(!(p[j].flags & BUDDY_ALLOCATED));
                if (j != 0) {
                    assert(!PageProperty(p + j) && p[j].property == 0);
                }
            }
            if (order < BUDDY_MAX_ORDER) {
                struct Page *b = buddy_of(p, order);
                assert(b == NULL || !PageProperty(b) || b->property != span);
            }
            /* Disjoint aligned dyadic blocks can only overlap by containment.
             * Their interiors have no free-head bit (checked above), so
             * nested blocks or duplicate membership fail these assertions. */
            for (list_entry_t *other = list_next(le); other != head;
                 other = list_next(other)) {
                assert(le2page(other, page_link) != p);
            }
            total += span;
        }
    }
    assert(total == nr_free_pages());
}

static size_t count_order(unsigned order) {
    size_t n = 0;
    list_entry_t *head = &state->lists[order];
    for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
        n++;
    }
    return n;
}

static void reserve_arena(struct Page *arena) {
    for (size_t i = 0; i < 64; i++) {
        arena[i].flags = 0;
        SetPageReserved(arena + i);
        arena[i].property = 0;
        set_page_ref(arena + i, 0);
        list_init(&arena[i].page_link);
    }
}

static void buddy_check(void) {
    size_t initial = nr_free_pages();
    verify_state();
    cprintf("[Buddy] initial free pages: %lu\n", (unsigned long)initial);
    assert(alloc_pages((size_t)-1) == NULL);
    assert(alloc_pages(block_pages(BUDDY_MAX_ORDER) + 1) == NULL);
    assert(nr_free_pages() == initial);

    struct Page *arena = alloc_pages(64);
    assert(arena != NULL && (page2ppn(arena) & 63) == 0);
    static struct buddy_state test_state;
    state = &test_state;
    buddy_init();
    reserve_arena(arena);
    buddy_init_memmap(arena, 64);

    /* A single page must split order 6 into one block at each order 0..5. */
    struct Page *p = alloc_page();
    assert(p == arena && nr_free_pages() == 63);
    for (unsigned i = 0; i < 6; i++) {
        assert(count_order(i) == 1);
    }
    verify_state();
    free_page(p);
    assert(count_order(6) == 1 && nr_free_pages() == 64);
    verify_state();
    cprintf("[Buddy] split six levels and merge back: PASS\n");

    const size_t requests[] = {1, 2, 3, 4, 5, 8, 9, 16, 17, 32, 33, 64};
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        size_t n = requests[i], span = block_pages(request_order(n));
        p = alloc_pages(n);
        assert(p == arena && (page2ppn(p) & (span - 1)) == 0);
        assert(p->property == n && !PageProperty(p) && page_ref(p) == 0);
        assert(nr_free_pages() == 64 - span);
        free_pages(p, n);
        assert(nr_free_pages() == 64 && count_order(6) == 1);
        verify_state();
    }
    cprintf("[Buddy] powers of two and rounded requests (3 -> 4): PASS\n");

    struct Page *held[64];
    for (size_t i = 0; i < 64; i++) {
        held[i] = alloc_page();
        assert(held[i] == arena + i);
        for (size_t j = 0; j < i; j++) {
            assert(held[i] != held[j]);
        }
    }
    assert(nr_free_pages() == 0 && alloc_page() == NULL);
    /* +1 and +2 are adjacent, equally sized, but not XOR buddies. */
    free_page(held[1]);
    free_page(held[2]);
    assert(nr_free_pages() == 2 && count_order(0) == 2);
    assert(alloc_pages(2) == NULL);
    verify_state();
    assert(alloc_page() == held[2]);
    assert(alloc_page() == held[1]);
    for (size_t i = 0; i < 64; i += 2) {
        free_page(held[i]);
    }
    assert(nr_free_pages() == 32 && alloc_pages(2) == NULL);
    verify_state();
    for (size_t i = 1; i < 64; i += 2) {
        free_page(held[i]);
    }
    verify_state();
    assert(nr_free_pages() == 64 && count_order(6) == 1);
    cprintf("[Buddy] non-buddy neighbours, fragmentation, exhaustion: PASS\n");

    /* Unaligned 37-page range, a gap, then an independently registered
     * adjacent range. Every page is checked against the expected set. */
    buddy_init();
    reserve_arena(arena);
    buddy_init_memmap(arena + 3, 37);
    buddy_init_memmap(arena + 48, 8);
    buddy_init_memmap(arena + 40, 8);
    assert(nr_free_pages() == 53);
    verify_state();
    bool seen[64] = {0};
    for (size_t i = 0; i < 53; i++) {
        held[i] = alloc_page();
        assert(held[i] >= arena + 3 && held[i] < arena + 56);
        size_t index = held[i] - arena;
        assert(!seen[index]);
        seen[index] = 1;
    }
    for (size_t i = 0; i < 64; i++) {
        assert(seen[i] == (i >= 3 && i < 56));
    }
    assert(alloc_page() == NULL && nr_free_pages() == 0);
    for (size_t i = 53; i > 0; i--) {
        free_page(held[i - 1]);
    }
    verify_state();
    assert(nr_free_pages() == 53);
    cprintf("[Buddy] unaligned 37-page init, multiple ranges, no lost pages: PASS\n");

    /* Seeded mixed-size workload with an independent occupancy oracle.
     * Repeats enough operations to exercise different split/merge orders. */
    buddy_init();
    reserve_arena(arena);
    buddy_init_memmap(arena, 64);
    struct Page *slots[16] = {0};
    size_t sizes[16] = {0};
    bool occupied[64] = {0};
    size_t used = 0;
    unsigned seed = 2412456;
    for (size_t step = 0; step < 1024; step++) {
        seed = seed * 1664525U + 1013904223U;
        size_t slot = (seed >> 16) % 16;
        if (slots[slot] != NULL) {
            size_t span = block_pages(request_order(sizes[slot]));
            size_t offset = slots[slot] - arena;
            for (size_t j = 0; j < span; j++) {
                assert(occupied[offset + j]);
                occupied[offset + j] = 0;
            }
            free_pages(slots[slot], sizes[slot]);
            used -= span;
            slots[slot] = NULL;
        } else {
            size_t n = 1 + ((seed >> 8) % 17);
            size_t span = block_pages(request_order(n));
            p = alloc_pages(n);
            if (p != NULL) {
                assert(p >= arena && p + span <= arena + 64);
                size_t offset = p - arena;
                assert((page2ppn(p) & (span - 1)) == 0);
                for (size_t j = 0; j < span; j++) {
                    assert(!occupied[offset + j]);
                    occupied[offset + j] = 1;
                }
                used += span;
                slots[slot] = p;
                sizes[slot] = n;
            } else {
                /* Failure iff no aligned block is free in the oracle. */
                for (size_t offset = 0; offset < 64; offset += span) {
                    bool busy = 0;
                    for (size_t j = 0; j < span; j++) {
                        busy |= occupied[offset + j];
                    }
                    assert(busy);
                }
            }
        }
        assert(nr_free_pages() == 64 - used);
        verify_state();
    }
    for (size_t i = 0; i < 16; i++) {
        if (slots[i] != NULL) {
            free_pages(slots[i], sizes[i]);
        }
    }
    assert(nr_free_pages() == 64 && count_order(6) == 1);
    verify_state();
    cprintf("[Buddy] 1024 mixed operations with occupancy oracle: PASS\n");

    /* Restore the original 64-page allocation descriptor, then return it
     * to the untouched real free lists. No test-only state can escape. */
    reserve_arena(arena);
    for (size_t i = 0; i < 64; i++) {
        ClearPageReserved(arena + i);
    }
    arena->flags |= BUDDY_ALLOCATED;
    arena->property = 64;
    state = &main_state;
    free_pages(arena, 64);
    verify_state();
    assert(nr_free_pages() == initial);
    cprintf("[Buddy] state restored: %lu -> %lu free pages: PASS\n",
            (unsigned long)initial, (unsigned long)nr_free_pages());
}
