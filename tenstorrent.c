#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "system/reset.h"
#include "system/address-spaces.h"
#include "system/ram_addr.h"
#include "qemu/memalign.h"
#include "blackhole.h"
#include "rv32sim.h"
#include <pthread.h>
#include <sched.h>
#include "system/address-spaces.h"
#include "system/memory.h"
#include "coroutine_lib.h"
#include "tenstorrent.h"

/* Log levels: 0=ERROR, 1=INFO, 2=VERBOSE, 3=DEBUG */
#define LOG_LEVEL_ERROR   0
#define LOG_LEVEL_INFO    1
#define LOG_LEVEL_VERBOSE 2
#define LOG_LEVEL_DEBUG   3

#ifndef TT_LOG_LEVEL
#define TT_LOG_LEVEL LOG_LEVEL_VERBOSE
#endif

#define TT_LOG(level, fmt, ...) do { \
    if ((level) <= TT_LOG_LEVEL) printf(fmt, ##__VA_ARGS__); \
} while (0)

static void proc_arc_msg(TenstorrentState *tt);

static void init_arc_node(TenstorrentState *tt, uint8_t *buf) {
    *(uint32_t *)&buf[ARC_BOOT_STATUS] = 0x5;
    *(uint32_t *)&buf[ARC_MSG_QCB_PTR] = ARC_CSM_BASE + ARC_MSG_QCB_OFFSET;
    *(uint32_t *)&buf[ARC_TELEMETRY_PTR] = ARC_CSM_BASE + TELEMETRY_PTR_OFFSET;
    *(uint32_t *)&buf[ARC_TELEMETRY_DATA] = ARC_CSM_BASE + TELEMETRY_DATA_OFFSET;

    memcpy(&buf[TELEMETRY_PTR_OFFSET], telemetry_ptr, (TELEMETRY_NUM + 2) * sizeof(uint32_t));
    memcpy(&buf[TELEMETRY_DATA_OFFSET], telemetry_data, TELEMETRY_NUM * sizeof(uint32_t));
    
    memcpy(&buf[ARC_MSG_QCB_OFFSET], &tt->csm_regs[ARC_MSG_QCB_OFFSET], 0x200);
}

extern boot_results_t eth_boot_results;
static void init_eth_node(TenstorrentState *tt, uint8_t *buf) {
    memcpy(&buf[BOOT_RESULTS_ADDR], &eth_boot_results, sizeof(eth_boot_results));
}

static uint8_t * obtain_node_mem(TenstorrentState *tt, int x, int y) {
    if(!tt->node_mem_8m[y][x]) {
    	if (get_node_type(x, y) != DRAM) {
    	    uint8_t *ram = qemu_memalign(qemu_real_host_page_size(), NODE_MEM_SIZE);
    	    tt->node_mem_8m[y][x] = ram;
    	    memset(ram, 0, NODE_MEM_SIZE);
    	    if(x == ARC_X && y == ARC_Y)
    	        init_arc_node(tt, ram);
    	    if(y == 1 && x != 0 && x != 8 && x !=9) {
    	        init_eth_node(tt, ram);
    	    }
    	} else {
    	    int b_idx = dram_node2bank(x, y);
    	    if (!tt->dram_bank[b_idx]) {
    	        tt->dram_bank[b_idx] = qemu_memalign(qemu_real_host_page_size(), DRAM_BANK_SIZE);
    	    }
    	    tt->node_mem_8m[y][x] = tt->dram_bank[b_idx];
    	}
    }
    return tt->node_mem_8m[y][x];
}

static bool is_arcram(TenstorrentState *tt, uint8_t *ram){
    return ram == tt->node_mem_8m[ARC_Y][ARC_X];
}

static bool is_arcnode(int x, int y) {
    return x == ARC_X && y == ARC_Y;
}

// RAM mapping size: 1MB for L1 storage, upper 1MB for MMIO
#define TLB_RAM_SIZE (1024 * 1024)

static void remap_ram_region(TenstorrentState *tt, int tlb_index, uint8_t *ram, bool isarc){
    MemoryRegion *mr = &tt->tlb_2m_region[tlb_index];

    if(memory_region_is_mapped(mr)){
        RAMBlock *rb = mr->ram_block;
        if(rb->host == ram)
            return;
        // Remove arc_mmio if it was attached to any TLB region
        if(tt->arc_mmio.container)
            memory_region_del_subregion(tt->arc_mmio.container, &tt->arc_mmio);
        memory_region_del_subregion(&tt->bar0_mmio, mr);
    }

    char name[20];
    snprintf(name, sizeof(name), "tlb_mr_%d", tlb_index);
    // Only map first 1MB as RAM, upper 1MB is MMIO (handled by tenstorrent_tlb_2m_write)
    memory_region_init_ram_ptr(mr, OBJECT(tt), name, TLB_RAM_SIZE, ram);
    if(isarc)
        memory_region_add_subregion(mr, ARC_FW_INT_ADDR, &tt->arc_mmio);
    memory_region_add_subregion(&tt->bar0_mmio, tlb_index * TLB_2M_WINDOW_SIZE, mr);
}

// Flush multicast: copy source node RAM to all other nodes in the rectangle
static void flush_mcast(TenstorrentState *tt, int tlb_index)
{
    int xs = tt->mcast_state[tlb_index].xs;
    int ys = tt->mcast_state[tlb_index].ys;
    int xe = tt->mcast_state[tlb_index].xe;
    int ye = tt->mcast_state[tlb_index].ye;
    uint8_t *src = obtain_node_mem(tt, xe, ye);

    TT_LOG(LOG_LEVEL_INFO, "flush_mcast tlb=%d (%d,%d)-(%d,%d)\n", tlb_index, xs, ys, xe, ye);

    for (int y = ys; y <= ye; y++) {
        for (int x = xs; x <= xe; x++) {
            if (x == xe && y == ye)
                continue;  // skip source node
            uint8_t *dst = obtain_node_mem(tt, x, y);
            if (dst && dst != src)
                memcpy(dst, src, TLB_2M_WINDOW_SIZE);
        }
    }
    tt->mcast_state[tlb_index].active = false;
}

// Node ID update function
static void tenstorrent_update_node_id(TenstorrentState *tt, int tlb_index)
{
    if (tlb_index < 0 || tlb_index >= TLB_TOTAL_WINDOW_COUNT) {
        return;
    }

    bool is_4g_window = (tlb_index >= TLB_2M_WINDOW_COUNT);
    int config_index = is_4g_window ? (tlb_index - TLB_2M_WINDOW_COUNT) : tlb_index;

    if (is_4g_window) {
        // 4G window handling (if needed)
    } else {
        TLB_2M_REG *config = &tt->tlb_2m_configs[config_index];
        int x = config->x_end, y = config->y_end;
        virt2log(&x, &y);

        if (config->multicast) {
            int xs = config->x_start, ys = config->y_start;
            virt2log(&xs, &ys);
            // If previous multicast on this TLB had different target, flush first
            if (tt->mcast_state[tlb_index].active &&
                (tt->mcast_state[tlb_index].xs != xs || tt->mcast_state[tlb_index].ys != ys ||
                 tt->mcast_state[tlb_index].xe != x  || tt->mcast_state[tlb_index].ye != y)) {
                flush_mcast(tt, tlb_index);
            }
            tt->mcast_state[tlb_index].active = true;
            tt->mcast_state[tlb_index].xs = xs;
            tt->mcast_state[tlb_index].ys = ys;
            tt->mcast_state[tlb_index].xe = x;
            tt->mcast_state[tlb_index].ye = y;
        } else {
            // Switching from multicast to unicast: flush pending data
            if (tt->mcast_state[tlb_index].active) {
                flush_mcast(tt, tlb_index);
            }
        }

        uint8_t *ram = obtain_node_mem(tt, x, y);
        remap_ram_region(tt, tlb_index, ram, is_arcnode(x, y));
    }
}

// CSM MMIO read/write functions
static uint64_t tenstorrent_csm_read(void *opaque, hwaddr addr, unsigned size)
{
    TenstorrentState *tt = opaque;
    uint64_t val = 0;

    if (addr >= CSM_REGS_SIZE) {
        return 0;
    }
    
    // Handle specific CSM register reads - only ARC-related registers
    switch (addr) {
        case ARC_BOOT_STATUS:
            val = 0x5; // ARC_BOOT_STATUS_READY_FOR_MSG && ready to run
            break;
        case ARC_MSG_QCB_PTR:
            // Return the address of the message queue control block within CSM space
            val = ARC_CSM_BASE + ARC_MSG_QCB_OFFSET; // Offset within CSM space
            break;
        case ARC_TELEMETRY_DATA:
            val = ARC_CSM_BASE + TELEMETRY_DATA_OFFSET; // Offset within CSM space
            break;
        case ARC_TELEMETRY_PTR:
            val = ARC_CSM_BASE + TELEMETRY_PTR_OFFSET; // Offset within CSM space
            break;
        case ARC_MSI_FIFO:
            val = 0;
            break;
        default:
            if (addr >= TELEMETRY_PTR_OFFSET && addr < TELEMETRY_PTR_OFFSET + TELEMETRY_PTR_SIZE)
            	val = telemetry_ptr[(addr - TELEMETRY_PTR_OFFSET) / sizeof(uint32_t)];
            else if (addr >= TELEMETRY_DATA_OFFSET && addr < TELEMETRY_DATA_OFFSET + TELEMETRY_DATA_SIZE)
                val = telemetry_data[(addr - TELEMETRY_DATA_OFFSET) / sizeof(uint32_t)];
            else if (addr < CSM_REGS_SIZE) {
                memcpy(&val, &tt->csm_regs[addr], size);
            }
            break;
    }

    return val;
}

