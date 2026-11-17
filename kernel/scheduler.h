#ifndef COXVBN_SCHEDULER_H
#define COXVBN_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>
#include "module_registry.h"

#define COXVBN_TASK_CAPACITY 32U
#define COXVBN_TASK_MIN_STACK_SIZE 4096U

enum coxvbn_task_state {
    COXVBN_TASK_UNUSED = 0,
    COXVBN_TASK_READY,
    COXVBN_TASK_RUNNING,
    COXVBN_TASK_BLOCKED,
    COXVBN_TASK_ZOMBIE
};

struct coxvbn_task_ref {
    uint32_t slot;
    uint32_t generation;
};

struct coxvbn_task_record {
    struct coxvbn_task_ref ref;
    struct coxvbn_module_ref owner;
    uintptr_t entry;
    uintptr_t argument;
    uintptr_t stack_base;
    size_t stack_size;
    int exit_code;
    enum coxvbn_task_state state;
};

struct coxvbn_scheduler {
    uint32_t next_generation;
    uint32_t cursor;
    int32_t current_slot;
    uint32_t task_count;
    struct coxvbn_task_record tasks[COXVBN_TASK_CAPACITY];
};

/* Caller must serialize all operations; this model is not IRQ/SMP safe. */
void coxvbn_scheduler_init(struct coxvbn_scheduler *scheduler);

/* owner may be NULL for a kernel-owned task. Entry and stack are validated,
 * but this registry does not yet construct an architectural start frame. */
int coxvbn_task_create(struct coxvbn_scheduler *scheduler,
                       uintptr_t entry, uintptr_t argument,
                       uintptr_t stack_base, size_t stack_size,
                       const struct coxvbn_module_ref *owner,
                       struct coxvbn_task_ref *ref_out);

/* Selects the next READY record in round-robin slot order. This is a
 * bookkeeping primitive only; it does not switch CPU registers or stacks. */
int coxvbn_scheduler_pick_next(struct coxvbn_scheduler *scheduler,
                               struct coxvbn_task_ref *ref_out);
int coxvbn_task_yield(struct coxvbn_scheduler *scheduler);
int coxvbn_task_block(struct coxvbn_scheduler *scheduler);
int coxvbn_task_wake(struct coxvbn_scheduler *scheduler,
                     const struct coxvbn_task_ref *ref);
int coxvbn_task_exit(struct coxvbn_scheduler *scheduler, int exit_code);
int coxvbn_task_reap(struct coxvbn_scheduler *scheduler,
                     const struct coxvbn_task_ref *ref);
int coxvbn_task_get_state(const struct coxvbn_scheduler *scheduler,
                          const struct coxvbn_task_ref *ref,
                          enum coxvbn_task_state *state_out,
                          int *exit_code_out);

#endif
