// rv32sim.h
#ifndef RV32SIM_H
#define RV32SIM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* rv32_cpu_t;

// Forward declaration - full definition in tensix.h
// QEMU should not know the internal structure of tensix_t
struct tensix;
typedef struct tensix tensix_t;

// Create a RISC-V 32 virtual CPU
// tensix: Tensix coprocessor instance (NULL for B and NC cores)
// core_id: -1 for B, 0 for T0, 1 for T1, 2 for T2, other for NC
rv32_cpu_t rv32_create(uint8_t *base, int size, uint8_t *mem_local, int local_size,
                       uint32_t start, int cycles, tensix_t *tensix, int core_id);

// Run at full speed until exit (illegal instruction or trap)
void rv32_run(rv32_cpu_t cpu);

// Coroutine mode: execute a given number of instructions or until stopped
void rv32_run_co(rv32_cpu_t handle, int max_instructions, void (*yield_cb)(void *, void *), void *cb_arg0, void *cb_arg1);

void rv32_halt(rv32_cpu_t handle);

int rv32_has_halted(rv32_cpu_t handle);

// Destroy the CPU instance
void rv32_destroy(rv32_cpu_t handle);

// ========== Tensix Coprocessor Interface ==========
// Initialize Tensix coprocessor
// tt: pointer to tensix_t structure (should point to ram + 0x180000)
// l1_scratchpad_mem: L1 scratchpad (1.5MB, at ram + 0x00000000)
// high_mem: High address region (6MB, at ram + 0x00200000, maps to 0xFFB00000+)
// t0_ldm: T0 local memory (16KB)
// t1_ldm: T1 local memory (16KB)
// t2_ldm: T2 local memory (16KB)
void tensix_init(tensix_t *tt,
                 uint8_t *l1_scratchpad_mem,
                 uint8_t *high_mem,
                 uint8_t *t0_ldm,
                 uint8_t *t1_ldm,
                 uint8_t *t2_ldm);

#ifdef __cplusplus
}
#endif

#endif // RV32SIM_H
