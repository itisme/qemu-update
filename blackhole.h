#ifndef __BLACKHOLE_H__
#define __BLACKHOLE_H__

////////////////////////////////////////////////////////////////////////
///arc telemetry part
#define TELEMETRY_NUM 21

typedef enum {
    BOARD_ID_HIGH = 1,
    BOARD_ID_LOW = 2,
    ASIC_ID = 3,
    HARVESTING_STATE = 4,
    UPDATE_TELEM_SPEED = 5,
    VCORE = 6,
    TDP = 7,
    TDC = 8,
    VDD_LIMITS = 9,
    THM_LIMITS = 10,
    ASIC_TEMPERATURE = 11,
    VREG_TEMPERATURE = 12,
    BOARD_TEMPERATURE = 13,
    AICLK = 14,
    AXICLK = 15,
    ARCCLK = 16,
    L2CPUCLK0 = 17,
    L2CPUCLK1 = 18,
    L2CPUCLK2 = 19,
    L2CPUCLK3 = 20,
    ETH_LIVE_STATUS = 21,
    DDR_STATUS = 22,
    DDR_SPEED = 23,
    ETH_FW_VERSION = 24,
    DDR_FW_VERSION = 25,
    BM_APP_FW_VERSION = 26,
    BM_BL_FW_VERSION = 27,
    FLASH_BUNDLE_VERSION = 28,
    CM_FW_VERSION = 29,
    L2CPU_FW_VERSION = 30,
    FAN_SPEED = 31,
    TIMER_HEARTBEAT = 32,
    TELEMETRY_ENUM_COUNT = 33,
    ENABLED_TENSIX_COL = 34,
    ENABLED_ETH = 35,
    ENABLED_GDDR = 36,
    ENABLED_L2CPU = 37,
    PCIE_USAGE = 38,
    NOC_TRANSLATION = 40,
    FAN_RPM = 41,
    ASIC_LOCATION = 52,
    TDC_LIMIT_MAX = 55,
    TT_FLASH_VERSION = 58,
    ASIC_ID_HIGH = 61,
    ASIC_ID_LOW = 62,
    AICLK_LIMIT_MAX = 63,
    TDP_LIMIT_MAX = 64,
    NUMBER_OF_TAGS = 65,
} TelemetryTag;

//////////////////////////////////////////////////////////////////////////////////////////////////
////eth node

#define POSTCODE_ETH_INIT_SKIP  0xC0DE0000
#define POSTCODE_ETH_INIT_SERDES 0xC0DE1000
#define POSTCODE_ETH_INIT_ETH_CTRL  0xC0DE2000
#define POSTCODE_ETH_INIT_MACPCS  0xC0DE3000
#define POSTCODE_ETH_INIT_PACKET  0xC0DE4000
#define POSTCODE_ETH_INIT_PASS  0xC0DEA000
#define POSTCODE_ETH_INIT_FAIL  0xC0DEB000
#define POSTCODE_ETH_INIT_CODE_NOT_FOUND  0xC0DEFFFF

#define NUM_SERDES_LANES  8

typedef enum {
    LINK_TRAIN_TRAINING,
    LINK_TRAIN_SKIP,
    LINK_TRAIN_PASS,
    LINK_TRAIN_INT_LB,
    LINK_TRAIN_EXT_LB,
    LINK_TRAIN_TIMEOUT_MANUAL_EQ,
    LINK_TRAIN_TIMEOUT_ANLT,
    LINK_TRAIN_TIMEOUT_CDR_LOCK,
    LINK_TRAIN_TIMEOUT_BIST_LOCK,
    LINK_TRAIN_TIMEOUT_LINK_UP,
    LINK_TRAIN_TIMEOUT_CHIP_INFO,
} link_train_status_e;

typedef enum {
    PORT_UNKNOWN,
    PORT_UP,
    PORT_DOWN,
    PORT_UNUSED,
} port_status_e;

typedef struct fw_version_t {
    uint32_t patch : 8;
    uint32_t minor : 8;
    uint32_t major : 8;
    uint32_t unused : 8;
} fw_version_t;

typedef struct chip_info_t {
    uint8_t pcb_type;  // 0
    uint8_t asic_location;
    uint8_t eth_id;
    uint8_t logical_eth_id;
    uint32_t board_id_hi;   // 1
    uint32_t board_id_lo;   // 2
    uint32_t mac_addr_org;  // 3
    uint32_t mac_addr_id;   // 4
    uint32_t spare[2];      // 5-6
    uint32_t ack;           // 7
} chip_info_t;