static void tenstorrent_csm_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    TenstorrentState *tt = opaque;

    if (addr >= CSM_REGS_SIZE) {
        return;
    }

    // Handle specific CSM register writes
    switch (addr) {
        case ARC_FW_INT_ADDR:
            // ARC firmware interrupt trigger (new UMD path via ARC APB AXI)
            proc_arc_msg(tt);
            break;
        case ARC_MSI_FIFO:
            // Simulate ARC message processing (old UMD path)
            uint32_t res_wptr;
            memcpy(&res_wptr, &tt->csm_regs[0x1114], 4);
            uint32_t new_res_wptr = (res_wptr + 1) % 32;
            memcpy(&tt->csm_regs[0x1114], &new_res_wptr, 4);

            // Write success response
            uint32_t response_slot = new_res_wptr % 16;
            uint32_t response_offset = 0x1520 + response_slot * 32;
            uint32_t success_header = 0;
            memcpy(&tt->csm_regs[response_offset], &success_header, 4);
            break;
        default:
            if (addr < CSM_REGS_SIZE) {
                memcpy(&tt->csm_regs[addr], &val, size);
            }
            break;
    }
}

// Dedicated 2M TLB read function
static uint64_t tenstorrent_tlb_2m_read(TenstorrentState *tt, hwaddr addr, unsigned size)
{
    // Calculate 2M TLB window index and offset within window
    int tlb_index = addr / TLB_2M_WINDOW_SIZE;
    hwaddr offset_in_window = addr % TLB_2M_WINDOW_SIZE;
    
    // Add bounds check
    if (tlb_index < 0 || tlb_index >= TLB_2M_WINDOW_COUNT) {
        return 0;
    }
    
    if (!tt->tlb_configured[tlb_index]) {
        return 0;
    }

    TLB_2M_REG *config = &tt->tlb_2m_configs[tlb_index];
    int y = config->y_end & 0x3F;
    int x = config->x_end & 0x3F;
    
    int x0 = x, y0 = y;
    virt2log(&x, &y);
    
    uint8_t *ram = tt->node_mem_8m[y][x];
    if(ram)
    	return *(uint32_t *)&ram[offset_in_window];
    return (y0 << 6) | x0;
}

// BAR4 operation functions - 4G TLB window space
static uint64_t tenstorrent_bar4_read(void *opaque, hwaddr addr, unsigned size)
{
    TenstorrentState *tt = opaque;

    // Calculate 4G TLB window index and offset within window
    int tlb_index = addr / TLB_4G_WINDOW_SIZE;
    //hwaddr offset_in_window = addr % TLB_4G_WINDOW_SIZE;
    
    // Add bounds check
    if (tlb_index < 0 || tlb_index >= TLB_4G_WINDOW_COUNT) {
        return 0;
    }
    
    // Convert to global TLB index
    int global_tlb_index = TLB_2M_WINDOW_COUNT + tlb_index;
    
    //if (!tt->tlb_configured[global_tlb_index]) {
    //    return 0;
    //}

    // Handle node ID read
    if (tt->tlb_configured[global_tlb_index]) {
        TLB_4G_REG *config = &tt->tlb_4g_configs[tlb_index];
        uint32_t node_id = (config->y_end & 0x3F) << 6 | (config->x_end & 0x3F);
        return node_id;
    } else {
        return 0;
    }
    //}
    
    //return 0;
}

static void tenstorrent_bar4_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    // 4G TLB window is typically read-only
}

static const MemoryRegionOps tenstorrent_bar4_ops = {
    .read = tenstorrent_bar4_read,
    .write = tenstorrent_bar4_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

// BAR2 operation functions - IATU configuration space (simplified implementation)
static uint64_t tenstorrent_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    TenstorrentState *tt = opaque;
    uint64_t val = 0;

    if (addr < BAR2_SIZE) {
        memcpy(&val, &tt->bar2_regs[addr], size);
    }

    return val;
}

static void tenstorrent_bar2_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    TenstorrentState *tt = opaque;

    if (addr < BAR2_SIZE) {
        memcpy(&tt->bar2_regs[addr], &val, size);
    }
}

static const MemoryRegionOps tenstorrent_bar2_ops = {
    .read = tenstorrent_bar2_read,
    .write = tenstorrent_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

// TLB configuration write function
static void tenstorrent_tlb_config_write(TenstorrentState *tt, hwaddr addr, uint64_t val, unsigned size)
{
    int tlb_index = (addr - TLB_REGS_START) / TLB_REG_SIZE;
    int reg_offset = (addr - TLB_REGS_START) % TLB_REG_SIZE;
    
    if (tlb_index < TLB_TOTAL_WINDOW_COUNT) {
        // Determine if this is a 2M window or 4G window
        bool is_4g_window = (tlb_index >= TLB_2M_WINDOW_COUNT);
        int config_index = is_4g_window ? (tlb_index - TLB_2M_WINDOW_COUNT) : tlb_index;
        
        if (is_4g_window) {
            // Handle 4G window configuration
            TLB_4G_REG *config = &tt->tlb_4g_configs[config_index];
            
            switch (reg_offset) {
                case 0:
                    config->low32 = val;
                    break;
                case 4:
                    config->mid32 = val;
                    break;
                case 8:
                    config->high32 = val;
                    tt->tlb_configured[tlb_index] = true;
                    
                    // Update node ID region
                    tenstorrent_update_node_id(tt, tlb_index);
                    break;
            }
        } else {
            // Handle 2M window configuration
            TLB_2M_REG *config = &tt->tlb_2m_configs[config_index];
            
            switch (reg_offset) {
                case 0:
                    config->low32 = val;
                    break;
                case 4:
                    config->mid32 = val;
                    break;
                case 8:
                    config->high32 = val;
                    tt->tlb_configured[tlb_index] = true;
                    
                    tenstorrent_update_node_id(tt, tlb_index);
                    break;
            }
        }
        
        // Also update TLB register storage (for reads)
        //hwaddr tlb_reg_offset = addr - TLB_REGS_START;
        //if (tlb_reg_offset < TLB_REGS_SIZE) {
        //    memcpy(&tt->tlb_regs[tlb_reg_offset], &val, size);
        //}
    }
}

static char * get_timestamp(char *buffer, int size) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    strftime(buffer, 8, "%H:%M:", tm_info);  // Format only hours and minutes
    snprintf(buffer + 6, size - 6, "%02d.%03ld", 
             tm_info->tm_sec, tv.tv_usec / 1000);
    return buffer;
}

static void flush_mem(Rv32Core * rv32core) {
    if(*(volatile uint32_t *)&rv32core->ram[rv32core->start_addr] == 0)
        usleep(1000);
}

static void noc_read(void* tt_ptr, void* core_ptr);
static void noc_write(void* tt_ptr, void* core_ptr);
static void start_eth_core(TenstorrentState *tt, int x, int y,
                           Rv32Core *core, uint8_t *ram, uint32_t start_addr,
                           bool is_primary);
static void co_yield(void *tt_ptr, void * core_ptr){
    Rv32Core * rv32core = (Rv32Core *)core_ptr;
    TenstorrentState *tt = (TenstorrentState *)tt_ptr;

    if(rv32core->core_type == N_CORE) {
        noc_read(tt_ptr, core_ptr);
        noc_write(tt_ptr, core_ptr);
    }

    // Advance wall clock timer so firmware riscv_wait() can complete.
    // Firmware reads 0xFFB121F0/F8 → ram[TLB_2M_WINDOW_SIZE + WALL_CLOCK_L/H]
    {
        uint32_t *wc_lo = (uint32_t *)&rv32core->ram[TLB_2M_WINDOW_SIZE + WALL_CLOCK_L];
        uint32_t old = *wc_lo;
        *wc_lo += INSTRUCTIONS_PER_STEP;
        if (*wc_lo < old)
            (*(uint32_t *)&rv32core->ram[TLB_2M_WINDOW_SIZE + WALL_CLOCK_H])++;
    }

    // ETH DM1 startup detection: when DM0 writes subordinate PC, start DM1
    // DM0 firmware writes to 0xFFB14008, which rv32-emu maps to ram[mem_size + 0x14008]
    // mem_size = TLB_2M_WINDOW_SIZE (0x200000), so check ram[0x200000 + 0x14008]
    if (get_node_type(rv32core->node_x, rv32core->node_y) == ETH) {
        EthNodeState *eth = &tt->node_core[rv32core->node_y][rv32core->node_x].eth;
        if (!eth->dm1_started) {
            uint32_t dm1_pc = *(uint32_t *)&rv32core->ram[TLB_2M_WINDOW_SIZE + SUBORDINATE_IERISC_RESET_PC];
            if (dm1_pc != 0) {
                TT_LOG(LOG_LEVEL_INFO, "ETH (%d,%d): starting DM1 at pc=0x%x\n",
                       rv32core->node_x, rv32core->node_y, dm1_pc);
                start_eth_core(tt, rv32core->node_x, rv32core->node_y,
                               &eth->cores[ETH_DM1], rv32core->ram, dm1_pc, false);
                eth->dm1_started = true;
            }
        }
    }

    coroutine_yield_with_handle(rv32core->coroutine_ctx);
}

