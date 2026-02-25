# tenstorrent-qemu-update

[中文说明](README_zh.md)

QEMU device emulation for Tenstorrent Blackhole AI accelerator chip. This module implements a PCIe device that emulates the complete Blackhole chip, including:

- **17x12 NOC mesh** with 204 routers (NOC0 + NOC1)
- **140 Tensix compute nodes** (7 RV32IMC cores + Tensix coprocessor per node)
- **14 Active Ethernet nodes**
- **ARC management processor** with message queue
- **PCIe BAR0/BAR2/BAR4** register space
- **TLB address translation** (2M and 4G windows)
- **NOC read/write/atomic** data transfer operations
- **8 DRAM banks**

## Prerequisites

- QEMU 10.x or later (source code)
- [tenstorrent-rv32-emu](https://github.com/xxx/tenstorrent-rv32-emu) - RV32IMC emulator with Tensix coprocessor
- [tenstorrent-coroutine-lib](https://github.com/xxx/tenstorrent-coroutine-lib) - Coroutine scheduling library

## Installation

### Automatic (Recommended)

Use the main project [tenstorrent-blackhole-emulator](https://github.com/xxx/tenstorrent-blackhole-emulator) which handles all setup automatically.

### Manual

1. Clone this repo as a sibling of your QEMU source directory:

```bash
mkdir blackhole-project && cd blackhole-project
git clone https://github.com/qemu/qemu.git
git clone https://github.com/xxx/tenstorrent-qemu-update.git qemu_update
```

2. Run the patch script:

```bash
cd qemu_update
./patch_qemu.sh ../qemu
```

3. Build QEMU:

```bash
cd ../qemu
mkdir build && cd build
../configure --target-list=x86_64-softmmu --enable-kvm
make -j$(nproc)
```

## Files

| File | Description |
|------|-------------|
| `tenstorrent.c` | Main device: PCIe, NOC, ARC message queue, node lifecycle |
| `tenstorrent.h` | Device state, register definitions, TLB structures, memory map |
| `blackhole.c` | Blackhole board initialization |
| `blackhole.h` | Node types, grid layout, firmware addresses, telemetry |
| `coroutine_lib.h` | Header for coroutine scheduling library |
| `rv32sim.h` | Header for RV32IMC emulator |
| `patch_qemu.sh` | Script to patch QEMU source tree |

## Architecture

```
Host (x86_64)
  └── QEMU
       └── PCIe Device (tenstorrent)
            ├── BAR0 (512MB) ── MMIO register access
            ├── BAR2 (16MB)  ── iATU configuration
            ├── BAR4 (32GB)  ── 4G TLB window
            ├── NOC0/NOC1    ── 17x12 mesh network
            ├── Tensix nodes ── 140 nodes (7 cores each)
            ├── ETH nodes    ── 14 Active Ethernet
            ├── ARC node     ── management processor
            └── DRAM banks   ── 8 banks
```

## Log Levels

Control output verbosity by defining `TT_LOG_LEVEL` before compilation:

| Level | Value | Content |
|-------|-------|---------|
| ERROR | 0 | Errors only |
| INFO | 1 | Node start/stop, reset events (default) |
| VERBOSE | 2 | NOC operations, config writes |
| DEBUG | 3 | Per-core details |

## License

This project is for research and educational purposes.