typedef struct serdes_rx_bist_results_t {
    uint32_t bist_mode;  // 0
    uint32_t test_time;  // 1
    // test_time in cycles for bist mode 0 and ms for bist mode 1
    uint32_t error_cnt_nt[NUM_SERDES_LANES];           // 2-9
    uint32_t error_cnt_55t32_nt[NUM_SERDES_LANES];     // 10-17
    uint32_t error_cnt_overflow_nt[NUM_SERDES_LANES];  // 18-25
} serdes_rx_bist_results_t;

typedef struct eth_status_t {
    // Basic status
    uint32_t postcode;                 // 0
    port_status_e port_status;         // 1
    link_train_status_e train_status;  // 2
    uint32_t train_speed;              // 3 - Actual resulting speed from training
    uint32_t spare[28 - 4];            // 4-27
    // Heartbeat
    uint32_t heartbeat[4];  // 28-31
} eth_status_t;

typedef struct serdes_results_t {
    uint32_t postcode;           // 0
    uint32_t serdes_inst;        // 1
    uint32_t serdes_lane_mask;   // 2
    uint32_t target_speed;       // 3 - Target speed from the boot params
    uint32_t data_rate;          // 4
    uint32_t data_width;         // 5
    uint32_t spare_main[8 - 6];  // 6-7
    // Training retries
    uint32_t anlt_retry_cnt;  // 8
    uint32_t spare[16 - 9];   // 9-15
    // BIST
    uint32_t bist_mode;       // 16
    uint32_t bist_test_time;  // 17
    // test_time in cycles for bist mode 0 and ms for bist mode 1
    uint32_t bist_err_cnt_nt[NUM_SERDES_LANES];           // 18-25
    uint32_t bist_err_cnt_55t32_nt[NUM_SERDES_LANES];     // 26-33
    uint32_t bist_err_cnt_overflow_nt[NUM_SERDES_LANES];  // 34-41
    uint32_t cdr_unlocked_cnt;                            // 42
    uint32_t cdr_unlock_transitions;                      // 43
    uint32_t spare2[48 - 44];                             // 44-47
    // Training times
    uint32_t man_eq_cmn_pstate_time;      // 48
    uint32_t man_eq_tx_ack_time;          // 49
    uint32_t man_eq_rx_ack_time;          // 50
    uint32_t man_eq_rx_eq_assert_time;    // 51
    uint32_t man_eq_rx_eq_deassert_time;  // 52
    uint32_t anlt_auto_neg_time;          // 53
    uint32_t anlt_link_train_time;        // 54
    uint32_t anlt_retrain_time;           // 55
    uint32_t cdr_lock_time;               // 56
    uint32_t bist_lock_time;              // 57
    uint32_t spare_time[64 - 58];         // 58-63
} serdes_results_t;

typedef struct macpcs_results_t {
    uint32_t postcode;          // 0
    uint32_t macpcs_retry_cnt;  // 1
    uint32_t spare[24 - 2];     // 2-23
    // Training times
    uint32_t link_up_time;         // 24
    uint32_t chip_info_time;       // 25
    uint32_t spare_time[32 - 26];  // 26-31
} macpcs_results_t;

typedef struct eth_live_status_t {
    uint32_t retrain_count;  // 0
    uint32_t rx_link_up;     // 1 - MAC/PCS RX Link Up
    uint32_t spare[8 - 2];   // 2-7
    // Snapshot registers
    uint64_t frames_txd;          // 8,9 - Cumulative TX Packets Transmitted count
    uint64_t frames_txd_ok;       // 10,11 - Cumulative TX Packets Transmitted OK count
    uint64_t frames_txd_badfcs;   // 12,13 - Cumulative TX Packets Transmitted with BAD FCS count
    uint64_t bytes_txd;           // 14,15 - Cumulative TX Bytes Transmitted count
    uint64_t bytes_txd_ok;        // 16,17 - Cumulative TX Bytes Transmitted OK count
    uint64_t bytes_txd_badfcs;    // 18,19 - Cumulative TX Bytes Transmitted with BAD FCS count
    uint64_t frames_rxd;          // 20,21 - Cumulative Packets Received count
    uint64_t frames_rxd_ok;       // 22,23 - Cumulative Packets Received OK count
    uint64_t frames_rxd_badfcs;   // 24,25 - Cumulative Packets Received with BAD FCS count
    uint64_t frames_rxd_dropped;  // 26,27 - Cumulative Dropped Packets Received count
    uint64_t bytes_rxd;           // 28,29 - Cumulative Bytes received count
    uint64_t bytes_rxd_ok;        // 30,31 - Cumulative Bytes received OK count
    uint64_t bytes_rxd_badfcs;    // 32,33 - Cumulative Bytes received with BAD FCS count
    uint64_t bytes_rxd_dropped;   // 34,35 - Cumulative Bytes received and dropped count
    uint64_t corr_cw;             // 36,37 - Cumulative Corrected Codeword count
    uint64_t uncorr_cw;           // 38,39 - Cumulative Uncorrected Codeword count
    uint32_t spare2[64 - 40];     // 40-63
} eth_live_status_t;

