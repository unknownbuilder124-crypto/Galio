/* pmem.c - Physical memory manager */
#include "pmem.h"
#include "kprintf.h"

#define FRAME_SIZE 4096
#define FRAMES_PER_BYTE 8
#define BITMAP_SIZE (128 * 1024 * 1024 / FRAME_SIZE / 8)  /* For 128MB */

static u8 frame_bitmap[BITMAP_SIZE] = {0};
static u32 total_frames = 0;
static u32 used_frames = 0;
static u32 kernel_frames = 0;

#define FRAME_MASK(frame) ((frame) / 8)
#define BIT_MASK(frame)   (1 << ((frame) % 8))

static void set_frame(u32 frame) {
    frame_bitmap[FRAME_MASK(frame)] |= BIT_MASK(frame);
}

static void unset_frame(u32 frame) {
    frame_bitmap[FRAME_MASK(frame)] &= ~BIT_MASK(frame);
}

static u8 get_frame(u32 frame) {
    return frame_bitmap[FRAME_MASK(frame)] & BIT_MASK(frame);
}

void pmem_init(u32 mmap_addr, u32 mmap_length) {
    u32 kernel_end = 0x400000;  /* Approximate kernel end */
    
    kprintf("Physical memory manager initializing...\n");

    /* Mark all memory as used initially */
    for (u32 frame = 0; frame < BITMAP_SIZE * 8; frame++) {
        set_frame(frame);
    }

    /* If no valid mmap, assume 128MB is available */
    if (mmap_addr == 0 || mmap_length == 0) {
        kprintf("No Multiboot memory map, assuming 128 MB available\n");
        
        /* Assume memory from 0x100000 to 0x8000000 is available (126 MB) */
        u32 start_frame = 0x100000 / FRAME_SIZE;
        u32 end_frame = 0x8000000 / FRAME_SIZE;
        
        for (u32 frame = start_frame; frame < end_frame; frame++) {
            unset_frame(frame);
            total_frames++;
        }
    } else {
        kprintf("Multiboot mmap: addr=%x, len=%d\n", mmap_addr, mmap_length);
        
        /* Parse Multiboot memory map */
        mmap_entry_t *entry = (mmap_entry_t *)mmap_addr;
        while ((u32)entry < mmap_addr + mmap_length) {
            u32 addr = entry->addr_low;
            u32 len = entry->len_low;
            u32 next = (u32)entry + entry->size + sizeof(entry->size);

            if (entry->type == MMAP_AVAILABLE && len > 0) {
                u32 start_frame = addr / FRAME_SIZE;
                u32 end_frame = (addr + len) / FRAME_SIZE;

                for (u32 frame = start_frame; frame < end_frame; frame++) {
                    unset_frame(frame);
                    total_frames++;
                }

                kprintf("  Available: %x - %x (%u MB)\n",
                        addr, addr + len, len / (1024*1024));
            }

            entry = (mmap_entry_t *)next;
        }
    }

    /* Mark kernel space as used */
    u32 kernel_start_frame = 0x100000 / FRAME_SIZE;
    u32 kernel_end_frame = kernel_end / FRAME_SIZE;
    for (u32 frame = kernel_start_frame; frame < kernel_end_frame; frame++) {
        if (!get_frame(frame)) {
            set_frame(frame);
            used_frames++;
            kernel_frames++;
        }
    }

    kprintf("Physical memory: total=%u MB, kernel=%u KB, free=%u MB\n",
            total_frames * FRAME_SIZE / (1024 * 1024),
            kernel_frames * FRAME_SIZE / 1024,
            (total_frames - used_frames) * FRAME_SIZE / (1024 * 1024));

    /* Mark frame 0 as used to avoid NULL pointer allocations */
    set_frame(0);
    used_frames++;
}

u32 pmem_alloc(size_t num_frames) {
    for (u32 frame = 0; frame < BITMAP_SIZE * 8; frame++) {
        if (!get_frame(frame)) {
            /* Check if we have enough contiguous frames */
            u8 found = 1;
            for (u32 i = 1; i < num_frames; i++) {
                if (get_frame(frame + i)) {
                    found = 0;
                    break;
                }
            }
            
            if (found) {
                for (u32 i = 0; i < num_frames; i++) {
                    set_frame(frame + i);
                }
                used_frames += num_frames;
                return frame * FRAME_SIZE;
            }
        }
    }
    
    kprintf("pmem_alloc: Out of physical memory\n");
    return 0;
}

void pmem_free(u32 addr, size_t num_frames) {
    u32 frame = addr / FRAME_SIZE;
    for (u32 i = 0; i < num_frames; i++) {
        if (get_frame(frame + i)) {
            unset_frame(frame + i);
            used_frames--;
        }
    }
}

void pmem_claim(u32 addr, size_t num_frames) {
    u32 frame = addr / FRAME_SIZE;
    for (u32 i = 0; i < num_frames; i++) {
        set_frame(frame + i);
    }
    used_frames += num_frames;
}

u32 pmem_get_total(void) {
    return total_frames * FRAME_SIZE;
}

u32 pmem_get_used(void) {
    return used_frames * FRAME_SIZE;
}

u32 pmem_get_free(void) {
    return (total_frames - used_frames) * FRAME_SIZE;
}
