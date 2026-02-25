#ifndef TENSTORRENT_H
#define TENSTORRENT_H

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qom/object.h"
#include <pthread.h>

#define TYPE_TENSTORRENT "tenstorrent"

typedef struct TenstorrentState TenstorrentState;
DECLARE_INSTANCE_CHECKER(TenstorrentState, TENSTORRENT, TYPE_TENSTORRENT)

#define TENSTORRENT_VENDOR_ID  0x1E52
#define TENSTORRENT_DEVICE_ID  0xB140

// Multi-BAR configuration
#define NUM_BARS 5

// BAR0: Main MMIO space
#define BAR0_SIZE 0x20000000  // 512M, according to guest BAR0_SIZE

// BAR2: IATU configuration space
#define BAR2_SIZE 0x01000000  // 16MB

// BAR4: 4G TLB window space
#define BAR4_SIZE 0x800000000ULL // 32GB (8 * 4GB)

// PCIe capability structure offset
#define TENSTORRENT_PCIE_CAP_OFFSET 0x60

// TLB window definitions - per Blackhole device spec
#define TLB_2M_WINDOW_COUNT 202
#define TLB_2M_SHIFT 21
#define TLB_2M_WINDOW_SIZE (1 << TLB_2M_SHIFT)
#define TLB_2M_WINDOW_MASK (TLB_2M_WINDOW_SIZE - 1)
#define TLB_2M_WINDOWS_END      (TLB_2M_WINDOW_COUNT * TLB_2M_WINDOW_SIZE)

#define TLB_BASE_INDEX_2M         0
#define MEM_LARGE_WRITE_TLB       (TLB_BASE_INDEX_2M + 181)
#define MEM_LARGE_READ_TLB        (TLB_BASE_INDEX_2M + 182)
#define MEM_SMALL_READ_WRITE_TLB  (TLB_BASE_INDEX_2M + 183)
#define MEM_COUNT  3

#define TLB_4G_WINDOW_COUNT 8
#define TLB_4G_SHIFT 32
#define TLB_4G_WINDOW_SIZE (1UL << TLB_4G_SHIFT)
#define TLB_4G_WINDOW_MASK (TLB_4G_WINDOW_SIZE - 1)
#define TLB_BASE_INDEX_4G  202

#define TLB_REG_SIZE 12	// Same for 2M and 4G
#define TLB_TOTAL_WINDOW_COUNT (TLB_2M_WINDOW_COUNT + TLB_4G_WINDOW_COUNT)

#define TLB_REGS_START 0x1FC00000   // BAR0
#define TLB_REGS_LEN 0x00001000     // Covers all TLB registers

#define KERNEL_TLB_INDEX (TLB_2M_WINDOW_COUNT - 1)	// Last 2M window, for soft reset && others control
#define KERNEL_TLB_START (KERNEL_TLB_INDEX * TLB_2M_WINDOW_SIZE)
#define KERNEL_TLB_LEN TLB_2M_WINDOW_SIZE

#define REG_TLB_INDEX    191

#define NOC2AXI_CFG_START 0x1FD00000
#define NOC2AXI_CFG_LEN 0x00100000
#define NOC_ID_OFFSET 0x4044
#define NOC_STATUS_OFFSET 0x4200
#define NOC1_NOC2AXI_OFFSET 0x10000

#define PCIE_DBI_ADDR 0xF800000000000000ULL

// CSM space definitions
#define ARC_CSM_BASE           0x10000000  // CSM space base address
#define ARC_CSM_SIZE (1 << 19)

#define ARC_MSG_QCB_OFFSET     0x1000
#define ARC_MSG_QCB_SIZE       0x1000

#define TELEMETRY_PTR_OFFSET   0x2000
#define TELEMETRY_DATA_OFFSET  0x3000
#define TELEMETRY_PTR_SIZE     0x1000
#define TELEMETRY_DATA_SIZE    0x1000