typedef struct eth_api_table_t {
    uint32_t* eth_link_status_check_ptr;  // 0 - Pointer to the link status check function
    uint32_t spare[16 - 1];               // 1-15
} eth_api_table_t;



typedef struct boot_results_t {
    eth_status_t eth_status;            // 0-31
    serdes_results_t serdes_results;    // 32 - 95
    macpcs_results_t macpcs_results;    // 96 - 127
    eth_live_status_t eth_live_status;  // 128 - 191
    eth_api_table_t eth_api_table;      // 192 - 207
    uint32_t spare[238 - 208];          // 208 - 237
    fw_version_t serdes_fw_ver;         // 238
    fw_version_t eth_fw_ver;            // 239
    chip_info_t local_info;             // 240 - 247
    chip_info_t remote_info;            // 248 - 255
} boot_results_t;

#define BOOT_RESULTS_ADDR  0x7CC00

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// dev_mem_map
#define MEM_ERISC_RESERVED1 0
#define MEM_ERISC_RESERVED1_SIZE 256

#define MAX_NUM_NOCS_PER_CORE 2
#define MAX_RISCV_PER_CORE    5

#define NUM_PROGRAMMABLE_CORE_TYPES  3
#define NUM_PROCESSORS_PER_CORE_TYPE 5

struct ncrisc_halt_msg_t {
    volatile uint32_t resume_addr;
    volatile uint32_t stack_save;
};

struct subordinate_sync_msg_t {
    union {
        volatile uint32_t all;
        struct {
            volatile uint8_t dm1;  // ncrisc must come first, see ncrisc-halt.S
            volatile uint8_t trisc0;
            volatile uint8_t trisc1;
            volatile uint8_t trisc2;
        };
    };
};

typedef struct rta_offset_t {
    volatile uint16_t rta_offset;
    volatile uint16_t crta_offset;
} rta_offset_t;

struct kernel_config_msg_t {
    // Ring buffer of kernel configuration data
    volatile uint32_t kernel_config_base[NUM_PROGRAMMABLE_CORE_TYPES];
    volatile uint16_t sem_offset[NUM_PROGRAMMABLE_CORE_TYPES];
    volatile uint16_t local_cb_offset;
    volatile uint16_t remote_cb_offset;
    rta_offset_t rta_offset[NUM_PROCESSORS_PER_CORE_TYPE];
    volatile uint8_t mode;  // dispatch mode host/dev
    volatile uint8_t pad2[1];  // CODEGEN:skip
    volatile uint32_t kernel_text_offset[NUM_PROCESSORS_PER_CORE_TYPE];
    volatile uint64_t local_cb_mask;

    volatile uint8_t brisc_noc_id;
    volatile uint8_t brisc_noc_mode;
    volatile uint8_t min_remote_cb_start_index;
    volatile uint8_t exit_erisc_kernel;
    // 32 bit program/launch_msg_id used by the performance profiler
    // [9:0]: physical device id
    // [30:10]: program id
    // [31:31]: 0 (specifies that this id corresponds to a program running on device)
    volatile uint32_t host_assigned_id;
    // bit i set => processor i enabled
    volatile uint32_t enables;
    volatile uint16_t watcher_kernel_ids[NUM_PROCESSORS_PER_CORE_TYPE];
    volatile uint16_t ncrisc_kernel_size16;  // size in 16 byte units

    volatile uint8_t sub_device_origin_x;  // Logical X coordinate of the sub device origin
    volatile uint8_t sub_device_origin_y;  // Logical Y coordinate of the sub device origin
    volatile uint8_t pad3[13];             // CODEGEN:skip

    volatile uint8_t preload;  // Must be at end, so it's only written when all other data is written.
} __attribute__((packed));

struct launch_msg_t {  // must be cacheline aligned
    struct kernel_config_msg_t kernel_config;
} __attribute__((packed));

