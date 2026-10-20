#include <slub.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>

//lab2 challenge SLUB tests 2412456
static void *objects[512];
static size_t ctor_calls;

static void constructor(void *object) {
    memset(object, 0x5a, 37);
    ctor_calls++;
}

static void check_bytes(void *object, size_t size, unsigned char expected) {
    unsigned char *p = object;
    for (size_t i = 0; i < size; i++) {
        assert(p[i] == expected);
    }
}

void slub_check(void) {
    size_t initial = nr_free_pages();
    slub_init();
#if SLUB_NEGATIVE_TEST
    void *bad = kmalloc(32);
    assert(bad != NULL);
    cprintf("[SLUB negative] case %d: expect assertion panic\n", SLUB_NEGATIVE_TEST);
#if SLUB_NEGATIVE_TEST == 1
    kfree(bad);
    kfree(bad);
#elif SLUB_NEGATIVE_TEST == 2
    struct kmem_cache *wrong = kmem_cache_create("wrong", 32, 16, NULL);
    assert(wrong != NULL);
    kmem_cache_free(wrong, bad);
#elif SLUB_NEGATIVE_TEST == 3
    kfree((char *)bad + 1);
#endif
    panic("negative test did not assert");
#endif
    assert(kmalloc(0) == NULL && kmalloc((size_t)-1) == NULL);
    kfree(NULL);
    assert(kmem_cache_create("zero", 0, 16, NULL) == NULL);
    assert(kmem_cache_create("alignment", 24, 3, NULL) == NULL);
    assert(kmem_cache_create("too-large", PGSIZE, 16, NULL) == NULL);

    struct kmem_cache *cache = kmem_cache_create("records", 37, 64, constructor);
    assert(cache != NULL);
    struct slub_cache_stats stats = kmem_cache_stats(cache);
    assert(stats.object_size == 37 && stats.stride == 64 && stats.capacity > 1);
    size_t count = stats.capacity + 1;
    assert(count <= 512);
    for (size_t i = 0; i < count; i++) {
        objects[i] = kmem_cache_alloc(cache);
        assert(objects[i] && ((uintptr_t)objects[i] & 63) == 0);
        check_bytes(objects[i], 37, 0x5a);
        for (size_t j = 0; j < i; j++) {
            assert(objects[i] != objects[j]);
        }
        memset(objects[i], (char)i, 37);
    }
    stats = kmem_cache_stats(cache);
    assert(stats.slabs == 2 && stats.live == count && ctor_calls == count);
    assert(nr_free_pages() == initial - 2);
    assert(!kmem_cache_destroy(cache));
    void *reused = objects[0];
    kmem_cache_free(cache, reused);
    objects[0] = kmem_cache_alloc(cache);
    assert(objects[0] == reused && ctor_calls == count + 1);
    check_bytes(objects[0], 37, 0x5a);
    memset(objects[0], 0, 37);
    for (size_t i = 0; i < count; i++) {
        check_bytes(objects[i], 37, (unsigned char)i);
        kmem_cache_free(cache, objects[i]);
    }
    assert(kmem_cache_stats(cache).slabs == 1);
    assert(kmem_cache_shrink(cache) == 1);
    assert(kmem_cache_destroy(cache));
    assert(nr_free_pages() == initial);
    cprintf("[SLUB] aligned cache, ctor, reuse, full/partial/empty, shrink: PASS\n");

    const size_t sizes[] = {1, 15, 16, 17, 31, 32, 33, 63, 64, 65,
        127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024,
        1025, 2048, 2049, 4096, 8193, 16384};
    count = sizeof(sizes) / sizeof(sizes[0]);
    for (size_t i = 0; i < count; i++) {
        objects[i] = kmalloc(sizes[i]);
        assert(objects[i] && ((uintptr_t)objects[i] & 15) == 0);
        assert(ksize(objects[i]) >= sizes[i]);
        memset(objects[i], (char)(i + 1), sizes[i]);
    }
    for (size_t i = count; i > 0; i--) {
        check_bytes(objects[i - 1], sizes[i - 1], (unsigned char)i);
        kfree(objects[i - 1]);
    }
    slub_shrink();
    assert(nr_free_pages() == initial);
    cprintf("[SLUB] kmalloc class boundaries, large blocks, content isolation: PASS\n");

    /* One minimum-size cache page must host many live objects. */
    for (size_t i = 0; i < 64; i++) {
        objects[i] = kmalloc(16);
        assert(objects[i] != NULL);
    }
    assert(nr_free_pages() == initial - 1);
    for (size_t i = 0; i < 64; i++) {
        kfree(objects[i]);
    }
    slub_shrink();
    assert(nr_free_pages() == initial);
    cprintf("[SLUB] 64 small objects share one physical page: PASS\n");

    size_t requested[64] = {0};
    memset(objects, 0, sizeof(objects));
    unsigned seed = 2412456;
    for (size_t step = 0; step < 1024; step++) {
        seed = seed * 1664525U + 1013904223U;
        size_t slot = (seed >> 16) % 64;
        if (objects[slot] != NULL) {
            check_bytes(objects[slot], requested[slot], (unsigned char)(slot + 1));
            kfree(objects[slot]);
            objects[slot] = NULL;
        } else {
            requested[slot] = 1 + ((seed >> 7) % 8193);
            objects[slot] = kmalloc(requested[slot]);
            assert(objects[slot] != NULL);
            uintptr_t start = (uintptr_t)objects[slot];
            for (size_t j = 0; j < 64; j++) {
                if (j != slot && objects[j] != NULL) {
                    uintptr_t other = (uintptr_t)objects[j];
                    assert(start + requested[slot] <= other ||
                           other + requested[j] <= start);
                }
            }
            memset(objects[slot], (char)(slot + 1), requested[slot]);
        }
    }
    for (size_t i = 0; i < 64; i++) {
        if (objects[i] != NULL) {
            check_bytes(objects[i], requested[i], (unsigned char)(i + 1));
            kfree(objects[i]);
        }
    }
    slub_shrink();
    assert(nr_free_pages() == initial);
    cprintf("[SLUB] 1024 mixed objects, nonoverlap and byte-pattern oracle: PASS\n");

    /* Cache descriptors are bounded too; failure must not consume pages. */
    struct kmem_cache *held[24];
    count = 0;
    while (count < 24 && (held[count] = kmem_cache_create("bounded", 32, 16, NULL))) {
        count++;
    }
    assert(count == 16 && nr_free_pages() == initial);
    for (size_t i = 0; i < count; i++) {
        assert(kmem_cache_destroy(held[i]));
    }
    cprintf("[SLUB] cache metadata exhaustion and recovery: PASS\n");

    /* Real PMM exhaustion: link the allocated pages in their own memory,
     * preserving the original Page metadata and exact free(1) contract. */
    void *chain = NULL;
    struct Page *page;
    while ((page = alloc_page()) != NULL) {
        void *address = (void *)(page2pa(page) + va_pa_offset);
        *(void **)address = chain;
        chain = address;
    }
    assert(nr_free_pages() == 0 && kmalloc(16) == NULL && kmalloc(8193) == NULL);
    assert(slub_shrink() == 0);
    while (chain != NULL) {
        void *next = *(void **)chain;
        free_page(pa2page(PADDR(chain)));
        chain = next;
    }
    assert(nr_free_pages() == initial);
    void *object = kmalloc(65);
    assert(object != NULL);
    kfree(object);
    slub_shrink();
    assert(nr_free_pages() == initial);
    cprintf("[SLUB] real page exhaustion, allocation failure, full recovery: PASS\n");
    cprintf("[SLUB] state restored: %lu -> %lu free pages: PASS\n",
            (unsigned long)initial, (unsigned long)nr_free_pages());
}
