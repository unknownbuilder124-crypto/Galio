/* paging.c - Virtual memory paging */
#include "paging.h"
#include "pmem.h"
#include "kprintf.h"

#define PAGE_SIZE 4096
#define TABLE_SIZE 1024

static page_directory_t kernel_pd_storage;
static page_directory_t *kernel_pd = NULL;

/* Assembly function to enable paging */
extern void paging_enable_asm(u32 pd_addr);

void paging_init(void) {
    kprintf("Initializing paging system...\n");
    kernel_pd = paging_create_directory();
    if (!kernel_pd) {
        kprintf("paging_init: Failed to create page directory\n");
        panic("Paging initialization failed");
    }

    /* Identity map first 4MB of memory so the kernel is still valid after paging is enabled */
    for (u32 i = 0; i < 4 * 1024 * 1024; i += PAGE_SIZE) {
        paging_map(kernel_pd, i, i, PAGE_PRESENT | PAGE_RW);
    }

    /* Enable paging */
    paging_enable(kernel_pd);
    kprintf("Paging enabled\n");
}

page_directory_t *paging_create_directory(void) {
    kernel_pd_storage.directory = (u32 *)pmem_alloc(1);
    if (!kernel_pd_storage.directory) {
        return NULL;
    }

    for (int i = 0; i < 1024; ++i) {
        kernel_pd_storage.directory[i] = 0;
        kernel_pd_storage.tables[i] = NULL;
    }

    return &kernel_pd_storage;
}

void paging_map(page_directory_t *pd, u32 vaddr, u32 paddr, u32 flags) {
    u32 pd_index = (vaddr >> 22) & 0x3FF;
    u32 pt_index = (vaddr >> 12) & 0x3FF;

    if (!pd->tables[pd_index]) {
        pd->tables[pd_index] = (u32 *)pmem_alloc(1);
        if (!pd->tables[pd_index]) {
            kprintf("paging_map: Failed to allocate page table\n");
            return;
        }
        for (int i = 0; i < 1024; ++i) {
            pd->tables[pd_index][i] = 0;
        }
        pd->directory[pd_index] = ((u32)pd->tables[pd_index] & 0xFFFFF000) | PAGE_PRESENT | PAGE_RW;
    }

    u32 pte = (paddr & 0xFFFFF000) | flags;
    pd->tables[pd_index][pt_index] = pte;
    pd->directory[pd_index] = ((u32)pd->tables[pd_index] & 0xFFFFF000) | PAGE_PRESENT | PAGE_RW;

    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

void paging_unmap(page_directory_t *pd, u32 vaddr) {
    u32 pd_index = (vaddr >> 22) & 0x3FF;
    u32 pt_index = (vaddr >> 12) & 0x3FF;

    if (pd->tables[pd_index]) {
        pd->tables[pd_index][pt_index] = 0;
        __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
    }
}

u32 paging_get_physical(page_directory_t *pd, u32 vaddr) {
    u32 pd_index = (vaddr >> 22) & 0x3FF;
    u32 pt_index = (vaddr >> 12) & 0x3FF;

    if (!pd->tables[pd_index]) {
        return 0;
    }

    u32 pte = pd->tables[pd_index][pt_index];
    if (!(pte & PAGE_PRESENT)) {
        return 0;
    }

    return (pte & 0xFFFFF000) | (vaddr & 0xFFF);
}

void paging_enable(page_directory_t *pd) {
    kernel_pd = pd;
    paging_enable_asm((u32)pd->directory);
}

page_directory_t *paging_get_current(void) {
    return kernel_pd;
}
