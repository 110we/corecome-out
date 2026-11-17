#include "scheduler.h"
#include "exception.h"

#include <assert.h>
#include <stdint.h>

static void test_frame_selection(void)
{
    static struct coxvbn_scheduler scheduler;
    static uint8_t stacks[2][COXVBN_TASK_MIN_STACK_SIZE]
        __attribute__((aligned(16)));
    struct exception_frame *frames[2];
    struct coxvbn_task_ref first, second;
    struct exception_frame *selected;

    frames[0] = (struct exception_frame *)(void *)
        (stacks[0] + sizeof(stacks[0]) - 304U);
    frames[1] = (struct exception_frame *)(void *)
        (stacks[1] + sizeof(stacks[1]) - 304U);
    coxvbn_scheduler_init(&scheduler);
    assert(coxvbn_task_create(&scheduler, 0x1000U, 11U,
        (uintptr_t)stacks[0], sizeof(stacks[0]), 0, &first) == 0);
    assert(coxvbn_task_create(&scheduler, 0x2000U, 22U,
        (uintptr_t)stacks[1], sizeof(stacks[1]), 0, &second) == 0);

    {
        struct exception_frame outside_stack __attribute__((aligned(16)));
        assert(coxvbn_task_set_initial_frame(&scheduler, &first,
                                             &outside_stack) == -3);
    }
    assert(coxvbn_task_set_initial_frame(&scheduler, &first, frames[0]) == 0);
    assert(coxvbn_task_set_initial_frame(&scheduler, &second, frames[1]) == 0);
    assert(coxvbn_task_set_initial_frame(&scheduler, &second, frames[1]) == -2);

    selected = coxvbn_scheduler_schedule_frame(&scheduler, 0);
    assert(selected == frames[0]);
    frames[0].elr = 0x1110U;
    selected = coxvbn_scheduler_schedule_frame(&scheduler, frames[0]);
    assert(selected == frames[1]);
    frames[1].elr = 0x2220U;
    selected = coxvbn_scheduler_schedule_frame(&scheduler, frames[1]);
    assert(selected == frames[0]);
    assert(frames[0].elr == 0x1110U);
    selected = coxvbn_scheduler_schedule_frame(&scheduler, frames[0]);
    assert(selected == frames[1]);
    assert(frames[1].elr == 0x2220U);
}

int main(void)
{
    static struct coxvbn_scheduler scheduler;
    static uint8_t stacks[3][COXVBN_TASK_MIN_STACK_SIZE]
        __attribute__((aligned(16)));
    struct coxvbn_task_ref a, b, c, picked, stale;
    struct coxvbn_module_ref owner = {7U, 2U};
    enum coxvbn_task_state state;
    int exit_code = 0;

    coxvbn_scheduler_init(&scheduler);
    assert(coxvbn_scheduler_pick_next(&scheduler, &picked) == -2);
    assert(coxvbn_task_create(&scheduler, 0U, 0U,
        (uintptr_t)stacks[0], sizeof(stacks[0]), 0, &a) == -1);
    assert(coxvbn_task_create(&scheduler, 0x1000U, 0U,
        (uintptr_t)stacks[0], sizeof(stacks[0]), 0, &a) == 0);
    assert(coxvbn_task_create(&scheduler, 0x2000U, 1U,
        (uintptr_t)stacks[1], sizeof(stacks[1]), &owner, &b) == 0);
    assert(coxvbn_task_create(&scheduler, 0x3000U, 2U,
        (uintptr_t)stacks[2], sizeof(stacks[2]), 0, &c) == 0);
    assert(scheduler.task_count == 3U);

    assert(coxvbn_scheduler_pick_next(&scheduler, &picked) == 0);
    assert(picked.slot == a.slot);
    assert(coxvbn_scheduler_pick_next(&scheduler, &picked) == -1);
    assert(coxvbn_task_yield(&scheduler) == 0);
    assert(coxvbn_scheduler_pick_next(&scheduler, &picked) == 0);
    assert(picked.slot == b.slot);
    assert(coxvbn_task_block(&scheduler) == 0);
    assert(coxvbn_task_get_state(&scheduler, &b, &state, 0) == 0);
    assert(state == COXVBN_TASK_BLOCKED);
    assert(coxvbn_task_wake(&scheduler, &b) == 0);
    assert(coxvbn_task_wake(&scheduler, &b) == -2);

    assert(coxvbn_scheduler_pick_next(&scheduler, &picked) == 0);
    assert(picked.slot == c.slot);
    assert(coxvbn_task_exit(&scheduler, 42) == 0);
    assert(coxvbn_task_get_state(&scheduler, &c, &state, &exit_code) == 0);
    assert(state == COXVBN_TASK_ZOMBIE && exit_code == 42);
    stale = c;
    assert(coxvbn_task_reap(&scheduler, &c) == 0);
    assert(coxvbn_task_get_state(&scheduler, &stale, &state, 0) == -1);
    assert(scheduler.task_count == 2U);

    assert(coxvbn_scheduler_pick_next(&scheduler, &picked) == 0);
    assert(picked.slot == a.slot);
    assert(coxvbn_task_exit(&scheduler, 0) == 0);
    assert(coxvbn_task_reap(&scheduler, &a) == 0);
    assert(coxvbn_scheduler_pick_next(&scheduler, &picked) == 0);
    assert(picked.slot == b.slot);
    assert(coxvbn_task_exit(&scheduler, 0) == 0);
    assert(coxvbn_task_reap(&scheduler, &b) == 0);
    assert(coxvbn_scheduler_pick_next(&scheduler, &picked) == -2);
    assert(scheduler.task_count == 0U);

    test_frame_selection();
    return 0;
}
