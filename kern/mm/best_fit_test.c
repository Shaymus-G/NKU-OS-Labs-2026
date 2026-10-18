#include <best_fit_pmm.h>
#include <stdio.h>

#define TEST_PAGES 32

struct expected_block {
    size_t offset;
    size_t length;
};

struct memory_state {
    struct Page *first;
    size_t blocks;
    size_t free_pages;
    size_t reserved_pages;
};

// Observe descriptors without accessing the allocator's private free_area.
static struct memory_state
observe_memory(void) {
    struct memory_state state = {NULL, 0, 0, 0};
    size_t count = npage - nbase;
    for (size_t i = 0; i < count; i++) {
        struct Page *p = pages + i;
        if (PageReserved(p)) {
            state.reserved_pages++;
        }
        if (PageProperty(p)) {
            assert(!PageReserved(p));
            assert(p->property > 0 && p->property <= count - i);
            if (state.first == NULL) {
                state.first = p;
            }
            state.blocks++;
            state.free_pages += p->property;
        }
    }
    assert(state.free_pages == nr_free_pages());
    return state;
}

// Every expected block must be separated by allocated pages.
static void
expect_blocks(struct Page *base, const struct expected_block *blocks,
              size_t count) {
    size_t total = 0;
    for (size_t k = 0; k < count; k++) {
        assert(blocks[k].length > 0);
        assert(blocks[k].offset + blocks[k].length <= TEST_PAGES);
        if (k > 0) {
            assert(blocks[k - 1].offset + blocks[k - 1].length <
                   blocks[k].offset);
            list_entry_t *prev = &base[blocks[k - 1].offset].page_link;
            list_entry_t *next = &base[blocks[k].offset].page_link;
            assert(list_next(prev) == next && list_prev(next) == prev);
        }
        total += blocks[k].length;
    }
    assert(nr_free_pages() == total);
    for (size_t i = 0; i < TEST_PAGES; i++) {
        struct Page *p = base + i;
        size_t expected_length = 0;
        for (size_t k = 0; k < count; k++) {
            if (blocks[k].offset == i) {
                expected_length = blocks[k].length;
            }
        }
        assert(!PageReserved(p) && page_ref(p) == 0);
        assert(!!PageProperty(p) == (expected_length != 0));
        assert(p->property == expected_length);
    }
}

static void
check_best_match(struct Page *base) {
    free_pages(base, 8);
    free_pages(base + 12, 5);
    free_pages(base + 22, 7);
    const struct expected_block before[] = {{0, 8}, {12, 5}, {22, 7}};
    expect_blocks(base, before, 3);

    // The first adequate block is 8 pages, but the smallest is 5 pages.
    struct Page *p = alloc_pages(4);
    assert(p == base + 12);
    const struct expected_block after[] = {{0, 8}, {16, 1}, {22, 7}};
    expect_blocks(base, after, 3);
    free_pages(p, 4);
    expect_blocks(base, before, 3);
    assert(alloc_pages(5) == base + 12);
    assert(alloc_pages(7) == base + 22);
    assert(alloc_pages(8) == base);
    expect_blocks(base, NULL, 0);
    cprintf("[Best-Fit behavior] minimum adequate block: PASS\n");
}

static void
check_exact_and_tie(struct Page *base) {
    // Register in reverse address order to exercise ordered insertion too.
    free_pages(base + 22, 6);
    free_pages(base + 12, 4);
    free_pages(base + 2, 4);
    const struct expected_block before[] = {{2, 4}, {12, 4}, {22, 6}};
    expect_blocks(base, before, 3);
    assert(alloc_pages(4) == base + 2);
    const struct expected_block after[] = {{12, 4}, {22, 6}};
    expect_blocks(base, after, 2);
    assert(alloc_pages(4) == base + 12);
    assert(alloc_pages(6) == base + 22);
    expect_blocks(base, NULL, 0);
    cprintf("[Best-Fit behavior] exact match and address tie: PASS\n");
}

static void
check_split(struct Page *base) {
    free_pages(base + 4, 10);
    assert(alloc_pages(3) == base + 4);
    const struct expected_block after[] = {{7, 7}};
    expect_blocks(base, after, 1);
    assert(alloc_pages(7) == base + 7);
    expect_blocks(base, NULL, 0);
    cprintf("[Best-Fit behavior] split remainder: PASS\n");
}