static void rv32_core_coroutine(void* tt_ptr, void* core_ptr) {

    Rv32Core * rv32core = (Rv32Core *)core_ptr;

    flush_mem(rv32core);

    struct subordinate_sync_msg_t * sync_msg =
        (struct subordinate_sync_msg_t *)&rv32core->ram[(uint64_t)(&((mailboxes_t *)(MEM_MAILBOX_BASE))->subordinate_sync)];

    if(rv32core->wait_init) {
        if(rv32core->core_type == N_CORE) {
            while(sync_msg->dm1 != RUN_SYNC_MSG_INIT)
       	        co_yield(tt_ptr, core_ptr);
        } else if(rv32core->core_type == T0_CORE) {
            while(sync_msg->trisc0 != RUN_SYNC_MSG_INIT)
       	        co_yield(tt_ptr, core_ptr);
        } else if(rv32core->core_type == T1_CORE) {
            while(sync_msg->trisc1 != RUN_SYNC_MSG_INIT)
       	        co_yield(tt_ptr, core_ptr);
        } else if(rv32core->core_type == T2_CORE) {
            while(sync_msg->trisc2 != RUN_SYNC_MSG_INIT)
       	        co_yield(tt_ptr, core_ptr);
        }
       	rv32core->wait_init = false;
    }
    rv32_run_co(rv32core->rv32cpu, rv32core->yield_threshold, co_yield, tt_ptr, rv32core);
}

#define NOC_CMD_BUF_OFF(noc, cmd_buf, addr) \
	((noc << NOC_INSTANCE_OFFSET_BIT) \
	 + (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + addr \
	 - MEM_LOCAL_BASE)
	 
#define NOC_CMD_BUF_OFF1(noc, cmd_buf, addr) \
	((noc << NOC_INSTANCE_OFFSET_BIT) \
	 + (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + addr)
	 
#define GET_MEM1(offset, lo, hi, ldm, ldm_size) (offset < MEM_LOCAL_BASE ? &lo[offset] : \
	(offset < ldm_size + MEM_LOCAL_BASE) ? &ldm[offset - MEM_LOCAL_BASE] : &hi[offset - MEM_LOCAL_BASE])

static void data_read(TenstorrentState *tt, Rv32Core *rv32core, int noc, int cmd_buf, 
	uint8_t * iatu_config, uint8_t * lo_mem, uint8_t * hi_mem) {
    int off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_TARG_ADDR_LO);
    uint32_t src_addr = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_TARG_ADDR_HI);
    uint32_t src_hi = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_AT_LEN_BE);
    uint32_t size = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_RET_ADDR_LO);
    uint32_t dst_addr = *(uint32_t *)&hi_mem[off];
    
    int x = src_hi & 0x3f, y = (src_hi >> 6) & 0x3f;
    virt2log(&x, &y);
    if( get_node_type(x, y) == PCI) {
        AddressSpace * dev_as = pci_get_address_space(&tt->parent_obj);
        address_space_read(dev_as, 
                         *(uint32_t *)&iatu_config[ATU_OFFSET_IN_BH_BAR2 + ATU_REGION_ADDR_OFF] + src_addr, 
                         MEMTXATTRS_UNSPECIFIED, 
                         &lo_mem[dst_addr], 
                         size);
    } else {
    	tt->node_mem_8m[y][x] = obtain_node_mem(tt, x, y);
        memcpy(&lo_mem[dst_addr], tt->node_mem_8m[y][x] + src_addr, size);
    }

    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_CMD_CTRL);
    *(uint32_t *)&hi_mem[off] = 0;
    
    uint32_t resp_off = NOC_STATUS_READ_REG_OFF(noc, NIU_MST_RD_RESP_RECEIVED) - MEM_LOCAL_BASE;
    uint32_t *resp = (uint32_t *)&hi_mem[resp_off];
    (*resp)++;
    //uint32_t num_issued = NOC_READS_NUM_ISSUED - MEM_LOCAL_BASE;
    uint32_t num_issued = *(uint32_t *)GET_MEM1(NOC_READS_NUM_ISSUED, lo_mem, hi_mem, rv32core->ldm, rv32core->ldm_size);
    
    TT_LOG(LOG_LEVEL_VERBOSE, "src_hi 0x%x , noc %d, read data from 0x%x to (%d, %d) 0x%x, len is %d, num_issued %d, resp_received %d\n", src_hi, noc, src_addr,
    	rv32core->node_x, rv32core->node_y, dst_addr, size, num_issued, *resp);
}

static void noc_read(void* tt_ptr, void* core_ptr) {
    TenstorrentState *tt = (TenstorrentState *)tt_ptr;
    Rv32Core *rv32core = (Rv32Core *)core_ptr;
    
    uint8_t * lo_mem = tt->node_mem_8m[rv32core->node_y][rv32core->node_x];
    uint8_t *hi_mem = lo_mem + TLB_2M_WINDOW_SIZE;
    for (int noc = 0; noc < NUM_NOCS; noc++) {
    	uint32_t ctrl_off = NOC_CMD_BUF_OFF(noc, RD_CMD_BUF, NOC_CMD_CTRL);
    	if(*(uint32_t *)&hi_mem[ctrl_off] == NOC_CTRL_SEND_REQ) {
	    data_read(tt, rv32core, noc, RD_CMD_BUF, tt->bar2_regs, lo_mem, hi_mem);	//false for read
    	}
    }
}

static void data_write(TenstorrentState *tt, Rv32Core *rv32core, int noc, int cmd_buf, 
	uint8_t * lo_mem, uint8_t * hi_mem) {
    int off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_TARG_ADDR_LO);
    uint32_t src_addr = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_AT_LEN_BE);
    uint32_t size = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_RET_ADDR_LO);
    uint32_t dst_lo = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_RET_ADDR_MID);
    uint32_t dst_mid = *(uint32_t *)&hi_mem[off];
    
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_RET_ADDR_HI);
    uint32_t dst_hi = *(uint32_t *)&hi_mem[off];
    int x = dst_hi & 0x3f, y = (dst_hi >> 6) & 0x3f;
    if (x >= BH_GRID_X || y >= BH_GRID_Y)
    	virt2log(&x, &y);
    if (get_node_type(x, y) != PCI) {
        tt->node_mem_8m[y][x] = obtain_node_mem(tt, x, y);
        memcpy(tt->node_mem_8m[y][x] + dst_lo, &lo_mem[src_addr], size);
    } else {
        uint8_t *iatu_config = tt->bar2_regs;
        AddressSpace *dev_as = pci_get_address_space(&tt->parent_obj);
        uint64_t host_addr = *(uint32_t *)&iatu_config[ATU_OFFSET_IN_BH_BAR2 + ATU_REGION_ADDR_OFF] + dst_lo;
        address_space_write(dev_as, host_addr, MEMTXATTRS_UNSPECIFIED, &lo_mem[src_addr], size);
    }
    
    uint32_t noc_ctrl_off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_CTRL);
    uint32_t noc_ctrl = *(uint32_t *)&hi_mem[noc_ctrl_off];

    uint32_t ctrl_off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_CMD_CTRL);
    *(uint32_t *)&hi_mem[ctrl_off] = 0;

    // Update status registers based on write type
    if (noc_ctrl & NOC_CMD_RESP_MARKED) {
        // Nonposted write (requires response)
        uint32_t req_off = NOC_CMD_BUF_OFF(noc, 0, NOC_STATUS(NIU_MST_NONPOSTED_WR_REQ_SENT));
        (*(uint32_t *)&hi_mem[req_off])++;
        uint32_t ack_off = NOC_CMD_BUF_OFF(noc, 0, NOC_STATUS(NIU_MST_WR_ACK_RECEIVED));
        (*(uint32_t *)&hi_mem[ack_off])++;
    } else {
        // Posted write (no response required)
        uint32_t req_off = NOC_CMD_BUF_OFF(noc, 0, NOC_STATUS(NIU_MST_POSTED_WR_REQ_SENT));
        (*(uint32_t *)&hi_mem[req_off])++;
    }
    
    uint32_t num_acked = NPST_WRITES_NUM_ACKED - MEM_LOCAL_BASE;

    TT_LOG(LOG_LEVEL_VERBOSE, "oper (%d, %d), noc %d, write data begin, cmd_buf is %d, src is 0x%x, dst_lo is 0x%x, dst_mid is 0x%x, dst_hi is 0x%x, size is %d, \
    	noc_ctrl 0x%x, num_acked is %d!\n", rv32core->node_x, rv32core->node_y, noc, cmd_buf, src_addr, dst_lo, dst_mid, \
    	dst_hi, size, noc_ctrl, hi_mem[num_acked]);
}

