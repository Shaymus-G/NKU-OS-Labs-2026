#include <slub.h>
#include <pmm.h>
#include <string.h>

//lab2 challenge SLUB 2412456
#define CACHE_LIMIT 24
#define GENERAL_CACHES 8
#define SLAB_MAGIC 0x534c5542534c4142UL
#define LARGE_MAGIC 0x534c55424c415247UL

struct kmem_cache {
    const char *name;
    size_t size, stride, offset, capacity, slabs, live;
    void (*ctor)(void *);
    list_entry_t partial, full, empty;
    bool used, general;
};

/* Metadata lives inside its own backing page. Free objects carry next
 * pointers, rather than requiring an external per-object descriptor. */
struct slab {
    uint64_t magic;
    struct kmem_cache *cache;
    list_entry_t link;
    void *free;
    size_t inuse;
    uint64_t allocated[4];
};

struct large_object {
    uint64_t magic;
    size_t pages, requested;
    uint64_t padding;             /* keep returned memory 16-byte aligned */
};

static struct kmem_cache caches[CACHE_LIMIT];
static struct kmem_cache *general[GENERAL_CACHES];
static bool initialized;

static void valid_cache(struct kmem_cache *cache) {
    assert(initialized && cache >= caches && cache < caches + CACHE_LIMIT);
    assert(cache->used);
}

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
                                    size_t align, void (*ctor)(void *)) {
    assert(initialized);
    if (name == NULL || size == 0 || size > PGSIZE || align == 0 ||
        align > PGSIZE || (align & (align - 1)) != 0) {
        return NULL;
    }
    if (align < 16) {
        align = 16;
    }
    size_t stride = ROUNDUP(size, align);
    size_t offset = ROUNDUP(sizeof(struct slab), align);
    if (offset >= PGSIZE || stride > PGSIZE - offset) {
        return NULL;
    }
    for (unsigned i = 0; i < CACHE_LIMIT; i++) {
        struct kmem_cache *cache = &caches[i];
        if (!cache->used) {
            memset(cache, 0, sizeof(*cache));
            cache->used = 1;
            cache->name = name;
            cache->size = size;
            cache->stride = stride;
            cache->offset = offset;
            cache->capacity = (PGSIZE - offset) / stride;
            assert(cache->capacity > 0 && cache->capacity <= 256);
            cache->ctor = ctor;
            list_init(&cache->partial);
            list_init(&cache->full);
            list_init(&cache->empty);
            return cache;
        }
    }
    return NULL;
}

void slub_init(void) {
    assert(!initialized);
    initialized = 1;
    for (unsigned i = 0; i < GENERAL_CACHES; i++) {
        general[i] = kmem_cache_create("kmalloc", (size_t)16 << i, 16, NULL);
        assert(general[i] != NULL);
        general[i]->general = 1;
    }
}

static void place_slab(struct slab *slab) {
    struct kmem_cache *cache = slab->cache;
    list_entry_t *head = slab->inuse == 0 ? &cache->empty :
        (slab->inuse == cache->capacity ? &cache->full : &cache->partial);
    list_add(head, &slab->link);
}

static struct slab *new_slab(struct kmem_cache *cache) {
    struct Page *page = alloc_page();
    if (page == NULL) {
        return NULL;
    }
    struct slab *slab = (void *)(page2pa(page) + va_pa_offset);
    memset(slab, 0, sizeof(*slab));
    slab->magic = SLAB_MAGIC;
    slab->cache = cache;
    for (size_t i = cache->capacity; i > 0; i--) {
        void *object = (char *)slab + cache->offset + (i - 1) * cache->stride;
        *(void **)object = slab->free;
        slab->free = object;
    }
    cache->slabs++;
    place_slab(slab);
    return slab;
}

static struct slab *object_slab(void *object) {
    uintptr_t address = (uintptr_t)object;
    assert(address >= KERNBASE && address < va_pa_offset + npage * PGSIZE);
    return (void *)ROUNDDOWN(address, PGSIZE);
}

static size_t object_index(struct slab *slab, void *object) {
    struct kmem_cache *cache = slab->cache;
    uintptr_t start = (uintptr_t)slab + cache->offset;
    uintptr_t address = (uintptr_t)object;
    assert(address >= start && address - start < cache->capacity * cache->stride);
    assert((address - start) % cache->stride == 0);
    return (address - start) / cache->stride;
}

