#include "arm_timer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint32_t ticks = 77U;

    assert(arm_timer_set_oneshot_ns(1000000U, &ticks) ==
           ARM_TIMER_ERR_UNAVAILABLE);
    assert(arm_timer_start_oneshot_ns(1000000U, &ticks) ==
           ARM_TIMER_ERR_UNAVAILABLE);
    assert(arm_timer_ticks_from_ns(0U, 1000000U, &ticks) ==
           ARM_TIMER_ERR_ARGUMENT);
    assert(arm_timer_ticks_from_ns(1000000U, 0U, &ticks) ==
           ARM_TIMER_ERR_ARGUMENT);
    assert(arm_timer_ticks_from_ns(1000000U, 1000000U, 0) ==
           ARM_TIMER_ERR_ARGUMENT);
    assert(ticks == 77U);

    assert(arm_timer_ticks_from_ns(1000000U, 1000000000ULL, &ticks) ==
           ARM_TIMER_OK);
    assert(ticks == 1000000U);
    assert(arm_timer_ticks_from_ns(24000000U, 1U, &ticks) == ARM_TIMER_OK);
    assert(ticks == 1U); /* Round sub-tick durations up. */
    assert(arm_timer_ticks_from_ns(24000000U, 1000000000ULL, &ticks) ==
           ARM_TIMER_OK);
    assert(ticks == 24000000U);
    assert(arm_timer_ticks_from_ns(24000000U, 100000000000ULL, &ticks) ==
           ARM_TIMER_ERR_RANGE);
    assert(arm_timer_ticks_from_ns(UINT64_MAX, UINT64_MAX, &ticks) ==
           ARM_TIMER_ERR_RANGE);

    puts("COXVBN ARM Generic Timer conversion tests: PASS");
    return 0;
}