struct go_msg_t {
    union {
        uint32_t all;
        struct {
            uint8_t dispatch_message_offset;
            uint8_t master_x;
            uint8_t master_y;
            uint8_t signal;  // INIT, GO, DONE, RESET_RD_PTR
        };
    };
} __attribute__((packed));

#define num_waypoint_bytes_per_riscv 4
struct debug_waypoint_msg_t {
    volatile uint8_t waypoint[num_waypoint_bytes_per_riscv];
};

struct debug_sanitize_noc_addr_msg_t {
    volatile uint64_t noc_addr;
    volatile uint32_t l1_addr;
    volatile uint32_t len;
    volatile uint16_t which_risc;
    volatile uint16_t return_code;
    volatile uint8_t is_multicast;
    volatile uint8_t is_write;
    volatile uint8_t is_target;
    volatile uint8_t pad;  // CODEGEN:skip
};

struct debug_eth_link_t {
    volatile uint8_t link_down;
};

struct debug_assert_msg_t {
    volatile uint16_t line_num;
    volatile uint8_t tripped;
    volatile uint8_t which;
};

struct debug_pause_msg_t {
    volatile uint8_t flags[NUM_PROCESSORS_PER_CORE_TYPE];
    uint8_t pad[3];  // CODEGEN:skip
};

struct debug_stack_usage_per_cpu_t {
    // min free stack, offset by +1 (0 == unset)
    volatile uint16_t min_free;
    volatile uint16_t watcher_kernel_id;
};

struct debug_stack_usage_t {
    struct debug_stack_usage_per_cpu_t cpu[NUM_PROCESSORS_PER_CORE_TYPE];
    uint8_t pad[12];  // CODEGEN:skip
};

struct debug_insert_delays_msg_t {
    volatile uint32_t read_delay_processor_mask;    // Which processors will delay their reads
    volatile uint32_t write_delay_processor_mask;   // Which processors will delay their writes
    volatile uint32_t atomic_delay_processor_mask;  // Which processors will delay their atomics
    volatile uint32_t feedback;                     // Stores the feedback about delays (used for testing)
};

#define DEBUG_RING_BUFFER_ELEMENTS 32
#define DEBUG_RING_BUFFER_SIZE     (DEBUG_RING_BUFFER_ELEMENTS * sizeof(uint32_t))
struct debug_ring_buf_msg_t {
    int16_t current_ptr;
    uint16_t wrapped;
    uint32_t data[DEBUG_RING_BUFFER_ELEMENTS];
};

struct watcher_msg_t {
    volatile uint32_t enable;
    struct debug_waypoint_msg_t debug_waypoint[MAX_RISCV_PER_CORE];
    struct debug_sanitize_noc_addr_msg_t sanitize_noc[MAX_NUM_NOCS_PER_CORE];
    _Atomic(_Bool) noc_linked_status[MAX_NUM_NOCS_PER_CORE];;
    struct debug_eth_link_t eth_status;
    uint8_t pad0;  // CODEGEN:skip
    struct debug_assert_msg_t assert_status;
    struct debug_pause_msg_t pause_status;
    struct debug_stack_usage_t stack_usage;
    struct debug_insert_delays_msg_t debug_insert_delays;
    struct debug_ring_buf_msg_t debug_ring_buf;
};

#define ATTR_PACK __attribute__((packed))
#define DPRINT_BUFFER_SIZE  204

struct DebugPrintMemLayout {
    struct Aux {
        // current writer offset in buffer
        uint32_t wpos;
        uint32_t rpos;
        uint16_t core_x;
        uint16_t core_y;
    } aux ATTR_PACK;
    uint8_t data[DPRINT_BUFFER_SIZE - sizeof(struct Aux)];

} ATTR_PACK;

struct dprint_buf_msg_t {
    struct DebugPrintMemLayout data[NUM_PROCESSORS_PER_CORE_TYPE];
    uint32_t pad;  // to 1024 bytes
};

struct addressable_core_t {
    volatile uint8_t x;
    volatile uint8_t y;
    volatile uint8_t type;
};

#define MAX_VIRTUAL_NON_WORKER_CORES   29
#define MAX_PHYSICAL_NON_WORKER_CORES  35
#define MAX_HARVESTED_ON_AXIS          2