void *kmem_cache_alloc(struct kmem_cache *cache) {
    valid_cache(cache);
    struct slab *slab;
    if (!list_empty(&cache->partial)) {
        slab = to_struct(list_next(&cache->partial), struct slab, link);
    } else if (!list_empty(&cache->empty)) {
        slab = to_struct(list_next(&cache->empty), struct slab, link);
    } else {
        slab = new_slab(cache);
        if (slab == NULL) {
            return NULL;
        }
    }
    assert(slab->magic == SLAB_MAGIC && slab->cache == cache && slab->free);
    void *object = slab->free;
    size_t index = object_index(slab, object);
    uint64_t bit = (uint64_t)1 << (index % 64);
    assert(!(slab->allocated[index / 64] & bit));
    list_del_init(&slab->link);
    slab->free = *(void **)object;
    slab->allocated[index / 64] |= bit;
    slab->inuse++;
    cache->live++;
    place_slab(slab);
    if (cache->ctor != NULL) {
        cache->ctor(object);
    }
    return object;
}

static void release_slab(struct slab *slab) {
    assert(slab->inuse == 0 && slab->magic == SLAB_MAGIC);
    list_del_init(&slab->link);
    slab->cache->slabs--;
    slab->magic = 0;
    free_page(pa2page(PADDR(slab)));
}

void kmem_cache_free(struct kmem_cache *cache, void *object) {
    valid_cache(cache);
    assert(object != NULL);
    struct slab *slab = object_slab(object);
    assert(slab->magic == SLAB_MAGIC && slab->cache == cache);
    size_t index = object_index(slab, object);
    uint64_t bit = (uint64_t)1 << (index % 64);
    assert(slab->allocated[index / 64] & bit);
    list_del_init(&slab->link);
    slab->allocated[index / 64] &= ~bit;
    slab->inuse--;
    cache->live--;
    memset(object, (char)0xdd, cache->size);
    *(void **)object = slab->free;
    slab->free = object;
    /* Keep at most one empty slab per cache to make reuse observable. */
    if (slab->inuse == 0 && !list_empty(&cache->empty)) {
        /* release_slab expects an attached link. */
        list_add(&cache->empty, &slab->link);
        release_slab(slab);
    } else {
        place_slab(slab);
    }
}

size_t kmem_cache_shrink(struct kmem_cache *cache) {
    valid_cache(cache);
    size_t count = 0;
    while (!list_empty(&cache->empty)) {
        struct slab *slab = to_struct(list_next(&cache->empty), struct slab, link);
        release_slab(slab);
        count++;
    }
    return count;
}

bool kmem_cache_destroy(struct kmem_cache *cache) {
    valid_cache(cache);
    if (cache->general || cache->live != 0) {
        return 0;
    }
    kmem_cache_shrink(cache);
    assert(cache->slabs == 0);
    cache->used = 0;
    return 1;
}

struct slub_cache_stats kmem_cache_stats(struct kmem_cache *cache) {
    valid_cache(cache);
    struct slub_cache_stats stats = {
        cache->size, cache->stride, cache->capacity, cache->slabs, cache->live
    };
    return stats;
}

void *kmalloc(size_t size) {
    assert(initialized);
    if (size == 0 || size > (npage - nbase) * PGSIZE - sizeof(struct large_object)) {
        return NULL;
    }
    for (unsigned i = 0; i < GENERAL_CACHES; i++) {
        if (size <= general[i]->size) {
            return kmem_cache_alloc(general[i]);
        }
    }
    size_t count = (size + sizeof(struct large_object) + PGSIZE - 1) / PGSIZE;
    struct Page *page = alloc_pages(count);
    if (page == NULL) {
        return NULL;
    }
    struct large_object *large = (void *)(page2pa(page) + va_pa_offset);
    large->magic = LARGE_MAGIC;
    large->pages = count;
    large->requested = size;
    return large + 1;
}

size_t ksize(void *object) {
    if (object == NULL) {
        return 0;
    }
    struct slab *slab = object_slab(object);
    if (slab->magic == LARGE_MAGIC) {
        struct large_object *large = (void *)slab;
        assert(object == large + 1);
        return large->requested;
    }
    assert(slab->magic == SLAB_MAGIC);
    size_t index = object_index(slab, object);
    assert(slab->allocated[index / 64] & ((uint64_t)1 << (index % 64)));
    return slab->cache->size;
}

void kfree(void *object) {
    if (object == NULL) {
        return;
    }
    struct slab *slab = object_slab(object);
    if (slab->magic == LARGE_MAGIC) {
        struct large_object *large = (void *)slab;
        assert(object == large + 1);
        size_t count = large->pages;
        large->magic = 0;
        free_pages(pa2page(PADDR(large)), count);
    } else {
        assert(slab->magic == SLAB_MAGIC && slab->cache->general);
        kmem_cache_free(slab->cache, object);
    }
}

size_t slub_shrink(void) {
    size_t count = 0;
    for (unsigned i = 0; i < CACHE_LIMIT; i++) {
        if (caches[i].used) {
            count += kmem_cache_shrink(&caches[i]);
        }
    }
    return count;
}
