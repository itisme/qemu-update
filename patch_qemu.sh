#!/bin/bash
# patch_qemu.sh - Patch QEMU source tree for Tenstorrent Blackhole device
#
# Usage: ./patch_qemu.sh <path-to-qemu>
# Example: ./patch_qemu.sh ../qemu

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QEMU_DIR="${1:?Usage: $0 <path-to-qemu>}"

# Verify QEMU directory
if [ ! -f "$QEMU_DIR/hw/misc/meson.build" ]; then
    echo "Error: $QEMU_DIR does not appear to be a QEMU source directory."
    echo "Expected to find hw/misc/meson.build"
    exit 1
fi

QEMU_DIR="$(cd "$QEMU_DIR" && pwd)"
MISC_DIR="$QEMU_DIR/hw/misc"

echo "Patching QEMU at: $QEMU_DIR"
echo "Source files from: $SCRIPT_DIR"

# Copy source files
echo "Copying source files to $MISC_DIR ..."
cp "$SCRIPT_DIR/tenstorrent.c" "$MISC_DIR/"
cp "$SCRIPT_DIR/tenstorrent.h" "$MISC_DIR/"
cp "$SCRIPT_DIR/blackhole.c"   "$MISC_DIR/"
cp "$SCRIPT_DIR/blackhole.h"   "$MISC_DIR/"
cp "$SCRIPT_DIR/coroutine_lib.h" "$MISC_DIR/"
cp "$SCRIPT_DIR/rv32sim.h"     "$MISC_DIR/"

# Check if meson.build already patched
if grep -q "tenstorrent" "$MISC_DIR/meson.build"; then
    echo "meson.build already contains tenstorrent entry, skipping."
else
    echo "Patching meson.build ..."
    cat >> "$MISC_DIR/meson.build" << 'MESON_EOF'

# tenstorrent blackhole emulator
tt_emu_dir = meson.project_source_root() / '..'
rv32sim_dir = tt_emu_dir / 'rv32-emu'
coroute_dir = tt_emu_dir / 'coroutine-lib'
system_ss.add(when: 'CONFIG_PCI', if_true: [
	files('tenstorrent.c', 'blackhole.c'),
	declare_dependency(
	    link_args: [
		'-L' + rv32sim_dir,
		'-L' + coroute_dir,
		'-Wl,-rpath,' + rv32sim_dir,
		'-Wl,-rpath,' + coroute_dir,
		'-lrv32sim', '-lcoroutine_lib'
	    ]
	)
])
MESON_EOF
fi

echo ""
echo "Patch complete!"
echo ""
echo "Next steps:"
echo "  cd $QEMU_DIR"
echo "  mkdir -p build && cd build"
echo "  ../configure --target-list=x86_64-softmmu --enable-kvm"
echo "  make -j\$(nproc)"