struct core_info_msg_t {
    volatile uint64_t noc_pcie_addr_base;
    volatile uint64_t noc_pcie_addr_end;
    volatile uint64_t noc_dram_addr_base;
    volatile uint64_t noc_dram_addr_end;
    struct addressable_core_t non_worker_cores[MAX_PHYSICAL_NON_WORKER_CORES];
    struct addressable_core_t virtual_non_worker_cores[MAX_VIRTUAL_NON_WORKER_CORES];
    volatile uint8_t harvested_coords[MAX_HARVESTED_ON_AXIS];
    volatile uint8_t virtual_harvested_coords[MAX_HARVESTED_ON_AXIS];
    volatile uint8_t noc_size_x;
    volatile uint8_t noc_size_y;
    volatile uint8_t worker_grid_size_x;
    volatile uint8_t worker_grid_size_y;
    volatile uint8_t absolute_logical_x;  // Logical X coordinate of this core
    volatile uint8_t absolute_logical_y;  // Logical Y coordinate of this core
    volatile uint32_t l1_unreserved_start;
    uint8_t pad;  // CODEGEN:skip
};

#define PROFILER_L1_CONTROL_VECTOR_SIZE   32
#define PROFILER_L1_VECTOR_SIZE       ((250 + 4 + 2)*2)

#define launch_msg_buffer_num_entries  8
#define go_message_num_entries         9

#define PROFILER_NOC_ALIGNMENT_PAD_COUNT   4

struct profiler_msg_t {
    uint32_t control_vector[PROFILER_L1_CONTROL_VECTOR_SIZE];
    uint32_t buffer[5][PROFILER_L1_VECTOR_SIZE];
};  // struct profiler_msg_template_t

typedef struct mailboxes_t {
    struct ncrisc_halt_msg_t ncrisc_halt;
    struct subordinate_sync_msg_t subordinate_sync;
    volatile uint32_t launch_msg_rd_ptr;  // Volatile so this can be manually reset by host. TODO: remove volatile when
                                          // dispatch init moves to one-shot.
    struct launch_msg_t launch[launch_msg_buffer_num_entries];
    volatile struct go_msg_t go_messages[go_message_num_entries];
    uint32_t pads_1[3];                  // CODEGEN:skip
    volatile uint32_t go_message_index;  // Index into go_messages to use. Always 0 on unicast cores.
    struct watcher_msg_t watcher;
    struct dprint_buf_msg_t dprint_buf;  // CODEGEN:skip
    struct core_info_msg_t core_info;
    // Keep profiler last since it's size is dynamic per core type
    uint32_t pads_2[PROFILER_NOC_ALIGNMENT_PAD_COUNT];  // CODEGEN:skip
    struct profiler_msg_t profiler;                            // CODEGEN:skip
} mailboxes_t;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// dev mem

#define MEM_L1_BASE 0x0
#define MEM_L1_SIZE (1536 * 1024)

#define MEM_ETH_BASE 0x0
// Top 64K is reserved for syseng but host reads/writes from that region
#define MEM_ETH_SIZE (512 * 1024)

#define MEM_DRAM_SIZE (4177920 * 1024U)

#define MEM_LOCAL_BASE 0xFFB00000
#define MEM_BRISC_LOCAL_SIZE (8 * 1024)
#define MEM_NCRISC_LOCAL_SIZE (8 * 1024)
#define MEM_TRISC_LOCAL_SIZE (4 * 1024)

// reset
#define TENSIX_SOFT_RESET_ADDR 0xFFB121B0
#define BRISC  ((uint32_t)1 << 11)
#define TRISC0 ((uint32_t)1 << 12)
#define TRISC1 ((uint32_t)1 << 13)
#define TRISC2 ((uint32_t)1 << 14)
#define NCRISC ((uint32_t)1 << 18)
#define STAGGERED_START ((uint32_t)1 << 31)
#define ALL_TRISC_SOFT_RESET  (TensixSoftResetOptions::TRISC0 | TensixSoftResetOptions::TRISC1 | TensixSoftResetOptions::TRISC2)
#define ALL_TENSIX_SOFT_RESET \
    (TensixSoftResetOptions::BRISC | TensixSoftResetOptions::NCRISC | TensixSoftResetOptions::STAGGERED_START | ALL_TRISC_SOFT_RESET)

// Messages for host to tell brisc to go
#define RUN_MSG_INIT  0x40
#define RUN_MSG_GO  0x80
#define RUN_MSG_RESET_READ_PTR  0xc0
#define RUN_MSG_RESET_READ_PTR_FROM_HOST  0xe0
#define RUN_MSG_DONE  0