// ARC register definitions (offsets within CSM space)
#define ARC_X 8
#define ARC_Y 0
#define RESET_SCRATCH_BASE      0x30400
#define ARC_BOOT_STATUS         (RESET_SCRATCH_BASE + 0x08)    // tt-metal SCRATCH_RAM_2
#define ARC_MSG_QCB_PTR         (RESET_SCRATCH_BASE + 0x2C)
#define ARC_TELEMETRY_DATA      (RESET_SCRATCH_BASE + 0x30)    // SCRATCH_RAM_12
#define ARC_TELEMETRY_PTR       (RESET_SCRATCH_BASE + 0x34)    // SCRATCH_RAM_13
#define ARC_MSI_FIFO            0xB0000

#define ARC_RESET_UNIT_OFFSET         0x30000
#define ARC_RESET_SCRATCH_OFFSET      (ARC_RESET_UNIT_OFFSET + 0x0060)
#define ARC_RESET_SCRATCH_2_OFFSET    (ARC_RESET_SCRATCH_OFFSET + 0x8)
#define ARC_RESET_REFCLK_LOW_OFFSET   (ARC_RESET_UNIT_OFFSET + 0xE0)
#define ARC_RESET_REFCLK_HIGH_OFFSET  (ARC_RESET_UNIT_OFFSET + 0xE4)

#define ARC_FW_INT_ADDR               (ARC_RESET_UNIT_OFFSET + 0x100)
#define ARC_FW_INT_VAL                65536

#define ARC_MSG_RESPONSE_OK_LIMIT     240

// Message queue registers
#define ARC_MSG_QUEUE_BASE         0x1100
#define ARC_MSG_QUEUE_REQ_WPTR(base) ((base) + 0x00)
#define ARC_MSG_QUEUE_RES_RPTR(base) ((base) + 0x04)
#define ARC_MSG_QUEUE_REQ_RPTR(base) ((base) + 0x10)
#define ARC_MSG_QUEUE_RES_WPTR(base) ((base) + 0x14)

#define ARC_QUEUE_ENTRY_SIZE        32
#define ARC_MSG_QUEUE_HEADER_SIZE   32
#define NUM_ENTRIES_PER_QUEUE       16

#define NODE_MEM_SIZE      0x800000
#define HIGH_MEM_SIZE     0x600000
#define HIGH_MEM_OFFSET   (TLB_2M_WINDOW_SIZE - HIGH_MEM_SIZE)

typedef enum {
    KMD = 0,
    MONITORING = 1,
    TOOLS = 2,
    APPLICATION = 3,
} BlackholeArcMessageQueueIndex;

// Internal register sizes
#define TLB_REGS_SIZE           0x1000
#define NOC2AXI_CFG_SIZE        0x100000
#define CSM_REGS_SIZE           0x200000  // Extended to 2MB

// Node ID definitions
#define NODE_ID_REGION_BASE     0x10120148  // Node ID region within CSM space
#define NODE_ID_REGION_SIZE     0x1000      // 4KB region

const uint64_t PCI_NOC_NODE_ID_LOGICAL = 0xFFFFFFFFFF000148ULL;
const uint64_t ARC_NOC_NODE_ID = 0x0000000080050044ULL;

#define BH_GRID_X 17
#define BH_GRID_Y 12

#define NIU_CFG_NOC0_BAR_ADDR  0x1FD04100;
#define NIU_CFG_NOC1_BAR_ADDR  0x1FD14100;

#define ATU_OFFSET_IN_BH_BAR2  0x1000
#define ATU_REGION_ITEM_SIZE   0x200
#define ATU_REGION_ADDR_OFF    0x14
#define ATU_REGION_SIZE        0x40000000

extern const uint32_t telemetry_ptr[];
extern const uint32_t telemetry_data[];

#define INTERFACE_TIMER_CONTROL_OFF  0x930  // Config space offset 0x930
#define INTERFACE_TIMER_TARGET_OFF   0x934  // Config space offset 0x934

#define MEM_MAILBOX_BASE  96

#define DRAM_BANK_SIZE 0x100000000UL
#define DRAM_BANK_NUM  8

typedef enum {
    B_CORE = 0,
    N_CORE,
    T0_CORE,
    T1_CORE,
    T2_CORE,
    NUM_CORES,
} CoreType;

// Coroutine scheduler settings
#define NUM_SCHEDULER_THREADS 10
#define INSTRUCTIONS_PER_STEP 100
#define STEPS_PER_YIELD       1

