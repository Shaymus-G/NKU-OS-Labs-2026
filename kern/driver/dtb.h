#ifndef __KERN_DRIVER_DTB_H__
#define __KERN_DRIVER_DTB_H__

#include <defs.h>

// Defined in entry.S
extern uint64_t boot_hartid;
extern uint64_t boot_dtb;

void dtb_init(void);
#define DTB_INVALID (-1)
#define DTB_NO_MEMORY (-2)
#define DTB_UNSUPPORTED (-3)
/* One contiguous bank; length is readable storage. Failure preserves outputs. */
int dtb_parse_memory(const void *blob, size_t length,
                     uint64_t *base, uint64_t *size);
uint64_t get_memory_base(void);
uint64_t get_memory_size(void);

#endif /* !__KERN_DRIVER_DTB_H__ */