// 0x80808000 is a micro-optimization, calculated with 1 riscv insn
#define RUN_SYNC_MSG_INIT  0x40
#define RUN_SYNC_MSG_GO  0x80
// Trigger loading CBs (and IRAM) before actually running the kernel.
#define RUN_SYNC_MSG_LOAD  0x1
#define RUN_SYNC_MSG_WAITING_FOR_RESET  0x2
#define RUN_SYNC_MSG_INIT_SYNC_REGISTERS  0x3
#define RUN_SYNC_MSG_DONE  0
#define RUN_SYNC_MSG_ALL_GO  0x80808080
#define RUN_SYNC_MSG_ALL_INIT  0x40404040
#define RUN_SYNC_MSG_ALL_SUBORDINATES_DONE  0

// formware base address
#define MEM_NCRISC_FIRMWARE_SIZE 2560
#define MEM_TRISC0_FIRMWARE_SIZE 2560
#define MEM_TRISC1_FIRMWARE_SIZE 2560
#define MEM_TRISC2_FIRMWARE_SIZE 2560

#define MEM_MAILBOX_BASE 96
#define MEM_MAILBOX_SIZE 12896
#define MEM_MAILBOX_END (MEM_MAILBOX_BASE + MEM_MAILBOX_SIZE)

#define MEM_ZEROS_BASE ((MEM_MAILBOX_END + 31) & ~31)
#define MEM_ZEROS_SIZE 512
#define MEM_LLK_DEBUG_SIZE 1024
#define MEM_LLK_DEBUG_BASE (MEM_ZEROS_BASE + MEM_ZEROS_SIZE)
#define MEM_BRISC_FIRMWARE_SIZE (6 * 1024 + 2560)

#define MEM_BRISC_FIRMWARE_BASE (MEM_LLK_DEBUG_BASE + MEM_LLK_DEBUG_SIZE)
#define MEM_NCRISC_FIRMWARE_BASE (MEM_BRISC_FIRMWARE_BASE + MEM_BRISC_FIRMWARE_SIZE)
#define MEM_TRISC0_FIRMWARE_BASE (MEM_NCRISC_FIRMWARE_BASE + MEM_NCRISC_FIRMWARE_SIZE)
#define MEM_TRISC1_FIRMWARE_BASE (MEM_TRISC0_FIRMWARE_BASE + MEM_TRISC0_FIRMWARE_SIZE)
#define MEM_TRISC2_FIRMWARE_BASE (MEM_TRISC1_FIRMWARE_BASE + MEM_TRISC1_FIRMWARE_SIZE)

#define MEM_ERISC_RESERVED1 0
#define MEM_ERISC_RESERVED1_SIZE 256
#define MEM_ERISC_MAILBOX_SIZE 12768
#define MEM_AERISC_MAILBOX_BASE (MEM_ERISC_RESERVED1 + MEM_ERISC_RESERVED1_SIZE)
#define MEM_AERISC_MAILBOX_SIZE MEM_ERISC_MAILBOX_SIZE
#define MEM_AERISC_MAILBOX_END (MEM_AERISC_MAILBOX_BASE + MEM_AERISC_MAILBOX_SIZE)
#define MEM_L1_INLINE_SIZE_PER_NOC 16
#define MEM_AERISC_L1_INLINE_BASE MEM_AERISC_MAILBOX_END
#define MEM_AERISC_L1_INLINE_END (MEM_AERISC_L1_INLINE_BASE + (MEM_L1_INLINE_SIZE_PER_NOC * 2) * 2)
#define MEM_AERISC_VOID_LAUNCH_FLAG MEM_AERISC_L1_INLINE_END
#define MEM_AERISC_VOID_LAUNCH_FLAG_SIZE 16
#define MEM_AERISC_FIRMWARE_BASE (MEM_AERISC_VOID_LAUNCH_FLAG + MEM_AERISC_VOID_LAUNCH_FLAG_SIZE)

// noc parameter ---------------------------------------------
#define NOC_REG_SPACE_START_ADDR 0xFF000000
#define NOC_REGS_START_ADDR 0xFFB20000
#define NOC_CMD_BUF_OFFSET 0x00000800
#define NOC_CMD_BUF_OFFSET_BIT 11
#define NOC_INSTANCE_OFFSET 0x00010000
#define NOC_INSTANCE_OFFSET_BIT 16
#define NOC_CMD_BUF_INSTANCE_OFFSET(noc, buf) (((buf) << NOC_CMD_BUF_OFFSET_BIT) + ((noc) << NOC_INSTANCE_OFFSET_BIT))

