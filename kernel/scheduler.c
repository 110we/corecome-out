#include "scheduler.h"
#include "exception.h"

#include <limits.h>

static int task_ref_valid(const struct coxvbn_scheduler *scheduler,
                          const struct coxvbn_task_ref *ref)
{
    const struct coxvbn_task_record *task;
    if (scheduler == 0 || ref == 0 ||
        ref->slot >= COXVBN_TASK_CAPACITY || ref->generation == 0U)
        return 0;
    task = &scheduler->tasks[ref->slot];
    return task->state != COXVBN_TASK_UNUSED &&
           task->ref.generation == ref->generation;
}

static struct coxvbn_task_record *current_task(
    struct coxvbn_scheduler *scheduler)
{
    struct coxvbn_task_record *task;
    if (scheduler == 0 || scheduler->current_slot < 0 ||
        (uint32_t)scheduler->current_slot >= COXVBN_TASK_CAPACITY)
        return 0;
    task = &scheduler->tasks[(uint32_t)scheduler->current_slot];
    if (task->state != COXVBN_TASK_RUNNING)
        return 0;
    return task;
}

/* boot.S allocates 304 bytes for each exception frame (288-byte payload). */
#define COXVBN_EXCEPTION_FRAME_BYTES 304U

static int frame_in_task_stack(const struct coxvbn_task_record *task,
                               const struct exception_frame *frame)
{
    uintptr_t base, address, end;
    if (task == 0 || frame == 0)
        return 0;
    base = task->stack_base;
    address = (uintptr_t)frame;
    if (task->stack_size > UINTPTR_MAX - base)
        return 0;
    end = base + task->stack_size;
    if ((address & 0xfU) != 0U || address < base ||
        address > end || end - address < COXVBN_EXCEPTION_FRAME_BYTES)
        return 0;
    return 1;
}

void coxvbn_scheduler_init(struct coxvbn_scheduler *scheduler)
{
    uint32_t i;
    if (scheduler == 0)
        return;
    scheduler->next_generation = 1U;
    scheduler->cursor = 0U;
    scheduler->current_slot = -1;
    scheduler->task_count = 0U;
    for (i = 0U; i < COXVBN_TASK_CAPACITY; ++i) {
        scheduler->tasks[i].ref.slot = i;
        scheduler->tasks[i].ref.generation = 0U;
        scheduler->tasks[i].state = COXVBN_TASK_UNUSED;
        scheduler->tasks[i].owner.module_id = 0U;
        scheduler->tasks[i].owner.generation = 0U;
        scheduler->tasks[i].entry = 0U;
        scheduler->tasks[i].argument = 0U;
        scheduler->tasks[i].stack_base = 0U;
        scheduler->tasks[i].stack_size = 0U;
        scheduler->tasks[i].saved_frame = 0;
        scheduler->tasks[i].exit_code = 0;
    }
}

int coxvbn_task_create(struct coxvbn_scheduler *scheduler,
                       uintptr_t entry, uintptr_t argument,
                       uintptr_t stack_base, size_t stack_size,
                       const struct coxvbn_module_ref *owner,
                       struct coxvbn_task_ref *ref_out)
{
    uint32_t i;
    struct coxvbn_task_record *task;
    if (scheduler == 0 || ref_out == 0 || entry == 0U ||
        stack_base == 0U || (stack_base & 0xfU) != 0U ||
        stack_size < COXVBN_TASK_MIN_STACK_SIZE ||
        (stack_size & 0xfU) != 0U ||
        stack_base > UINTPTR_MAX - stack_size ||
        scheduler->next_generation == 0U ||
        scheduler->next_generation == UINT32_MAX)
        return -1;
    if (owner != 0 &&
        (owner->module_id == 0U || owner->generation == 0U))
        return -1;
    if (scheduler->task_count >= COXVBN_TASK_CAPACITY)
        return -2;
    for (i = 0U; i < COXVBN_TASK_CAPACITY; ++i)
        if (scheduler->tasks[i].state == COXVBN_TASK_UNUSED)
            break;
    if (i == COXVBN_TASK_CAPACITY)
        return -2;

    task = &scheduler->tasks[i];
    task->ref.slot = i;
    task->ref.generation = scheduler->next_generation++;
    task->entry = entry;
    task->argument = argument;
    task->stack_base = stack_base;
    task->stack_size = stack_size;
    task->saved_frame = 0;
    task->exit_code = 0;
    task->owner.module_id = owner == 0 ? 0U : owner->module_id;
    task->owner.generation = owner == 0 ? 0U : owner->generation;
    task->state = COXVBN_TASK_READY;
    ++scheduler->task_count;
    *ref_out = task->ref;
    return 0;
}

int coxvbn_task_set_initial_frame(struct coxvbn_scheduler *scheduler,
                                  const struct coxvbn_task_ref *ref,
                                  struct exception_frame *frame)
{
    struct coxvbn_task_record *task;
    if (!task_ref_valid(scheduler, ref) || frame == 0 ||
        ((uintptr_t)frame & 0xfU) != 0U)
        return -1;
    task = &scheduler->tasks[ref->slot];
    if (task->state != COXVBN_TASK_READY || task->saved_frame != 0)
        return -2;
    if (!frame_in_task_stack(task, frame))
        return -3;
    task->saved_frame = frame;
    return 0;
}

