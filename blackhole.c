#include "qemu/osdep.h"
#include "blackhole.h"

#define MAJOR_VER 0x01
#define MINOR_VER 0x00

const uint32_t telemetry_ptr[] = { MAJOR_VER << 16 | MINOR_VER, TELEMETRY_NUM, 0 << 16 | BOARD_ID_HIGH, 1 << 16 | BOARD_ID_LOW, 2 << 16 | VCORE, 
	3 << 16 | TDP, 4 << 16 | TDC, 5 << 16 | ASIC_TEMPERATURE, 6 << 16 | BOARD_TEMPERATURE, 7 << 16 | AICLK, 8 << 16 | AXICLK,
	9 << 16 | ARCCLK, 10 << 16 | DDR_STATUS, 11 << 16 | FLASH_BUNDLE_VERSION, 12 << 16 | AICLK_LIMIT_MAX };
	
const uint32_t telemetry_data[] = { 0x36 << 4, 0, 844, 60, 0, 64, 64, 1350, 1000, 1000, 0x55555555, 18 << 24 | 9 << 16, 1350 };

boot_results_t eth_boot_results = {
    .eth_status = {
        .port_status = PORT_UP
    }
};


#define BH_GRID_X 17
#define BH_GRID_Y 12

static const bool noc_translation_enable = true;

static const size_t eth_translated_coordinate_start_x = 20;
static const size_t eth_translated_coordinate_start_y = 25;

static const size_t pcie_translated_coordinate_start_x = 19;
static const size_t pcie_translated_coordinate_start_y = 24;

static const size_t dram_translated_coordinate_start_x = 17;
static const size_t dram_translated_coordinate_start_y = 12;

bool is_noc_translation_enabled(void) {
    return noc_translation_enable;
}

static const int eth_log_x[] = {1, 16, 2, 15, 3, 14, 4, 13, 5, 12, 6, 11, 7,  10};
	
#define NUMOF(it) (sizeof(it)/sizeof(it[0]))

static const int dram_cores_noc0[][12][2] = {
    {{0, 0},  {0, 1}, {0, 11},
     {0, 2}, {0, 10},  {0, 3},
     {0, 9},  {0, 4},  {0, 8},
     {0, 5},  {0, 7},  {0, 6}},
    {{9, 0},  {9, 1}, {9, 11},
     {9, 2}, {9, 10},  {9, 3},
     {9, 9},  {9, 4},  {9, 8},
     {9, 5},  {9, 7},  {9, 6}}};
     
static const int dram_n2b[][12] = {
   {0, 0, 1, 1, 2, 3, 3, 3, 2, 2, 1, 0},
   {4, 4, 5, 5, 6, 7, 7, 7, 6, 6, 5, 4}
};

static int eth_virt2log_x(int log) {
    int i = log - eth_translated_coordinate_start_x;
    
    if( i >= 0 && i < NUMOF(eth_log_x) )
        return eth_log_x[i];
        
    return -1;
}

static void dram_virt2log(int *x, int *y) {
    const int *noc0_pair = dram_cores_noc0[*x - dram_translated_coordinate_start_x][*y - dram_translated_coordinate_start_y];
    *x = noc0_pair[0];
    *y = noc0_pair[1];
}

void virt2log(int *x, int *y) {
    if(!noc_translation_enable)
        return;
        
    if (*y == eth_translated_coordinate_start_y ){
        *x = eth_virt2log_x(*x);
        *y = 1;
    }
    else if (*x == pcie_translated_coordinate_start_x && *y == pcie_translated_coordinate_start_y) {
        *x = 2;
        *y = 0;
    }
    else if ((*x == dram_translated_coordinate_start_x || *x == dram_translated_coordinate_start_x + 1) 
    		&& *y >= dram_translated_coordinate_start_y && *y < dram_translated_coordinate_start_y + 12) {
        dram_virt2log(x, y);
    }
}

NodeType get_node_type(int x, int y) {
    if(x == 8 && y == 0)
        return ARC;
    else if(x == 0 || x == 9)
        return DRAM;
    else if(y == 1)
        return ETH;
    else if(x==2 && y == 0)
        return PCI;
    else
        return TENSIX;
};

int dram_node2bank(int x, int y) {
    return x == 0 ? dram_n2b[0][y] : dram_n2b[1][y];
}