// tt_metal/hw/inc/blackhole/noc_nonblocking_api.h
#define WR_CMD_BUF      0
#define RD_CMD_BUF      1
#define WR_REG_CMD_BUF  2
#define AT_CMD_BUF      3

#define DISPATCH_S_WR_REG_CMD_BUF 1
#define DISPATCH_S_ATOMIC_CMD_BUF 2

////
// NIU master IF control registers:

#define NOC_TARG_ADDR_LO (NOC_REGS_START_ADDR + 0x0)
#define NOC_TARG_ADDR_MID (NOC_REGS_START_ADDR + 0x4)
#define NOC_TARG_ADDR_HI (NOC_REGS_START_ADDR + 0x8)

#define NOC_RET_ADDR_LO (NOC_REGS_START_ADDR + 0xC)
#define NOC_RET_ADDR_MID (NOC_REGS_START_ADDR + 0x10)
#define NOC_RET_ADDR_HI (NOC_REGS_START_ADDR + 0x14)

#define NOC_PACKET_TAG (NOC_REGS_START_ADDR + 0x18)
#define NOC_CTRL (NOC_REGS_START_ADDR + 0x1C)
#define NOC_AT_LEN_BE (NOC_REGS_START_ADDR + 0x20)
#define NOC_AT_LEN_BE_1 (NOC_REGS_START_ADDR + 0x24)
#define NOC_AT_DATA (NOC_REGS_START_ADDR + 0x28)
#define NOC_BRCST_EXCLUDE (NOC_REGS_START_ADDR + 0x2C)
#define NOC_L1_ACC_AT_INSTRN (NOC_REGS_START_ADDR + 0x30)
#define NOC_SEC_CTRL (NOC_REGS_START_ADDR + 0x34)

#define NOC_CMD_CTRL (NOC_REGS_START_ADDR + 0x40)
#define NOC_NODE_ID (NOC_REGS_START_ADDR + 0x44)
#define NOC_ENDPOINT_ID (NOC_REGS_START_ADDR + 0x48)

//
// NOC CTRL fields
#define NOC_CTRL_SEND_REQ (0x1 << 0)
//
#define NOC_CTRL_STATUS_READY 0x0
#define NOC_CMD_RESP_MARKED (0x1 << 4)
// Atomic command codes
#define NOC_AT_INS_NOP 0x0
#define NOC_AT_INS_INCR_GET 0x1
#define NOC_AT_INS_INCR_GET_PTR 0x2
#define NOC_AT_INS_SWAP 0x3
#define NOC_AT_INS_CAS 0x4
#define NOC_AT_INS_GET_TILE_MAP 0x5
#define NOC_AT_INS_STORE_IND 0x6
#define NOC_AT_INS_SWAP_4B 0x7
#define NOC_AT_INS_ACC 0x9

#define NOC_AT_IND_32(index) ((index) << 0)
#define NOC_AT_IND_32_SRC(index) ((index) << 10)
#define NOC_AT_WRAP(wrap) ((wrap) << 2)
// #define NOC_AT_INCR(incr)         ((incr) << 6)
#define NOC_AT_INS(ins) ((ins) << 12)
#define NOC_AT_TILE_MAP_IND(ind) ((ind) << 2)
#define NOC_AT_ACC_FORMAT(format) (((format) << 0) & 0x7)
#define NOC_AT_ACC_SAT_DIS(dis) ((dis) << 3)

///


#define NOC_COORD_REG_OFFSET 0

#define NOC_ADDR_LOCAL_BITS /*64*/ 36
#define NOC_ADDR_NODE_ID_BITS 6


// noc status
#define NOC_STATUS(cnt) (NOC_REGS_START_ADDR + 0x200 + ((cnt) * 4))
#define NOC_STATUS_READ_REG_OFF(noc, cnt) (((noc) << NOC_INSTANCE_OFFSET_BIT) + NOC_REGS_START_ADDR + 0x200 + ((cnt) * 4))

#define NIU_MST_NONPOSTED_ATOMIC_STARTED 0xF
#define NIU_MST_RD_REQ_STARTED 0xE
#define NIU_MST_POSTED_WR_REQ_STARTED 0xD
#define NIU_MST_NONPOSTED_WR_REQ_STARTED 0xC
#define NIU_MST_POSTED_WR_REQ_SENT 0xB
#define NIU_MST_NONPOSTED_WR_REQ_SENT 0xA
#define NIU_MST_POSTED_WR_DATA_WORD_SENT 0x9
#define NIU_MST_NONPOSTED_WR_DATA_WORD_SENT 0x8
#define NIU_MST_POSTED_ATOMIC_SENT 0x7
#define NIU_MST_NONPOSTED_ATOMIC_SENT 0x6
#define NIU_MST_RD_REQ_SENT 0x5