// Baby processor of each node
#define LDM_SIZE 0x4000
typedef struct Rv32Core {
    rv32_cpu_t rv32cpu;
    uint8_t * ram;
    uint8_t ldm[LDM_SIZE];
    int ldm_size;
    uint32_t start_addr;
    CoreType core_type;	//B_CORE, N_CORE..., for aeth, only B_CORE

    // Coroutine context
    coroutine_t coroutine_ctx;  // Coroutine handle
    bool should_stop;
    bool wait_init;
    uint64_t instructions_executed;
    uint32_t yield_threshold;
    int node_x, node_y;    // Node coordinates
} Rv32Core;

// Tensix node state (contains NUM_CORES cores)
typedef struct TensixNodeState {
    Rv32Core tensix_cores[NUM_CORES];  // RISCV cores
} TensixNodeState;

typedef union NodeProcessor {
    TensixNodeState tensix;  // Tensix node
    Rv32Core aeth_core;      // Active Ethernet node
    Rv32Core ieth_core;      // Idle Ethernet node
} NodeProcessor;

// TLB 2M register structure
typedef struct TLB_2M_REG {
    union {
        struct {
            uint32_t low32;
            uint32_t mid32;
            uint32_t high32;
        };
        struct {
            uint64_t address : 43;
            uint64_t x_end : 6;
            uint64_t y_end : 6;
            uint64_t x_start : 6;
            uint64_t y_start : 6;
            uint64_t noc : 2;
            uint64_t multicast : 1;
            uint64_t ordering : 2;
            uint64_t linked : 1;
            uint64_t use_static_vc : 1;
            uint64_t stream_header : 1;
            uint64_t static_vc : 3;
            uint64_t reserved : 18;
        };
    };
} TLB_2M_REG;

// TLB 4G register structure
typedef struct TLB_4G_REG {
    union {
        struct {
            uint32_t low32;
            uint32_t mid32;
            uint32_t high32;
        };
        struct {
            uint32_t address : 32;      // Address bits (32-bit)
            uint32_t x_end : 6;         // Destination X coordinate
            uint32_t y_end : 6;         // Destination Y coordinate
            uint32_t x_start : 6;       // Source X coordinate
            uint32_t y_start : 6;       // Source Y coordinate
            uint32_t noc : 2;           // NOC ID
            uint32_t multicast : 1;     // Multicast flag
            uint32_t ordering : 2;      // Ordering mode
            uint32_t linked : 1;        // Linked flag
            uint32_t use_static_vc : 1; // Use static VC
            uint32_t stream_header : 1; // Stream header flag
            uint32_t static_vc : 3;     // Static VC
            uint32_t reserved : 29;     // Reserved bits
        };
    };
} TLB_4G_REG;

struct TenstorrentState {
    PCIDevice parent_obj;

    // Multi-BAR support
    MemoryRegion bar0_mmio;
    MemoryRegion bar2_mmio;
    MemoryRegion bar4_mmio;

    MemoryRegion arc_mmio;

    uint32_t status;

    // Internal register storage
    //uint8_t tlb_regs[TLB_REGS_SIZE];
    uint8_t noc2axi_cfg[NOC2AXI_CFG_SIZE];
    uint8_t csm_regs[ARC_CSM_SIZE];
    uint8_t bar2_regs[BAR2_SIZE];  // BAR2 registers

    bool msi_enabled;
    bool msix_enabled;

    // TLB configuration storage
    TLB_2M_REG tlb_2m_configs[TLB_2M_WINDOW_COUNT];
    TLB_4G_REG tlb_4g_configs[TLB_4G_WINDOW_COUNT];
    bool tlb_configured[TLB_TOTAL_WINDOW_COUNT];

    // DRAM 4G * 8
    uint8_t * dram_bank[DRAM_BANK_NUM];

    // Per-node memory storage for TLB windows
    uint8_t *node_mem_8m[BH_GRID_Y][BH_GRID_X];  // Data for each 2M window
    MemoryRegion tlb_2m_region[TLB_2M_WINDOW_COUNT];
    MemoryRegion tlb_4g_region[TLB_4G_WINDOW_COUNT];

    NodeProcessor node_core[BH_GRID_Y][BH_GRID_X];

    // Coroutine scheduler
    scheduler_t scheduler;

    // Statistics
    uint64_t total_coroutines;
    uint64_t total_instructions;

};

#endif