static void atom_write(TenstorrentState *tt, Rv32Core *rv32core, int noc, int cmd_buf, uint8_t * lo_mem, uint8_t * hi_mem) {
    static time_t last_print = 0;
    time_t now = time(NULL);
    if (now - last_print >= 3) {
        char buf[20];
        TT_LOG(LOG_LEVEL_INFO, "running... (%d,%d) noc %d, time %s\n",
            rv32core->node_x, rv32core->node_y, noc, get_timestamp(buf, sizeof(buf)));
        last_print = now;
    }
    int off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_TARG_ADDR_LO);
    uint32_t src_addr = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_TARG_ADDR_HI);
    uint32_t src_hi = *(uint32_t *)&hi_mem[off];
    off =NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_CTRL);
    uint32_t ctrl = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_AT_LEN_BE);
    uint32_t size = *(uint32_t *)&hi_mem[off];
    //off = AT_CB_OFFSET + NOC_RET_ADDR_LO - MEM_LOCAL_BASE;
    //uint32_t dst_addr = *(uint32_t *)&hi_mem[off];
    off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_AT_DATA);
    uint32_t at_data = *(uint32_t *)&hi_mem[off];
    
    int x = src_hi & 0x3f, y = (src_hi >> 6) & 0x3f;
    uint8_t * dsmem = tt->node_mem_8m[y][x];
    uint8_t * ds_himem = dsmem + TLB_2M_WINDOW_SIZE;
    uint8_t * sem_p = src_addr < MEM_LOCAL_BASE ? (dsmem + src_addr) : (ds_himem  + src_addr - MEM_LOCAL_BASE);

    *(volatile uint32_t *)sem_p += at_data;
    
    if(src_addr >= NOC_OVERLAY_START_ADDR && src_addr < NOC_OVERLAY_START_ADDR + NOC_NUM_STREAMS * NOC_STREAM_REG_SPACE_SIZE) {
    	int stream_id = (src_addr - NOC_OVERLAY_START_ADDR) / NOC_STREAM_REG_SPACE_SIZE;
    	int reg_id = (src_addr % NOC_STREAM_REG_SPACE_SIZE) / 4;
    	if(reg_id == STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_REG_INDEX){
    	    uint32_t hioff = STREAM_REG_ADDR(stream_id, STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_REG_INDEX) - MEM_LOCAL_BASE;
    	    *(volatile uint32_t *)(ds_himem + hioff) += at_data;
    	}
    }
    
    uint32_t resp_off = NOC_CMD_BUF_OFF(noc, 0, NOC_STATUS(NIU_MST_ATOMIC_RESP_RECEIVED));
    //uint32_t num_issued = NPST_ATOMICS_ACKED - MEM_LOCAL_BASE;
    (*(uint32_t *)&hi_mem[resp_off])++;
    
    uint32_t ctrl_off = NOC_CMD_BUF_OFF(noc, cmd_buf, NOC_CMD_CTRL);
    *(uint32_t *)&hi_mem[ctrl_off] = 0;
    
    TT_LOG(LOG_LEVEL_VERBOSE, "oper (%d, %d), cmd_buf 0x%x, noc %d, atom data begin, src_addr is 0x%x, src_hi is 0x%x, at_data is 0x%x, ctrl is 0x%08x, size is %d, \
    	resp_ack %d!\n", rv32core->node_x, rv32core->node_y, cmd_buf, noc, src_addr,
    	src_hi, at_data, ctrl, size, hi_mem[resp_off]);
}

static bool is_s_wr_cmdbuf_enabled (TenstorrentState *tt, Rv32Core *rv32core, int noc) {
    uint8_t * lo_mem = tt->node_mem_8m[rv32core->node_y][rv32core->node_x];
    uint8_t *hi_mem = lo_mem + TLB_2M_WINDOW_SIZE;
    uint32_t off = NOC_CMD_BUF_OFF(noc, DISPATCH_S_WR_REG_CMD_BUF, NOC_TARG_ADDR_HI);
    uint32_t src_hi = *(uint32_t *)&hi_mem[off];
    int x = src_hi & 0x3f, y = (src_hi >> 6) & 0x3f;
    return (x == rv32core->node_x) && (y == rv32core->node_y);
}

static bool is_s_atom_cmdbuf_enabled (TenstorrentState *tt, Rv32Core *rv32core, int noc) {
    uint8_t * lo_mem = tt->node_mem_8m[rv32core->node_y][rv32core->node_x];
    uint8_t *hi_mem = lo_mem + TLB_2M_WINDOW_SIZE;
    uint32_t off = NOC_CMD_BUF_OFF(noc, DISPATCH_S_ATOMIC_CMD_BUF, NOC_RET_ADDR_HI);
    uint32_t src_hi = *(uint32_t *)&hi_mem[off];
    int x = src_hi & 0x3f, y = (src_hi >> 6) & 0x3f;
    return (x == rv32core->node_x) && (y == rv32core->node_y);
}

static void noc_write(void* tt_ptr, void* core_ptr) {
    TenstorrentState *tt = (TenstorrentState *)tt_ptr;
    Rv32Core *rv32core = (Rv32Core *)core_ptr;
    
    uint8_t * lo_mem = tt->node_mem_8m[rv32core->node_y][rv32core->node_x];
    uint8_t *hi_mem = lo_mem + TLB_2M_WINDOW_SIZE;
    
    for (int noc = 0; noc < NUM_NOCS; noc++) {
        uint32_t ctrl_off = NOC_CMD_BUF_OFF(noc, WR_CMD_BUF, NOC_CMD_CTRL);
        if(*(uint32_t *)&hi_mem[ctrl_off] == NOC_CTRL_SEND_REQ) {
            data_write(tt, rv32core, noc, WR_CMD_BUF, lo_mem, hi_mem);
        }
        
        if (noc == 1 && is_s_wr_cmdbuf_enabled(tt, rv32core, noc)) {
            ctrl_off = NOC_CMD_BUF_OFF(noc, DISPATCH_S_WR_REG_CMD_BUF, NOC_CMD_CTRL);
            if(*(uint32_t *)&hi_mem[ctrl_off] == NOC_CTRL_SEND_REQ) {
                data_write(tt, rv32core, noc, DISPATCH_S_WR_REG_CMD_BUF, lo_mem, hi_mem);
            }
        }
        
        if (noc == 0 || !is_s_atom_cmdbuf_enabled(tt, rv32core, noc)) {
            ctrl_off = NOC_CMD_BUF_OFF(noc, WR_REG_CMD_BUF, NOC_CMD_CTRL);
            if(*(uint32_t *)&hi_mem[ctrl_off] == NOC_CTRL_SEND_REQ) {
                data_write(tt, rv32core, noc, WR_REG_CMD_BUF, lo_mem, hi_mem);
            }
        } else {
            ctrl_off = NOC_CMD_BUF_OFF(noc, DISPATCH_S_ATOMIC_CMD_BUF, NOC_CMD_CTRL);
            if(*(uint32_t *)&hi_mem[ctrl_off] == NOC_CTRL_SEND_REQ) {
                atom_write(tt, rv32core, noc, DISPATCH_S_ATOMIC_CMD_BUF, lo_mem, hi_mem);
            }
        }
        
        ctrl_off = NOC_CMD_BUF_OFF(noc, AT_CMD_BUF, NOC_CMD_CTRL);
        if(*(uint32_t *)&hi_mem[ctrl_off] == NOC_CTRL_SEND_REQ) {
            atom_write(tt, rv32core, noc, AT_CMD_BUF, lo_mem, hi_mem);
        }
    }
}

static void stop_tensix_node(int x, int y, Rv32Core * tensix_cores) {
    TT_LOG(LOG_LEVEL_DEBUG, "stop_tensix_node (%d,%d): stopping\n", x, y);
    for(int i = B_CORE; i < NUM_CORES; ++i) {
        Rv32Core * rv32core = &tensix_cores[i];
        if (rv32core->coroutine_ctx) {
            if(rv32core->rv32cpu)
                rv32_halt(rv32core->rv32cpu);
            rv32core->should_stop = true;
            
            //usleep(1000);
            
            coroutine_destroy_sync(rv32core->coroutine_ctx);
            rv32core->coroutine_ctx = NULL;
        }
        
        if(rv32core->rv32cpu) {
            rv32_destroy(rv32core->rv32cpu);
            rv32core->rv32cpu = NULL;
        }
    }
}