#define NIU_MST_CMD_ACCEPTED 0x4
#define NIU_MST_RD_DATA_WORD_RECEIVED 0x3
#define NIU_MST_RD_RESP_RECEIVED 0x2
#define NIU_MST_WR_ACK_RECEIVED 0x1
#define NIU_MST_ATOMIC_RESP_RECEIVED 0x0

//get from compile definition, noc_reads_num_issued
#define NOC_READS_NUM_ISSUED     (MEM_LOCAL_BASE + 0x38)
#define NPST_WRITES_NUM_ISSUED  (MEM_LOCAL_BASE + 0x30)
#define NPST_WRITES_NUM_ACKED   (MEM_LOCAL_BASE + 0x28)
#define PST_WRITES_NUM_ISSUED   (MEM_LOCAL_BASE + 0x18)
#define NPST_ATOMICS_ACKED      (MEM_LOCAL_BASE + 0x20)

// Idle ERISC reset PC registers (ram offsets, written by host and firmware)
#define IERISC_RESET_PC             0x14000
#define SUBORDINATE_IERISC_RESET_PC 0x14008

// Wall clock timer registers (offset from RISCV_DEBUG_REGS_START_ADDR 0xFFB12000)
// Firmware reads these via riscv_wait() for timing delays
#define WALL_CLOCK_L    0x121F0   // low 32-bit, relative to 0xFFB00000
#define WALL_CLOCK_H    0x121F8   // high 32-bit, relative to 0xFFB00000

#define PREFETCH_Q_BASE  0x18fc0

// Addres formats

#define NOC_XY_ENCODING(x, y) ((((uint32_t)(y)) << (NOC_ADDR_NODE_ID_BITS)) | (((uint32_t)(x))))

// Base address pulled from tt::umd::Cluster::get_pcie_base_addr_from_device
#define NOC_XY_PCIE_ENCODING(x, y) \
    ((uint64_t(NOC_XY_ENCODING(x, y)) << (NOC_ADDR_LOCAL_BITS - NOC_COORD_REG_OFFSET)) | 0x1000000000000000)

#define NOC_MULTICAST_ENCODING(x_start, y_start, x_end, y_end)                                                         \
    ((((uint32_t)(x_start)) << (2 * NOC_ADDR_NODE_ID_BITS)) | (((uint32_t)(y_start)) << (3 * NOC_ADDR_NODE_ID_BITS)) | \
     (((uint32_t)(x_end))) | (((uint32_t)(y_end)) << (NOC_ADDR_NODE_ID_BITS)))

// Because BH uses WH style address encoding (36 bits followed by coordinates) but PCIe transactions require bit 60 to
// be set, we need to mask out the xy-coordinate When NOC_ADDR_LOCAL_BITS is 64 then NOC_LOCAL_ADDR_OFFSET can be used
// and the below define can be deprecated
#define NOC_LOCAL_ADDR(addr) ((addr) & 0x1000000FFFFFFFFF)

// sem_l1_base
#define SEM_L1_BASE   (MEM_LOCAL_BASE + 0x8a0)


// according to tt_metal/hw/toolchain/main.ld
#define GLOBAL_POINTER   (MEM_LOCAL_BASE + 0x3f0)
	
#define NUM_NOCS 2
#define NOC0_REGS_START_ADDR 0xFFB20000
#define NOC1_REGS_START_ADDR 0xFFB30000

// noc overlay stream reg
#define NOC_OVERLAY_START_ADDR 0xFFB40000
#define NOC_STREAM_REG_SPACE_SIZE 0x1000
#define NOC_NUM_STREAMS 64

#define STREAM_REG_ADDR(stream_id, reg_id) \
    ((NOC_OVERLAY_START_ADDR) + (((uint32_t)(stream_id)) * (NOC_STREAM_REG_SPACE_SIZE)) + (((uint32_t)(reg_id)) << 2))

#define STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_REG_INDEX 270
#define STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_REG_INDEX 297

/////////////////////////////////////////////////////////////////////////////////////////////////////////
///functions
typedef enum {
    ARC,
    TENSIX,
    ETH,
    DRAM,
    PCI
} NodeType;

bool is_noc_translation_enabled(void);
void virt2log(int *x, int *y);
NodeType get_node_type(int x, int y);
int dram_node2bank(int x, int y);

#endif
