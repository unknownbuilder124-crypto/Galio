# Simple build for a 32-bit freestanding kernel
CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Iinclude -Itools/shell -Itools/shell/commands -Itools/shell/editor
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T boot/linker.ld

SRCS = src/kernel.c src/kmain.c src/vga.c src/gdt.c src/idt.c src/irq.c src/isr.c src/kprintf.c \
       src/serial.c src/pmem.c src/paging.c src/heap.c src/pit.c src/keyboard.c src/process.c \
       src/syscall.c src/elf.c src/vfs_wrapper.c src/vfs_core.c src/string.c src/mem_test.c src/auth.c \
       src/scheduler.c src/init.c src/ata.c src/ext2.c \
       tools/shell/shell.c \
       tools/shell/commands/file.c tools/shell/commands/new.c \
       tools/shell/commands/show.c tools/shell/commands/write.c \
       tools/shell/commands/recycle.c tools/shell/commands/clean.c tools/shell/commands/delete.c \
       tools/shell/editor/editor.c
OBJS = $(SRCS:.c=.o) src/asm.o src/isr_asm.o boot/boot.o src/embedded_test.o src/embedded_initrd.o
TEST_ELF = test_elf.bin
INITRD_IMAGE = initrd.bin

src/embedded_test.o: $(TEST_ELF)
	objcopy -I binary -O elf32-i386 -B i386 $< $@

test_elf.bin: test/test_elf.c
	gcc -m32 -ffreestanding -nostdlib -Wl,--entry=_start -Wl,-Ttext=0x10000 $< -o $@

tools/mkiofs: tools/mkiofs.c
	gcc -o $@ $<

$(INITRD_IMAGE): tools/mkiofs
	./tools/mkiofs $@

src/embedded_initrd.o: $(INITRD_IMAGE)
	objcopy -I binary -O elf32-i386 -B i386 $< $@

.PHONY: all clean run disk

all: galio.bin galio.iso disk.img

galio.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

disk.img:
	@echo "Creating 64MB disk image..."
	@dd if=/dev/zero of=disk.img bs=1M count=64 2>/dev/null || true
	@command -v mkfs.ext2 >/dev/null 2>&1 && mkfs.ext2 disk.img -q || echo "Warning: mkfs.ext2 not found, disk image created but not formatted"
	@echo "Disk image created and formatted as ext2"

galio.iso: galio.bin
	@command -v grub-mkrescue >/dev/null 2>&1 || { echo "Error: grub-mkrescue not found in PATH"; exit 1; }
	@rm -rf iso
	@mkdir -p iso/boot/grub
	@cp galio.bin iso/boot/galio.bin
	@printf '%s\n' 'set timeout=0' 'set default=0' '' 'menuentry "Galio Kernel" {' '  multiboot /boot/galio.bin' '  boot' '}' > iso/boot/grub/grub.cfg
	@echo "Creating galio.iso..."
	@grub-mkrescue -o galio.iso iso

boot/boot.o: boot/boot.S
	$(CC) $(CFLAGS) -c boot/boot.S -o boot/boot.o

src/asm.o: src/asm.s
	$(AS) $(ASFLAGS) src/asm.s -o src/asm.o

src/isr_asm.o: src/isr_asm.s
	$(AS) $(ASFLAGS) src/isr_asm.s -o src/isr_asm.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f galio.bin galio.iso disk.img $(OBJS) tools/mkiofs $(INITRD_IMAGE)
	rm -rf iso

run: galio.bin
	qemu-system-i386 -kernel galio.bin -m 128M -serial stdio