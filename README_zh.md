# tenstorrent-qemu-update

[English](README.md)

Tenstorrent Blackhole AI 加速芯片的 QEMU 设备模拟模块。实现了完整的 Blackhole 芯片 PCIe 设备模拟，包括：

- **17x12 NOC 网格**：204 个路由器（NOC0 + NOC1 双网络）
- **140 个 Tensix 计算节点**：每节点 7 个 RV32IMC 核心 + Tensix 协处理器
- **14 个 Active Ethernet 节点**
- **ARC 管理处理器**：4 个消息队列
- **PCIe BAR0/BAR2/BAR4** 寄存器空间
- **TLB 地址翻译**：202 个 2M 窗口 + 8 个 4G 窗口
- **NOC read/write/atomic** 数据传输操作
- **8 个 DRAM bank**

## 依赖

- QEMU 10.x 或更高版本（源码）
- [tenstorrent-rv32-emu](https://github.com/xxx/tenstorrent-rv32-emu) - RV32IMC 模拟器及 Tensix 协处理器
- [tenstorrent-coroutine-lib](https://github.com/xxx/tenstorrent-coroutine-lib) - 协程调度库

## 安装

### 自动安装（推荐）

使用主项目 [tenstorrent-blackhole-emulator](https://github.com/xxx/tenstorrent-blackhole-emulator)，自动完成所有配置。

### 手动安装

1. 将本仓库克隆到 QEMU 源码的同级目录：

```bash
mkdir blackhole-project && cd blackhole-project
git clone https://github.com/qemu/qemu.git
git clone https://github.com/xxx/tenstorrent-qemu-update.git qemu_update
```

2. 运行 patch 脚本：

```bash
cd qemu_update
./patch_qemu.sh ../qemu
```

3. 编译 QEMU：

```bash
cd ../qemu
mkdir build && cd build
../configure --target-list=x86_64-softmmu --enable-kvm
make -j$(nproc)
```

## 文件说明

| 文件 | 说明 |
|------|------|
| `tenstorrent.c` | 主设备：PCIe、NOC、ARC 消息队列、节点生命周期管理 |
| `tenstorrent.h` | 设备状态、寄存器定义、TLB 结构、内存映射 |
| `blackhole.c` | Blackhole 板级初始化 |
| `blackhole.h` | 节点类型、网格布局、固件地址、遥测数据 |
| `coroutine_lib.h` | 协程调度库头文件 |
| `rv32sim.h` | RV32IMC 模拟器头文件 |
| `patch_qemu.sh` | QEMU 源码 patch 脚本 |

## 架构

```
宿主机 (x86_64)
  └── QEMU
       └── PCIe 设备 (tenstorrent)
            ├── BAR0 (512MB) ── MMIO 寄存器访问
            ├── BAR2 (16MB)  ── iATU 配置
            ├── BAR4 (32GB)  ── 4G TLB 窗口
            ├── NOC0/NOC1    ── 17x12 网格网络
            ├── Tensix 节点  ── 140 个节点（每节点 7 核）
            ├── ETH 节点     ── 14 个 Active Ethernet
            ├── ARC 节点     ── 管理处理器
            └── DRAM bank    ── 8 个存储体
```

## 日志级别

编译时定义 `TT_LOG_LEVEL` 控制输出：

| 级别 | 值 | 内容 |
|------|-----|------|
| ERROR | 0 | 仅错误 |
| INFO | 1 | 节点启停、复位事件（默认） |
| VERBOSE | 2 | NOC 操作、配置写入 |
| DEBUG | 3 | 每核详细信息 |

## 许可

本项目用于研究和教育目的。