static void start_tensix_node(TenstorrentState *tt, int x, int y, Rv32Core * tensix_cores, uint8_t *ram) {

    // Tensix memory layout (all memory is in ram, no extra allocation needed):
    // ram + 0x000000 - 0x17FFFF: L1 scratchpad (1.5MB)
    // ram + 0x180000 - 0x1FFFFF: Tensix state (512KB, stores tensix_t)
    // ram + 0x200000 - 0x7FFFFF: high_mem (6MB, mapped to 0xFFB00000+)

    tensix_t *tensix_ptr = (tensix_t *)(ram + 0x180000);  // Tensix state at ram + 1.5MB
    uint8_t *l1_scratchpad = ram;                         // L1 scratchpad (1.5MB)
    uint8_t *high_mem = ram + 0x200000;                   // High mem (6MB, maps to 0xFFB00000+)

    // Initialize Tensix coprocessor
    // Each baby core has its own private LDM (ldm[] in Rv32Core), high_mem is shared/public
    memset(high_mem, 0, HIGH_MEM_SIZE);
    tensix_init(tensix_ptr, l1_scratchpad, high_mem,
                tensix_cores[T0_CORE].ldm,
                tensix_cores[T1_CORE].ldm,
                tensix_cores[T2_CORE].ldm);

    queue_handle_t assigned_queue = NULL;

    char buf[20];
    for(int i = B_CORE; i < NUM_CORES; ++i) {
        Rv32Core * rv32core = &tensix_cores[i];
        if(rv32core->rv32cpu)
            continue;

        rv32core->ram = ram;
        rv32core->core_type = i;
        rv32core->node_x = x;
        rv32core->node_y = y;
        memset(rv32core->ldm, 0, sizeof(rv32core->ldm));

        int core_id;  // core_id for rv32_create: B=-1, T0=0, T1=1, T2=2, NC=other
        tensix_t *core_tensix_ptr = NULL;

        switch(i) {
            case B_CORE:
                rv32core->start_addr = MEM_BRISC_FIRMWARE_BASE;
                rv32core->wait_init = false;
                rv32core->ldm_size = MEM_BRISC_LOCAL_SIZE;
                core_id = -1;  // B core
                core_tensix_ptr = tensix_ptr;  // B core needs Tensix for stream overlay MMIO
                break;
            case N_CORE:
                rv32core->start_addr = MEM_NCRISC_FIRMWARE_BASE;
                rv32core->wait_init = true;
                rv32core->ldm_size = MEM_NCRISC_LOCAL_SIZE;
                core_id = 3;  // NC core (does not use Tensix)
                core_tensix_ptr = NULL;
                break;
            case T0_CORE:
                rv32core->start_addr = MEM_TRISC0_FIRMWARE_BASE;
                rv32core->wait_init = true;
                rv32core->ldm_size = MEM_TRISC_LOCAL_SIZE;
                core_id = 0;  // T0
                core_tensix_ptr = tensix_ptr;  // Use the initialized tensix_ptr
                break;
            case T1_CORE:
                rv32core->start_addr = MEM_TRISC1_FIRMWARE_BASE;
                rv32core->wait_init = true;
                rv32core->ldm_size = MEM_TRISC_LOCAL_SIZE;
                core_id = 1;  // T1
                core_tensix_ptr = tensix_ptr;  // Use the initialized tensix_ptr
                break;
            case T2_CORE:
                rv32core->start_addr = MEM_TRISC2_FIRMWARE_BASE;
                rv32core->wait_init = true;
                rv32core->ldm_size = MEM_TRISC_LOCAL_SIZE;
                core_id = 2;  // T2
                core_tensix_ptr = tensix_ptr;  // Use the initialized tensix_ptr
                break;
        }

        // Create RISC-V core, passing tensix pointer and core_id
        rv32core->rv32cpu = rv32_create(ram, NODE_MEM_SIZE - HIGH_MEM_SIZE,
                                         rv32core->ldm, rv32core->ldm_size,
                                         rv32core->start_addr, INSTRUCTIONS_PER_STEP,
                                         core_tensix_ptr, core_id);
        assert(rv32core->rv32cpu);

        rv32core->yield_threshold = STEPS_PER_YIELD;
        rv32core->instructions_executed = 0;
        rv32core->should_stop = false;
        
        // Create coroutine
        rv32core->coroutine_ctx = coroutine_create(rv32_core_coroutine, tt, rv32core);
        if (!rv32core->coroutine_ctx) {
            fprintf(stderr, "Failed to create coroutine for tensix core (%d,%d)-%d\n", x, y, i);
            rv32_destroy(rv32core->rv32cpu);
            rv32core->rv32cpu = NULL;
            continue;
        }
        
        // Add to scheduler
        assigned_queue = scheduler_add_coroutine(tt->scheduler, rv32core->coroutine_ctx, assigned_queue);
        
        if (!assigned_queue) {
            fprintf(stderr, "Failed to add coroutine for tensix core (%d,%d)-%d to scheduler\n", x, y, i);
            coroutine_destroy(rv32core->coroutine_ctx);
            rv32_destroy(rv32core->rv32cpu);
            rv32core->rv32cpu = NULL;
            continue;
        }
        
        TT_LOG(LOG_LEVEL_DEBUG, "start_tensix_node x %d, y %d, core_type: %d, time %s\n", x, y, rv32core->core_type, get_timestamp(buf, sizeof(buf)));

        tt->total_coroutines++;
    }
    TT_LOG(LOG_LEVEL_INFO, "start_tensix_node (%d,%d) %d cores, time %s\n", x, y, NUM_CORES, get_timestamp(buf, sizeof(buf)));
    // Rebalance queues after adding a tensix node
    scheduler_reorder_queues(tt->scheduler);
}

static void reset_tensix_node(TenstorrentState *tt, int x, int y, uint64_t val, uint8_t *ram) {
    Rv32Core * tensix_cores = tt->node_core[y][x].tensix.tensix_cores;

    // Stop core on any reset write
    stop_tensix_node(x, y, tensix_cores);

    // Distinguish assert vs deassert by checking the actual RISC reset control
    // bit (bit 11 = 0x800), NOT bit 31 (staggered_start).  UMD assert_risc_reset
    // does a read-modify-write that inherits bit 31 from a previous deassert,
    // so bit 31 alone cannot distinguish the two operations.
    //   assert  writes 0x47800  (bit11=1, cores held in reset)
    //   deassert writes 0x80047000 (bit11=0, cores released)
    //   atexit assert writes 0x80047800 (bit11=1, bit31 inherited — still an assert!)
    bool is_deassert = (val & 0x800) == 0;

    if(is_deassert) {
        uint8_t signal = ram[(uint64_t)(&((mailboxes_t *)(MEM_MAILBOX_BASE))->go_messages[0].signal)];
        uint32_t fw = *(uint32_t *)&ram[MEM_BRISC_FIRMWARE_BASE];

        if(signal == RUN_MSG_INIT && fw != 0) {
            TT_LOG(LOG_LEVEL_DEBUG, "reset_tensix_node (%d,%d): deassert, starting core (fw=0x%x, signal=0x%x)\n",
                   x, y, fw, signal);
            start_tensix_node(tt, x, y, tensix_cores, ram);
        }
    }
}

static void stop_eth_core(Rv32Core *core) {
    // Ensure state consistency
    assert(!(core->rv32cpu && !core->coroutine_ctx) && "Inconsistent state: rv32cpu without coroutine_ctx");
    assert(!(!core->rv32cpu && core->coroutine_ctx) && "Inconsistent state: coroutine_ctx without rv32cpu");

    if (core->coroutine_ctx) {
        TT_LOG(LOG_LEVEL_DEBUG, "Stopping eth core (%d,%d)\n", core->node_x, core->node_y);
        rv32_halt(core->rv32cpu);
        core->should_stop = true;

        // Use synchronous destruction
        coroutine_destroy_sync(core->coroutine_ctx);
        core->coroutine_ctx = NULL;

        // Immediately destroy rv32cpu
        if(core->rv32cpu) {
            rv32_destroy(core->rv32cpu);
            core->rv32cpu = NULL;
        }
        core->ram = NULL;
    } else if (core->rv32cpu) {
        // If only rv32cpu exists, destroy it directly
        TT_LOG(LOG_LEVEL_INFO, "Warning: Only rv32cpu exists for eth core (%d,%d), destroying it\n",
               core->node_x, core->node_y);
        rv32_destroy(core->rv32cpu);
        core->rv32cpu = NULL;
        core->ram = NULL;
    }
}

static void stop_eth_node(EthNodeState *eth) {
    stop_eth_core(&eth->cores[ETH_DM0]);
    stop_eth_core(&eth->cores[ETH_DM1]);
    // Clear reset PCs to prevent false trigger on next restart
    if (eth->cores[ETH_DM0].ram) {
        // Host writes DM0 PC through TLB → ram[TLB_RAM_SIZE + IERISC_RESET_PC]
        *(uint32_t *)&eth->cores[ETH_DM0].ram[TLB_RAM_SIZE + IERISC_RESET_PC] = 0;
        // Firmware writes DM1 PC to 0xFFB14008 → ram[TLB_2M_WINDOW_SIZE + SUBORDINATE_IERISC_RESET_PC]
        *(uint32_t *)&eth->cores[ETH_DM0].ram[TLB_2M_WINDOW_SIZE + SUBORDINATE_IERISC_RESET_PC] = 0;
    }
    eth->dm1_started = false;
}

