#!/usr/bin/env bash
set -euo pipefail

# iso.sh — create a bootable GRUB ISO from galio.bin and optionally run QEMU
# Usage: ./iso.sh [--run-qemu] [--qemu-args "<args>"]
# Example: ./iso.sh --run-qemu --qemu-args "-m 128M -serial file:serial.log"

OUT_ISO="galio.iso"
ISO_DIR="iso"
KERNEL_BIN="galio.bin"
DISK_IMG="disk.img"
DISK_SIZE_MB=16
GRUB_CFG_PATH="${ISO_DIR}/boot/grub/grub.cfg"
RUN_QEMU=false
QEMU_ARGS="-m 128M -serial file:serial.log -monitor none -no-reboot"

# parse args
while [ $# -gt 0 ]; do
  case "$1" in
    --run-qemu) RUN_QEMU=true; shift ;;
    --qemu-args) QEMU_ARGS="$2"; shift 2 ;;
    --help|-h) echo "Usage: $0 [--run-qemu] [--qemu-args \"<args>\"]"; exit 0 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

# sanity checks
command -v grub-mkrescue >/dev/null 2>&1 || { echo "Error: grub-mkrescue not found in PATH"; exit 1; }
if [ ! -f "${KERNEL_BIN}" ]; then
  echo "Error: ${KERNEL_BIN} not found. Build the kernel first (make)."
  exit 1
fi

# prepare ISO tree
rm -rf "${ISO_DIR}"
mkdir -p "${ISO_DIR}/boot/grub"
cp "${KERNEL_BIN}" "${ISO_DIR}/boot/${KERNEL_BIN}"

# write grub.cfg
cat > "${GRUB_CFG_PATH}" <<'EOF'
set timeout=0
set default=0

menuentry "Galio Kernel" {
  multiboot /boot/galio.bin
  boot
}
EOF

# create persistent ext2 disk image if needed
if [ ! -f "${DISK_IMG}" ]; then
  echo "Creating ${DISK_IMG} (${DISK_SIZE_MB} MB) ..."
  dd if=/dev/zero of="${DISK_IMG}" bs=1M count="${DISK_SIZE_MB}" status=none
  if command -v mke2fs >/dev/null 2>&1; then
    mke2fs -q -F -t ext2 -b 1024 "${DISK_IMG}"
  elif command -v mkfs.ext2 >/dev/null 2>&1; then
    mkfs.ext2 -q -F -b 1024 "${DISK_IMG}"
  else
    echo "Error: mke2fs or mkfs.ext2 not found. Install e2fsprogs." >&2
    exit 1
  fi
fi

# build ISO
echo "Creating ${OUT_ISO}..."
grub-mkrescue -o "${OUT_ISO}" "${ISO_DIR}"

echo "ISO created: ${OUT_ISO}"

echo "Persistent EXT2 disk image: ${DISK_IMG}"

# optionally run QEMU
if [ "${RUN_QEMU}" = true ]; then
  echo "Starting QEMU with args: ${QEMU_ARGS}"
  qemu-system-i386 -cdrom "${OUT_ISO}" -drive file="${DISK_IMG}",format=raw,if=ide,cache=writeback,index=0,media=disk ${QEMU_ARGS}
fi
