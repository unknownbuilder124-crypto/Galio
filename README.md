# Galio — Minimal 32-bit kernel for LIDE

**Galio** is a small, bootable 32‑bit kernel foundation intended as the initial kernel for the LIDE OS project. It provides a minimal protected‑mode runtime and early platform services so you can extend it with drivers, memory management, and userspace support.

---

### Quick overview

**What it includes**
- Multiboot v1 header so GRUB can load the kernel.
- Early entry and stack setup in assembly.
- Basic VGA text output driver (print, newline, scroll).
- GDT setup (null, code, data) and loader.
- IDT skeleton and loader.
- PIC remapping and IRQ masking helpers.
- Small C runtime helpers (`memcpy`, `memset`, `panic`).
- Build system and instructions to produce a GRUB ISO and run in QEMU.

**What it does not include**
- Full ISR/IRQ handlers (stubs only).
- Keyboard or PIT drivers.
- Memory allocator, paging, or ELF userspace loader.
- Scheduler or process model.

---

### Prerequisites

Install required packages on Debian/Ubuntu/Kali:

```bash
sudo apt update
sudo apt install -y build-essential gcc-multilib libc6-dev-i386 nasm binutils \
                    grub-pc-bin xorriso mtools qemu-system-i386

Build and run

From the project root:

# build kernel
make

# prepare ISO tree and GRUB config
mkdir -p iso/boot/grub
cp -f galio.bin iso/boot/galio.bin
cat > iso/boot/grub/grub.cfg <<'EOF'
set timeout=5
set default=0

menuentry "Galio kernel" {
  multiboot /boot/galio.bin
  boot
}
EOF

# create ISO
grub-mkrescue -o galio.iso iso

# run in QEMU with serial output to terminal
qemu-system-i386 -cdrom galio.iso -m 128M -serial stdio


Notes

    If grub-mkrescue fails, ensure grub-pc-bin, xorriso, and mtools are installed.

    Use qemu-system-i386 -cdrom galio.iso -m 128M to view VGA output in a window.

Project layout

Top level

    Makefile — build rules for compiling and linking the kernel.

    galio.bin — linked kernel image (after build).

    galio.iso — GRUB ISO (after grub-mkrescue).

    README.md — this file.

Directories

    boot — boot.S (Multiboot header + entry), linker.ld.

    include — public headers: common.h, vga.h, gdt.h, idt.h, irq.h.

    src — kernel sources:

        asm.s — assembly wrappers (gdt_flush, idt_load).

        kmain.c — kernel entry and init sequence.

        kernel.c — small runtime helpers.

        vga.c — VGA text driver.

        gdt.c — GDT setup.

        idt.c — IDT skeleton.

        irq.c — PIC remap and I/O helpers.

    iso — ISO tree used to build galio.iso.