static void start_eth_core(TenstorrentState *tt, int x, int y,
                           Rv32Core *core, uint8_t *ram, uint32_t start_addr,
                           bool is_primary) {
    if(core->rv32cpu)
        return;

    core->ram = ram;
    core->start_addr = start_addr;
    core->core_type = B_CORE;
    core->node_x = x;
    core->node_y = y;
    memset(core->ldm, 0, sizeof(core->ldm));
    // Only clear hi-mem for primary core (DM0); DM1 shares ram with DM0
    if (is_primary) {
        memset(ram + 0x200000, 0, HIGH_MEM_SIZE);
    }

    core->rv32cpu = rv32_create(ram, NODE_MEM_SIZE - HIGH_MEM_SIZE,
    	core->ldm, MEM_BRISC_LOCAL_SIZE, core->start_addr, INSTRUCTIONS_PER_STEP, NULL, -1);
    assert(core->rv32cpu);

    core->yield_threshold = STEPS_PER_YIELD;
    core->instructions_executed = 0;
    core->should_stop = false;
    core->wait_init = false;

    // Create coroutine
    core->coroutine_ctx = coroutine_create(rv32_core_coroutine, tt, core);
    if (!core->coroutine_ctx) {
        fprintf(stderr, "Failed to create coroutine for eth core (%d,%d)\n", x, y);
        rv32_destroy(core->rv32cpu);
        core->rv32cpu = NULL;
        return;
    }

    // Add to scheduler
    if (!scheduler_add_coroutine(tt->scheduler, core->coroutine_ctx, NULL)) {
        fprintf(stderr, "Failed to add coroutine for eth core (%d,%d) to scheduler\n", x, y);
        coroutine_destroy(core->coroutine_ctx);
        rv32_destroy(core->rv32cpu);
        core->rv32cpu = NULL;
        return;
    }

    char buf[20];
    TT_LOG(LOG_LEVEL_DEBUG, "start_eth_core (%d,%d) pc=0x%x, time %s\n", x, y, start_addr, get_timestamp(buf, sizeof(buf)));

    tt->total_coroutines++;

    // Rebalance after adding a few eth nodes
    static int eth_counter = 0;
    eth_counter++;
    if (eth_counter >= 3) {
        scheduler_reorder_queues(tt->scheduler);
        eth_counter = 0;
    }
}

static void reset_eth_node(TenstorrentState *tt, int x, int y, uint64_t val, uint8_t *ram) {
    EthNodeState *eth = &tt->node_core[y][x].eth;

    // Check actual RISC reset bit (bit 11), not staggered_start (bit 31).
    // See reset_tensix_node comment for rationale.
    bool is_deassert = (val & 0x800) == 0;

    // Read DM0 PC and signal BEFORE stop_eth_node, because stop_eth_node clears
    // the reset PC from ram. If the old buffer was reallocated at the same address,
    // stop_eth_node would wipe the PC the host just wrote.
    uint32_t dm0_pc = *(uint32_t *)&ram[TLB_RAM_SIZE + IERISC_RESET_PC];

    // Check go_message signal
    uint64_t signal_offset = (uint64_t)(&((mailboxes_t *)(MEM_AERISC_MAILBOX_BASE))->go_messages[0].signal);
    uint8_t signal_value = ram[signal_offset];

    // Stop all cores on any reset write
    stop_eth_node(eth);

    TT_LOG(LOG_LEVEL_DEBUG, "reset_eth_node (%d,%d): val=0x%lx, deassert=%d, dm0_pc=0x%x, signal=0x%x\n",
           x, y, (unsigned long)val, is_deassert, dm0_pc, signal_value);

    if (!is_deassert) return;
    if (signal_value != RUN_MSG_INIT) return;

    // If host wrote DM0 PC to ram[IERISC_RESET_PC], use it (new tt-metal).
    // Otherwise fall back to fixed MEM_AERISC_FIRMWARE_BASE (old tt-metal).
    if (dm0_pc == 0) {
        uint32_t fw_first_word = *(uint32_t *)&ram[MEM_AERISC_FIRMWARE_BASE];
        if (fw_first_word == 0) return;
        dm0_pc = MEM_AERISC_FIRMWARE_BASE;
    }

    TT_LOG(LOG_LEVEL_DEBUG, "reset_eth_node (%d,%d): starting DM0 at pc=0x%x\n", x, y, dm0_pc);
    start_eth_core(tt, x, y, &eth->cores[ETH_DM0], ram, dm0_pc, true);
    eth->dm1_started = false;
}

static void process_control(TenstorrentState *tt, int x, int y, int offset, uint64_t val, uint8_t *ram) {
    if(offset == (TENSIX_SOFT_RESET_ADDR & TLB_2M_WINDOW_MASK)) {
        if(get_node_type(x, y) == ETH) {
            reset_eth_node(tt, x, y, val, ram);
        } else if(get_node_type(x, y) == TENSIX) {
            reset_tensix_node(tt, x, y, val, ram);
        }
    }
}

//directly write tlb window
static void tenstorrent_tlb_2m_write(TenstorrentState *tt, hwaddr addr, uint64_t val, unsigned size){
    int tlb_index = addr / TLB_2M_WINDOW_SIZE;
    int offset = addr & TLB_2M_WINDOW_MASK;

    if(!tt->tlb_configured[tlb_index])
    	return;

    TLB_2M_REG *config = &tt->tlb_2m_configs[tlb_index];

    if (config->multicast) {
        int xs = config->x_start, ys = config->y_start;
        int xe = config->x_end,   ye = config->y_end;
        virt2log(&xs, &ys);
        virt2log(&xe, &ye);

        // For soft_reset (process_control), flush first 1MB from RAM-mapped node
        // to all other nodes first. The RAM region only covers (x_end, y_end), so
        // firmware/go_message data written through the RAM path must be propagated
        // before reset_tensix_node checks signal/fw conditions.
        if (offset >= TLB_RAM_SIZE && tt->mcast_state[tlb_index].active) {
            flush_mcast(tt, tlb_index);
            tt->mcast_state[tlb_index].active = true;  // keep active after flush
        }

        int started_tensix = 0, started_eth = 0;
        for (int y = ys; y <= ye; y++) {
            for (int x = xs; x <= xe; x++) {
                uint8_t *buf = obtain_node_mem(tt, x, y);
                if (offset >= TLB_RAM_SIZE)
                    process_control(tt, x, y, offset, val, buf);
                memcpy(&buf[offset], &val, size);
                // Count started cores for summary
                if (offset == (TENSIX_SOFT_RESET_ADDR & TLB_2M_WINDOW_MASK)) {
                    NodeType nt = get_node_type(x, y);
                    if (nt == TENSIX && tt->node_core[y][x].tensix.tensix_cores[B_CORE].rv32cpu)
                        started_tensix++;
                    else if (nt == ETH && tt->node_core[y][x].eth.cores[ETH_DM0].rv32cpu)
                        started_eth++;
                }
            }
        }
        if (started_tensix || started_eth) {
            bool is_deassert = (val & 0x800) == 0;
            char buf[20];
            TT_LOG(LOG_LEVEL_INFO, "mcast soft_reset (%d,%d)-(%d,%d): %s, started %d tensix + %d eth, val=0x%lx, time %s\n",
                   xs, ys, xe, ye, is_deassert ? "deassert" : "assert",
                   started_tensix, started_eth, (unsigned long)val, get_timestamp(buf, sizeof(buf)));
        }
    } else {
        int x = config->x_end & 0x3F;
        int y = config->y_end & 0x3F;
        virt2log(&x, &y);
        uint8_t *buf = obtain_node_mem(tt, x, y);
        if (offset >= TLB_RAM_SIZE)
            process_control(tt, x, y, offset, val, buf);
        memcpy(&buf[offset], &val, size);
    }
}

// BAR0 operation functions - main MMIO space
static uint64_t tenstorrent_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    TenstorrentState *tt = opaque;
    uint64_t val = 0;

    // Clear address space partitioning
    if (addr > KERNEL_TLB_START && addr < KERNEL_TLB_START + KERNEL_TLB_LEN) {
        // TLB-mapped CSM window access
        hwaddr csm_offset = addr - KERNEL_TLB_START;
        val = tenstorrent_csm_read(opaque, csm_offset, size);
    }
    else if (addr >= TLB_REGS_START && addr < TLB_REGS_START + TLB_REGS_LEN) {   //doesnot occur
        // TLB configuration register read
        //hwaddr tlb_reg_offset = addr - TLB_REGS_START;
        //if (tlb_reg_offset < TLB_REGS_SIZE) {
        //    memcpy(&val, &tt->tlb_regs[tlb_reg_offset], size);
        //}
    }
    else if (addr >= NOC2AXI_CFG_START && addr < NOC2AXI_CFG_START + NOC2AXI_CFG_LEN) {
        // NOC2AXI configuration space read
        hwaddr offset = addr - NOC2AXI_CFG_START;
        if (offset < NOC2AXI_CFG_SIZE) {
            switch (offset) {
                case 0x4044:
                    val = 0xB; // PCIe x=11 (TYPE1/Local), ARC accessible over AXI
                    break;
                case 0x4100:
                    val = is_noc_translation_enabled() ? 1 << 14 : 0;	// enable coordinate translation, info in tt_metal/hw/inc/blackhole/noc/noc_parameters.h
                    break;
                case 0x4200:
                    val = tt->status;
                    break;
                case 0x10000:
                    val = 0x1;
                    break;
                default:
                    memcpy(&val, &tt->noc2axi_cfg[offset], size);
                    break;
            }
        }
    }
    else if (addr >= ARC_APB_BAR0_START && addr < ARC_APB_BAR0_START + ARC_APB_BAR0_LEN) {
        // ARC APB AXI path - maps to same registers as CSM
        hwaddr arc_offset = addr - ARC_APB_BAR0_START;
        val = tenstorrent_csm_read(opaque, arc_offset, size);
    }
    else if (addr <= TLB_2M_WINDOWS_END) {
        // 2M TLB window access
        return tenstorrent_tlb_2m_read(tt, addr, size);
    }

    return val;
}