static void
check_merge_previous(struct Page *base) {
    free_pages(base + 4, 3);
    free_pages(base + 7, 2);
    const struct expected_block merged[] = {{4, 5}};
    expect_blocks(base, merged, 1);
    assert(alloc_pages(5) == base + 4);
    expect_blocks(base, NULL, 0);
    cprintf("[Best-Fit behavior] merge previous block: PASS\n");
}

static void
check_merge_next(struct Page *base) {
    free_pages(base + 7, 2);
    free_pages(base + 4, 3);
    const struct expected_block merged[] = {{4, 5}};
    expect_blocks(base, merged, 1);
    assert(alloc_pages(5) == base + 4);
    expect_blocks(base, NULL, 0);
    cprintf("[Best-Fit behavior] merge next block: PASS\n");
}

static void
check_merge_both(struct Page *base) {
    free_pages(base + 4, 3);
    free_pages(base + 9, 3);
    const struct expected_block before[] = {{4, 3}, {9, 3}};
    expect_blocks(base, before, 2);
    free_pages(base + 7, 2);
    const struct expected_block merged[] = {{4, 8}};
    expect_blocks(base, merged, 1);
    assert(alloc_pages(8) == base + 4);
    expect_blocks(base, NULL, 0);
    cprintf("[Best-Fit behavior] merge both neighbours: PASS\n");
}

static void
check_fragmentation(struct Page *base) {
    free_pages(base, 4);
    free_pages(base + 8, 4);
    free_pages(base + 16, 4);
    const struct expected_block fragmented[] = {{0, 4}, {8, 4}, {16, 4}};
    expect_blocks(base, fragmented, 3);
    assert(nr_free_pages() == 12);
    assert(alloc_pages(6) == NULL);
    expect_blocks(base, fragmented, 3);
    assert(alloc_pages(13) == NULL);
    expect_blocks(base, fragmented, 3);
    assert(alloc_pages(4) == base);
    assert(alloc_pages(4) == base + 8);
    assert(alloc_pages(4) == base + 16);
    expect_blocks(base, NULL, 0);
    assert(alloc_page() == NULL);
    expect_blocks(base, NULL, 0);
    cprintf("[Best-Fit behavior] fragmented and exhausted failures: PASS\n");
}

static void
check_repeated_roundtrip(struct Page *base) {
    const struct expected_block whole[] = {{0, TEST_PAGES}};
    for (size_t round = 0; round < 3; round++) {
        free_pages(base + 16, 16);
        free_pages(base, 8);
        free_pages(base + 8, 8);
        expect_blocks(base, whole, 1);
        assert(alloc_pages(TEST_PAGES) == base);
        expect_blocks(base, NULL, 0);
    }
    cprintf("[Best-Fit behavior] repeated full roundtrip: PASS\n");
}

void
best_fit_behavior_check(void) {
    assert(pmm_manager == &best_fit_pmm_manager);
    struct memory_state before = observe_memory();
    // A's current initialization registers one contiguous free interval.
    assert(before.blocks == 1 && before.free_pages >= TEST_PAGES);

    struct Page *base = alloc_pages(before.free_pages);
    assert(base == before.first && nr_free_pages() == 0);
    expect_blocks(base, NULL, 0);

    check_best_match(base);
    check_exact_and_tie(base);
    check_split(base);
    check_merge_previous(base);
    check_merge_next(base);
    check_merge_both(base);
    check_fragmentation(base);
    check_repeated_roundtrip(base);

    // All temporarily released pages have been allocated again by each case.
    // Return the entire original interval without resetting private state.
    free_pages(base, before.free_pages);
    struct memory_state after = observe_memory();
    assert(after.first == before.first && after.blocks == before.blocks);
    assert(after.free_pages == before.free_pages);
    assert(after.reserved_pages == before.reserved_pages);
    assert(PageProperty(base) && base->property == before.free_pages);
    assert(list_next(&base->page_link)->prev == &base->page_link);
    assert(list_prev(&base->page_link)->next == &base->page_link);
    cprintf("[Best-Fit behavior] original state restored: PASS (%lu free pages)\n",
            (unsigned long)after.free_pages);
    cprintf("[Best-Fit behavior] ALL PASS\n");
}
