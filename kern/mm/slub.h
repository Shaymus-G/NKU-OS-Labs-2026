#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

struct kmem_cache;
struct slub_cache_stats {
    size_t object_size, stride, capacity, slabs, live;
};

/* Single-core teaching SLUB: caller serializes access. Constructor runs
 * on every allocation; its name and function must outlive the cache. */
void slub_init(void);
struct kmem_cache *kmem_cache_create(const char *name, size_t size,
                                    size_t align, void (*ctor)(void *));
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *object);
size_t kmem_cache_shrink(struct kmem_cache *cache);
bool kmem_cache_destroy(struct kmem_cache *cache);
struct slub_cache_stats kmem_cache_stats(struct kmem_cache *cache);
void *kmalloc(size_t size);
void kfree(void *object);
size_t ksize(void *object);
size_t slub_shrink(void);
void slub_check(void);

#endif