static void tenstorrent_bar0_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{
    static hwaddr last_addr = 0;
    static uint64_t last_val = 0;
    static unsigned last_size = 0;
    
    // Skip if same as immediately previous call (KVM MMIO re-execution)
    if (addr == last_addr && val == last_val && size == last_size) {
        return;
    }
    last_addr = addr;
    last_val = val;
    last_size = size;
    
    TenstorrentState *tt = opaque;

    // Handle TLB configuration register writes
    if (addr >= TLB_REGS_START && addr < TLB_REGS_START + TLB_REGS_LEN) {
        tenstorrent_tlb_config_write(tt, addr, val, size);
        return;
    }
    else if (addr >= NOC2AXI_CFG_START && addr < NOC2AXI_CFG_START + NOC2AXI_CFG_LEN) {
        hwaddr offset = addr - NOC2AXI_CFG_START;
        if (offset < NOC2AXI_CFG_SIZE) {
            switch (offset) {
                case 0x00:
                    if (val & 0x2) {
                        tt->status = 0x1;
                    } else if (val & 0x1) {
                        tt->status |= 0x2;
                    }
                    break;
                default:
                    memcpy(&tt->noc2axi_cfg[offset], &val, size);
                    break;
            }
        }
    }
    else if (addr > KERNEL_TLB_START && addr < KERNEL_TLB_START + KERNEL_TLB_LEN) {
        // TLB-mapped CSM window access - convert to CSM physical address
        hwaddr csm_offset = addr - KERNEL_TLB_START;
        tenstorrent_csm_write(opaque, csm_offset, val, size);
    }
    else if (addr >= ARC_APB_BAR0_START && addr < ARC_APB_BAR0_START + ARC_APB_BAR0_LEN) {
        // ARC APB AXI path - maps to same registers as CSM
        hwaddr arc_offset = addr - ARC_APB_BAR0_START;
        tenstorrent_csm_write(opaque, arc_offset, val, size);
    }
    else if(addr <= TLB_2M_WINDOWS_END) {
        return tenstorrent_tlb_2m_write(tt, addr, val, size);
    }
}

static const MemoryRegionOps tenstorrent_bar0_ops = {
    .read = tenstorrent_bar0_read,
    .write = tenstorrent_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void proc_arc_msg(TenstorrentState *tt) {
    uint8_t* arc_buf = tt->node_mem_8m[ARC_Y][ARC_X];
    uint32_t msg_queue_size = 2 * NUM_ENTRIES_PER_QUEUE * ARC_QUEUE_ENTRY_SIZE + ARC_MSG_QUEUE_HEADER_SIZE;
    uint32_t msg_queue_base = ARC_MSG_QUEUE_BASE + APPLICATION * msg_queue_size;

    // Read current res_wptr
    uint32_t *res_wptr = (uint32_t *)&arc_buf[ARC_MSG_QUEUE_RES_WPTR(msg_queue_base)];
    // double-wrapping: pointer wraps at 2 * size, consistent with UMD pop_response
    uint32_t new_res_wptr = (*res_wptr + 1) % (2 * NUM_ENTRIES_PER_QUEUE);
    *res_wptr = new_res_wptr;

    // Write success response to the corresponding response slot
    // Response area follows the request area: header(8 words) + request(size * 8 words) + response
    // UMD: response_entry_offset = header_len + (size + rptr % size) * entry_len  (in words)
    // Convert to byte offset: msg_queue_base + (8 + (NUM_ENTRIES_PER_QUEUE + slot) * 8) * 4
    uint32_t response_slot = new_res_wptr % NUM_ENTRIES_PER_QUEUE;
    uint32_t response_offset = msg_queue_base +
        (8 + (NUM_ENTRIES_PER_QUEUE + response_slot) * 8) * 4;
    uint32_t success_header = 0;
    memcpy(&arc_buf[response_offset], &success_header, 4);
}

static uint64_t arc_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void arc_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size) {
    static int trig_cnt;
    ++trig_cnt;
    trig_cnt &= 1;
    if(!trig_cnt)
        return;

    TenstorrentState *tt = opaque;
    proc_arc_msg(tt);
}


static const MemoryRegionOps arc_ops = {
    .read = arc_read,
    .write = arc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void tenstorrent_realize(PCIDevice *dev, Error **errp)
{
    TenstorrentState *tt = TENSTORRENT(dev);
    uint8_t *pci_conf = dev->config;

    // Set up PCI configuration space
    pci_config_set_vendor_id(pci_conf, TENSTORRENT_VENDOR_ID);
    pci_config_set_device_id(pci_conf, TENSTORRENT_DEVICE_ID);
    pci_config_set_revision(pci_conf, 0x01);
    pci_config_set_class(pci_conf, PCI_CLASS_OTHERS);
    
    // Enable Memory Space and Bus Master
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    
    // Set subsystem vendor ID and device ID
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, TENSTORRENT_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, TENSTORRENT_DEVICE_ID);
    
    // Initialize BAR0 - main MMIO space
    memory_region_init_io(&tt->bar0_mmio, OBJECT(tt), &tenstorrent_bar0_ops, tt,
                          "tenstorrent-bar0", BAR0_SIZE);
    pci_register_bar(dev, 0, 
        PCI_BASE_ADDRESS_SPACE_MEMORY | 
        PCI_BASE_ADDRESS_MEM_TYPE_64 | 
        PCI_BASE_ADDRESS_MEM_PREFETCH,
        &tt->bar0_mmio);
    
    // Initialize BAR2 - IATU configuration space
    memory_region_init_io(&tt->bar2_mmio, OBJECT(tt), &tenstorrent_bar2_ops, tt,
                          "tenstorrent-bar2", BAR2_SIZE);
    pci_register_bar(dev, 2, 
        PCI_BASE_ADDRESS_SPACE_MEMORY,
        &tt->bar2_mmio);
    
    // Initialize BAR4 - 4G TLB window space
    memory_region_init_io(&tt->bar4_mmio, OBJECT(tt), &tenstorrent_bar4_ops, tt,
                          "tenstorrent-bar4", BAR4_SIZE);
    pci_register_bar(dev, 4, 
        PCI_BASE_ADDRESS_SPACE_MEMORY | 
        PCI_BASE_ADDRESS_MEM_TYPE_64 | 
        PCI_BASE_ADDRESS_MEM_PREFETCH,
        &tt->bar4_mmio);
        
    memory_region_init_io(&tt->arc_mmio, OBJECT(tt), &arc_ops, tt, "arc-io", 4);

    // MSI initialization
    int ret = msi_init(dev, 0, 1, true, false, errp);
    if (ret < 0) {
        pci_config_set_interrupt_pin(pci_conf, 1);
        tt->msi_enabled = false;
        tt->msix_enabled = false;
        warn_report("Tenstorrent: MSI initialization failed, using legacy interrupts");
    } else {
        tt->msi_enabled = true;
        tt->msix_enabled = false;
    }

    // PCIe endpoint initialization
    dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    int pcie_cap_offset = pcie_endpoint_cap_init(dev, TENSTORRENT_PCIE_CAP_OFFSET);
    if (pcie_cap_offset < 0) {
        error_setg(errp, "Failed to initialize PCIe endpoint capability");
        return;
    }

    // Set PCIe device control register
    pci_set_word(pci_conf + pcie_cap_offset + PCI_EXP_DEVCTL,
                PCI_EXP_DEVCTL_CERE |  
                PCI_EXP_DEVCTL_NFERE | 
                PCI_EXP_DEVCTL_FERE |  
                PCI_EXP_DEVCTL_URRE);

    // Initialize device state
    tt->status = 0x1;
    //memset(tt->tlb_regs, 0, sizeof(tt->tlb_regs));
    memset(tt->noc2axi_cfg, 0, sizeof(tt->noc2axi_cfg));
    memset(tt->bar2_regs, 0, sizeof(tt->bar2_regs));
    
    // init arc buffer
    memset(tt->csm_regs, 0, sizeof(tt->csm_regs));
    
    // Initialize TLB configuration arrays
    memset(tt->tlb_2m_configs, 0, sizeof(tt->tlb_2m_configs));
    memset(tt->tlb_4g_configs, 0, sizeof(tt->tlb_4g_configs));
    memset(tt->tlb_configured, 0, sizeof(tt->tlb_configured));
    
    // Pre-populate ARC message queue data
    uint32_t queue_base = ARC_CSM_BASE + ARC_MSG_QUEUE_BASE;
    memcpy(&tt->csm_regs[ARC_MSG_QCB_OFFSET], &queue_base, 4);
    
    uint32_t queue_info = NUM_ENTRIES_PER_QUEUE;
    memcpy(&tt->csm_regs[ARC_MSG_QCB_OFFSET + 4], &queue_info, 4);
    
    // Initialize message queue header
    uint32_t queue_header_offset = ARC_MSG_QUEUE_BASE;
    uint32_t req_wptr = 0, req_rptr = 0, res_wptr = 0, res_rptr = 0;
    memcpy(&tt->csm_regs[ARC_MSG_QUEUE_REQ_WPTR(queue_header_offset)], &req_wptr, 4);
    memcpy(&tt->csm_regs[ARC_MSG_QUEUE_RES_RPTR(queue_header_offset)], &res_rptr, 4);
    memcpy(&tt->csm_regs[ARC_MSG_QUEUE_REQ_RPTR(queue_header_offset)], &req_rptr, 4);
    memcpy(&tt->csm_regs[ARC_MSG_QUEUE_RES_WPTR(queue_header_offset)], &res_wptr, 4);
    
    // init node mem:
    memset(tt->node_mem_8m, 0, sizeof(tt->node_mem_8m));
    
    // init node core:
    memset(tt->node_core, 0, sizeof(tt->node_core));

    // alloc dram:
    memset(tt->dram_bank, 0, sizeof(tt->dram_bank));
    
    // Initialize coroutine scheduler
    tt->scheduler = scheduler_create(NUM_SCHEDULER_THREADS);
    tt->total_coroutines = 0;
    tt->total_instructions = 0;
    
    if (!tt->scheduler) {
        error_setg(errp, "Failed to create coroutine scheduler");
        return;
    }
    
    // Start scheduler
    scheduler_start(tt->scheduler);
 }

