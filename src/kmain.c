#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "kprintf.h"
#include "serial.h"
#include "pmem.h"
#include "paging.h"
#include "heap.h"
#include "pit.h"
#include "keyboard.h"
#include "process.h"
#include "vfs.h"

/* Syscall interface declaration */
void syscall_init(void);

/* Entry point from bootloader - receives Multiboot info */
void kmain(void *multiboot_ptr) {
    (void)multiboot_ptr;  /* Mark as used to suppress warning */

    serial_init();
    kprintf("=== Galio Kernel Boot ===\n\n");

    kprintf("Initializing VGA...\n");
    vga_init();

    kprintf("Initializing GDT...\n");
    gdt_init();

    kprintf("Initializing IDT...\n");
    idt_init();

    kprintf("Installing IRQ handlers...\n");
    irq_install();

    kprintf("Initializing physical memory manager...\n");

    typedef struct {
        u32 flags;
        u32 mem_lower;
        u32 mem_upper;
        u32 boot_device;
        u32 cmdline;
        u32 mods_count;
        u32 mods_addr;
        u32 syms[4];
        u32 mmap_length;
        u32 mmap_addr;
    } multiboot_info_t;

    multiboot_info_t *mb_info = (multiboot_info_t *)multiboot_ptr;
    u32 mmap_addr = 0;
    u32 mmap_length = 0;

    if (mb_info && (mb_info->flags & (1 << 6))) {
        mmap_addr = mb_info->mmap_addr;
        mmap_length = mb_info->mmap_length;
        kprintf("Found Multiboot mmap: addr=%x len=%u\n", mmap_addr, mmap_length);
    } else {
        kprintf("No Multiboot mmap available, using fallback\n");
    }

    pmem_init(mmap_addr, mmap_length);

    kprintf("Initializing paging...\n");
    paging_init();

    kprintf("Initializing heap...\n");
    heap_init();

    kprintf("Initializing process manager...\n");
    process_init();

    kprintf("Installing system call handler...\n");
    syscall_init();

    kprintf("Initializing timer (100 Hz)...\n");
    pit_init(100);

    kprintf("Initializing keyboard...\n");
    keyboard_init();

    kprintf("Initializing filesystem...\n");
    vfs_init(NULL);  /* No initrd mounted yet */

    kprintf("\n");
    kprintf("=== Galio Kernel Fully Initialized ===\n");
    kprintf("System ready. Entering idle loop...\n");
    kprintf("PID=%u, Uptime=0 ticks\n\n", process_current()->pid);

    /* Enable interrupts */
    __asm__ volatile("sti");

    u32 last_print = 0;
    for (;;) {
        __asm__ volatile("hlt");

        u32 ticks = pit_get_ticks();
        if (ticks - last_print >= 100) {  // 100 ticks = 1 second at 100 Hz
            kernel_status();
            last_print = ticks;
        }
    }
}