int coxvbn_scheduler_pick_next(struct coxvbn_scheduler *scheduler,
                               struct coxvbn_task_ref *ref_out)
{
    uint32_t offset;
    if (scheduler == 0 || ref_out == 0 || scheduler->current_slot >= 0)
        return -1;
    for (offset = 0U; offset < COXVBN_TASK_CAPACITY; ++offset) {
        uint32_t slot = (scheduler->cursor + offset) % COXVBN_TASK_CAPACITY;
        struct coxvbn_task_record *task = &scheduler->tasks[slot];
        if (task->state != COXVBN_TASK_READY)
            continue;
        task->state = COXVBN_TASK_RUNNING;
        scheduler->current_slot = (int32_t)slot;
        scheduler->cursor = (slot + 1U) % COXVBN_TASK_CAPACITY;
        *ref_out = task->ref;
        return 0;
    }
    return -2;
}

struct exception_frame *coxvbn_scheduler_schedule_frame(
    struct coxvbn_scheduler *scheduler,
    struct exception_frame *current_frame)
{
    uint32_t offset;

    if (scheduler == 0)
        return 0;

    if (scheduler->current_slot >= 0) {
        struct coxvbn_task_record *current = current_task(scheduler);
        if (current == 0 || !frame_in_task_stack(current, current_frame))
            return 0;
        current->saved_frame = current_frame;
        current->state = COXVBN_TASK_READY;
        scheduler->current_slot = -1;
    } else if (current_frame != 0) {
        return 0;
    }

    for (offset = 0U; offset < COXVBN_TASK_CAPACITY; ++offset) {
        uint32_t slot = (scheduler->cursor + offset) % COXVBN_TASK_CAPACITY;
        struct coxvbn_task_record *task = &scheduler->tasks[slot];
        if (task->state != COXVBN_TASK_READY || task->saved_frame == 0)
            continue;
        task->state = COXVBN_TASK_RUNNING;
        scheduler->current_slot = (int32_t)slot;
        scheduler->cursor = (slot + 1U) % COXVBN_TASK_CAPACITY;
        return task->saved_frame;
    }
    return 0;
}

struct exception_frame *coxvbn_scheduler_exit_frame(
    struct coxvbn_scheduler *scheduler,
    struct exception_frame *current_frame,
    int exit_code)
{
    struct coxvbn_task_record *current;
    uint32_t offset;

    if (scheduler == 0)
        return 0;
    current = current_task(scheduler);
    if (current == 0 || !frame_in_task_stack(current, current_frame))
        return 0;

    /* A retired task must never be selected again with its dead stack frame. */
    current->saved_frame = 0;
    current->exit_code = exit_code;
    current->state = COXVBN_TASK_ZOMBIE;
    scheduler->current_slot = -1;

    for (offset = 0U; offset < COXVBN_TASK_CAPACITY; ++offset) {
        uint32_t slot = (scheduler->cursor + offset) % COXVBN_TASK_CAPACITY;
        struct coxvbn_task_record *task = &scheduler->tasks[slot];
        if (task->state != COXVBN_TASK_READY || task->saved_frame == 0)
            continue;
        task->state = COXVBN_TASK_RUNNING;
        scheduler->current_slot = (int32_t)slot;
        scheduler->cursor = (slot + 1U) % COXVBN_TASK_CAPACITY;
        return task->saved_frame;
    }
    return 0;
}

int coxvbn_task_yield(struct coxvbn_scheduler *scheduler)
{
    struct coxvbn_task_record *task = current_task(scheduler);
    if (task == 0)
        return -1;
    task->state = COXVBN_TASK_READY;
    scheduler->current_slot = -1;
    return 0;
}

int coxvbn_task_block(struct coxvbn_scheduler *scheduler)
{
    struct coxvbn_task_record *task = current_task(scheduler);
    if (task == 0)
        return -1;
    task->state = COXVBN_TASK_BLOCKED;
    scheduler->current_slot = -1;
    return 0;
}

int coxvbn_task_wake(struct coxvbn_scheduler *scheduler,
                     const struct coxvbn_task_ref *ref)
{
    struct coxvbn_task_record *task;
    if (!task_ref_valid(scheduler, ref))
        return -1;
    task = &scheduler->tasks[ref->slot];
    if (task->state != COXVBN_TASK_BLOCKED)
        return -2;
    task->state = COXVBN_TASK_READY;
    return 0;
}

int coxvbn_task_exit(struct coxvbn_scheduler *scheduler, int exit_code)
{
    struct coxvbn_task_record *task = current_task(scheduler);
    if (task == 0)
        return -1;
    task->exit_code = exit_code;
    task->state = COXVBN_TASK_ZOMBIE;
    scheduler->current_slot = -1;
    return 0;
}

int coxvbn_task_reap(struct coxvbn_scheduler *scheduler,
                     const struct coxvbn_task_ref *ref)
{
    struct coxvbn_task_record *task;
    if (!task_ref_valid(scheduler, ref))
        return -1;
    task = &scheduler->tasks[ref->slot];
    if (task->state != COXVBN_TASK_ZOMBIE)
        return -2;
    task->state = COXVBN_TASK_UNUSED;
    task->entry = 0U;
    task->argument = 0U;
    task->stack_base = 0U;
    task->stack_size = 0U;
    task->saved_frame = 0;
    task->owner.module_id = 0U;
    task->owner.generation = 0U;
    task->exit_code = 0;
    --scheduler->task_count;
    return 0;
}

int coxvbn_task_get_state(const struct coxvbn_scheduler *scheduler,
                          const struct coxvbn_task_ref *ref,
                          enum coxvbn_task_state *state_out,
                          int *exit_code_out)
{
    const struct coxvbn_task_record *task;
    if (state_out == 0 || !task_ref_valid(scheduler, ref))
        return -1;
    task = &scheduler->tasks[ref->slot];
    *state_out = task->state;
    if (exit_code_out != 0)
        *exit_code_out = task->exit_code;
    return 0;
}