static void tenstorrent_exit(PCIDevice *dev)
{
    TenstorrentState *tt = TENSTORRENT(dev);

    // Stop coroutine scheduler
    if (tt->scheduler) {
        scheduler_stop(tt->scheduler);
        scheduler_destroy(tt->scheduler);
        tt->scheduler = NULL;
    }
    
    if (tt->msi_enabled) {
        msi_uninit(dev);
    }
    if (tt->msix_enabled) {
        msix_uninit(dev, &tt->bar0_mmio, &tt->bar0_mmio);
    }
}

static const char *core_type_name[] = {"B", "N", "T0", "T1", "T2"};

void dump_tensix_node(TenstorrentState *tt, int x, int y) {
    if (get_node_type(x, y) != TENSIX) {
        printf("(%d,%d) is not a tensix node\n", x, y);
        return;
    }
    TensixNodeState *node = &tt->node_core[y][x].tensix;
    printf("=== Tensix (%d,%d) ===\n", x, y);
    for (int i = 0; i < NUM_CORES; i++) {
        Rv32Core *core = &node->tensix_cores[i];
        if (core->rv32cpu) {
            printf("  %s: pc=0x%x halted=%d\n",
                   core_type_name[i],
                   rv32_get_pc(core->rv32cpu),
                   rv32_has_halted(core->rv32cpu));
        } else {
            printf("  %s: not started\n", core_type_name[i]);
        }
    }
}

static void release_resources(TenstorrentState *tt) {
    // Stop all RV32 cores
    int stopped_tensix = 0, stopped_eth = 0;
    for (int y = 0; y < BH_GRID_Y; ++y) {
        for (int x = 0; x < BH_GRID_X; ++x) {
            if (get_node_type(x, y) == ETH) {
                EthNodeState *eth = &tt->node_core[y][x].eth;
                if (eth->cores[ETH_DM0].rv32cpu || eth->cores[ETH_DM1].rv32cpu) {
                    stop_eth_node(eth);
                    stopped_eth++;
                }
            } else if (get_node_type(x, y) == TENSIX) {
                Rv32Core *cores = tt->node_core[y][x].tensix.tensix_cores;
                if (cores[B_CORE].rv32cpu) {
                    stop_tensix_node(x, y, cores);
                    stopped_tensix++;
                }
            }
        }
    }
    if (stopped_tensix || stopped_eth)
        TT_LOG(LOG_LEVEL_INFO, "release_resources: stopped %d tensix nodes, %d eth nodes\n",
               stopped_tensix, stopped_eth);

    // Stop and recreate scheduler so threads don't spin idle
    if (tt->scheduler) {
        scheduler_stop(tt->scheduler);
        scheduler_destroy(tt->scheduler);
        tt->scheduler = scheduler_create(NUM_SCHEDULER_THREADS);
        scheduler_start(tt->scheduler);
        tt->total_coroutines = 0;
    }

    for (int i = 0; i < TLB_2M_WINDOW_COUNT; ++i) {
        MemoryRegion *mr = &tt->tlb_2m_region[i];
        if (memory_region_is_mapped(mr))
            memory_region_del_subregion(&tt->bar0_mmio, mr);
    }
    for (int y = 0; y < BH_GRID_Y; ++y) {
        for (int x = 0; x < BH_GRID_X; ++x) {
            uint8_t *p = tt->node_mem_8m[y][x];
            if(p) {
                qemu_vfree(p);
                tt->node_mem_8m[y][x] = NULL;
            }
        }
    }
}

//static Property tenstorrent_properties[] = {
//};

static void tenstorrent_reset(Object *obj, ResetType rt) {
    TenstorrentState *tt = TENSTORRENT(obj);
    release_resources(tt);
}

static void tenstorrent_pci_config_write(PCIDevice *dev,
                                         uint32_t addr,
                                         uint32_t val,
                                         int len)
{
    TenstorrentState *tt = TENSTORRENT(dev);
    uint32_t old = pci_get_long(dev->config + addr);

    // Execute default write first
    pci_default_write_config(dev, addr, val, len);

    // Print all configuration writes
    TT_LOG(LOG_LEVEL_VERBOSE, "[TENSTORRENT] config_write: addr=0x%x, val=0x%x, len=%d\n", addr, val, len);

    // Detect the PARITY bit set by set_reset_marker
    if (addr == PCI_COMMAND) {
        uint16_t new_cmd = pci_get_word(dev->config + PCI_COMMAND);
        uint16_t old_cmd = pci_get_word((uint8_t*)&old);

        if ((new_cmd & PCI_COMMAND_PARITY) && !(old_cmd & PCI_COMMAND_PARITY)) {
            TT_LOG(LOG_LEVEL_INFO, "[TENSTORRENT] Reset marker SET via PCI_COMMAND_PARITY\n");
        }

        // Detect INTX_DISABLE rising edge
        if ((new_cmd & PCI_COMMAND_INTX_DISABLE) && !(old_cmd & PCI_COMMAND_INTX_DISABLE)) {
            TT_LOG(LOG_LEVEL_INFO, "[TENSTORRENT] INTX_DISABLE pulse -> Resetting!\n");
            release_resources(tt);
        }
    }
    // Detect vendor-specific timer register write
    else if (addr == 0x930) {  // INTERFACE_TIMER_CONTROL_OFF
        if ((val & 0x1) && (val & 0x10)) {  // INTERFACE_TIMER_EN & INTERFACE_FORCE_PENDING
            TT_LOG(LOG_LEVEL_INFO, "[TENSTORRENT] Timer interrupt register written -> ASIC reset! val=0x%x\n", val);
            
            release_resources(tt);
        }
    }
    else if (addr == 0x934) {  // INTERFACE_TIMER_TARGET_OFF
        TT_LOG(LOG_LEVEL_VERBOSE, "  Timer target set: 0x%x\n", val);
    }
}

static uint32_t tenstorrent_pci_config_read(PCIDevice *pci_dev,
                                     uint32_t addr, int len){
    return pci_default_read_config(pci_dev, addr, len);
}

static void tenstorrent_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = tenstorrent_realize;
    k->exit = tenstorrent_exit;
    k->vendor_id = TENSTORRENT_VENDOR_ID;
    k->device_id = TENSTORRENT_DEVICE_ID;
    k->class_id = PCI_CLASS_OTHERS;
    
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.hold = tenstorrent_reset;
    
    k->config_write = tenstorrent_pci_config_write;
    k->config_read = tenstorrent_pci_config_read;

    dc->user_creatable = true;
    //device_class_set_props(dc, tenstorrent_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo tenstorrent_info = {
    .name = TYPE_TENSTORRENT,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(TenstorrentState),
    .class_init = tenstorrent_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { TYPE_RESETTABLE_INTERFACE },
        { },
    },
};

static void tenstorrent_register_types(void)
{
    type_register_static(&tenstorrent_info);
}

type_init(tenstorrent_register_types)
