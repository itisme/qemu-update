// coroutine_lib.h - C interface for calling from C code (improved version)
#ifndef COROUTINE_LIB_H
#define COROUTINE_LIB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Coroutine handle
typedef void* coroutine_t;

// coroutine_lib.h - Simplified interface
typedef void* queue_handle_t;  // Queue handle type

// Coroutine function type
typedef void (*coroutine_func_t)(void* arg1, void* arg2);

// Create a coroutine
coroutine_t coroutine_create(coroutine_func_t func, void* arg1, void* arg2);

// Resume coroutine execution
int coroutine_resume(coroutine_t coroutine);

// Check if coroutine is finished
int coroutine_done(coroutine_t coroutine);

// Destroy a coroutine
void coroutine_destroy(coroutine_t coroutine);

// Synchronously destroy a coroutine
void coroutine_destroy_sync(coroutine_t coroutine);

// Get the current coroutine
coroutine_t coroutine_current(void);

// Yield the current coroutine
void coroutine_yield(void);

void coroutine_yield_with_handle(coroutine_t coroutine);

// Coroutine scheduler
typedef void* scheduler_t;

// Create a scheduler
scheduler_t scheduler_create(int num_threads);

// Add a coroutine to the scheduler
queue_handle_t scheduler_add_coroutine(scheduler_t scheduler, coroutine_t coroutine, queue_handle_t addqueue);

// Start the scheduler
void scheduler_start(scheduler_t scheduler);

// Stop the scheduler
void scheduler_stop(scheduler_t scheduler);

// Destroy the scheduler
void scheduler_destroy(scheduler_t scheduler);

// Get scheduler statistics
void scheduler_get_stats(scheduler_t scheduler, size_t* total_coroutines, size_t* active_threads);

// Reorder queues
void scheduler_reorder_queues(scheduler_t scheduler);

#ifdef __cplusplus
}
#endif

#endif // COROUTINE_LIB_H
