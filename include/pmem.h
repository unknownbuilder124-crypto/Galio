#ifndef PMEM_H
#define PMEM_H

#include "common.h"
#include <stddef.h>

/* Physical memory management */

typedef struct {
    u32 size;
    u32 addr_low;
    u32 addr_high;
    u32 len_low;
    u32 len_high;
    u32 type;
} mmap_entry_t;

/* Memory types from Multiboot */
#define MMAP_AVAILABLE 1
#define MMAP_RESERVED  2

/* Initialize physical memory manager from Multiboot memory map */
void pmem_init(u32 mmap_addr, u32 mmap_length);

/* Allocate contiguous physical frames */
u32 pmem_alloc(size_t num_frames);

/* Free physical frames */
void pmem_free(u32 addr, size_t num_frames);

/* Get available physical memory */
u32 pmem_get_total(void);
u32 pmem_get_used(void);
u32 pmem_get_free(void);

/* Mark region as used/free */
void pmem_claim(u32 addr, size_t num_frames);

#endif /* PMEM_H */
