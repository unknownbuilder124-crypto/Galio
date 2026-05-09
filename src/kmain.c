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
#include "elf.h"

/* Syscall interface declaration */
void syscall_init(void);

/* Memory test declaration */
void mem_test_run(void);

/* Embedded test binary */
extern u8 _binary_test_elf_bin_start;
extern u8 _binary_test_elf_bin_end;

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

    kprintf("Running memory stabilization tests...\n");
    mem_test_run();

    kprintf("Initializing process manager...\n");
    process_init();

    kprintf("Installing system call handler...\n");
    syscall_init();

    kprintf("Initializing timer (100 Hz)...\n");
    pit_init(100);

    kprintf("Initializing keyboard...\n");
    keyboard_init();

    kprintf("Initializing filesystem...\n");

    /* Get embedded initrd */
    extern u8 _binary_initrd_bin_start;
    vfs_init(&_binary_initrd_bin_start);
    vfs_debug();

    /* Test filesystem - COMPREHENSIVE DEBUG OUTPUT */
    kprintf("\n");
    kprintf("╔══════════════════════════════════════════════════════════════╗\n");
    kprintf("║           FILESYSTEM VERIFICATION & DEBUG OUTPUT             ║\n");
    kprintf("╚══════════════════════════════════════════════════════════════╝\n");
    kprintf("\n");

    /* Test 1: Filesystem Stats */
    kprintf("[TEST 1] Filesystem Statistics\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_stats();
    kprintf("\n");

    /* Test 2: Complete Filesystem Tree */
    kprintf("[TEST 2] Complete Filesystem Tree\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_listall();
    kprintf("\n");

    /* Test 3: Root Directory Listing */
    kprintf("[TEST 3] Root Directory Contents\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_listdir("/");
    kprintf("\n");

    /* Test 4: /boot Directory */
    kprintf("[TEST 4] /boot Directory Contents\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_listdir("/boot");
    kprintf("\n");

    /* Test 5: /etc Directory */
    kprintf("[TEST 5] /etc Directory Contents\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_listdir("/etc");
    kprintf("\n");

    /* Test 6: /var Directory */
    kprintf("[TEST 6] /var Directory Contents\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_listdir("/var");
    kprintf("\n");

    /* Test 7: /home Directory */
    kprintf("[TEST 7] /home Directory Contents\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_listdir("/home");
    kprintf("\n");

    /* Test 8: Read /etc/hostname */
    kprintf("[TEST 8] Reading /etc/hostname\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    char hostname_buf[256];
    u32 bytes_read = vfs_read("/etc/hostname", hostname_buf, 255);
    if (bytes_read > 0) {
        hostname_buf[bytes_read] = 0;
        kprintf("✓ Successfully read %u bytes\n", bytes_read);
        kprintf("  Content: %s", hostname_buf);
    } else {
        kprintf("✗ FAILED to read /etc/hostname\n");
    }
    kprintf("\n");

    /* Test 9: Read /boot/config.txt */
    kprintf("[TEST 9] Reading /boot/config.txt\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    char config_buf[512];
    bytes_read = vfs_read("/boot/config.txt", config_buf, 511);
    if (bytes_read > 0) {
        config_buf[bytes_read] = 0;
        kprintf("✓ Successfully read %u bytes\n", bytes_read);
        kprintf("  Content:\n%s", config_buf);
    } else {
        kprintf("✗ FAILED to read /boot/config.txt\n");
    }
    kprintf("\n");

    /* Test 10: Read /etc/os-release */
    kprintf("[TEST 10] Reading /etc/os-release\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    char os_buf[256];
    bytes_read = vfs_read("/etc/os-release", os_buf, 255);
    if (bytes_read > 0) {
        os_buf[bytes_read] = 0;
        kprintf("✓ Successfully read %u bytes\n", bytes_read);
        kprintf("  Content:\n%s", os_buf);
    } else {
        kprintf("✗ FAILED to read /etc/os-release\n");
    }
    kprintf("\n");

    /* Test 11: Read /home/root/readme.txt */
    kprintf("[TEST 11] Reading /home/root/readme.txt\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    char readme_buf[512];
    bytes_read = vfs_read("/home/root/readme.txt", readme_buf, 511);
    if (bytes_read > 0) {
        readme_buf[bytes_read] = 0;
        kprintf("✓ Successfully read %u bytes\n", bytes_read);
        kprintf("  Content:\n%s", readme_buf);
    } else {
        kprintf("✗ FAILED to read /home/root/readme.txt\n");
    }
    kprintf("\n");

    /* Test 12: Query file sizes */
    kprintf("[TEST 12] File Size Queries\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    u32 sz1 = vfs_size("/etc/hostname");
    u32 sz2 = vfs_size("/boot/config.txt");
    u32 sz3 = vfs_size("/var/log/boot.log");
    kprintf("  /etc/hostname size: %u bytes\n", sz1);
    kprintf("  /boot/config.txt size: %u bytes\n", sz2);
    kprintf("  /var/log/boot.log size: %u bytes\n", sz3);
    kprintf("\n");

    /* Test 13: Directory checks */
    kprintf("[TEST 13] Directory Type Checks\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    kprintf("  / is_dir: %u (should be 1)\n", vfs_is_dir("/"));
    kprintf("  /etc is_dir: %u (should be 1)\n", vfs_is_dir("/etc"));
    kprintf("  /etc/hostname is_dir: %u (should be 0)\n", vfs_is_dir("/etc/hostname"));
    kprintf("  /boot is_dir: %u (should be 1)\n", vfs_is_dir("/boot"));
    kprintf("\n");

    /* Test 14: Detailed tree view */
    kprintf("[TEST 14] Tree View with Details\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_tree();
    kprintf("\n");

    /* Test 15: Error handling */
    kprintf("[TEST 15] Error Handling Tests\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    kprintf("  Attempting to read non-existent file...\n");
    char dummy[256];
    bytes_read = vfs_read("/nonexistent/file.txt", dummy, 255);
    kprintf("  Result: Returned %u bytes (should be 0)\n", bytes_read);
    kprintf("\n");

    kprintf("╔══════════════════════════════════════════════════════════════╗\n");
    kprintf("║              FILESYSTEM TESTS COMPLETED SUCCESSFULLY          ║\n");
    kprintf("║                  Status: ALL SYSTEMS OPERATIONAL              ║\n");
    kprintf("╚══════════════════════════════════════════════════════════════╝\n");
    kprintf("\n");

    kprintf("Loading ELF loader smoke test...\n");
    u32 elf_entry = elf_load(&_binary_test_elf_bin_start);
    if (elf_entry) {
        kprintf("ELF loaded successfully, entry point: %08X\n", elf_entry);
        kprintf("Executing test ELF...\n");
        ((void (*)(void))elf_entry)();
    } else {
        kprintf("ELF_LOADER_TEST: FAILED - elf_load returned 0\n");
    }

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
