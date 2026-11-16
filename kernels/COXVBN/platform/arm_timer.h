#ifndef COXVBN_ARM_TIMER_H
#define COXVBN_ARM_TIMER_H

#include <stdint.h>

#define ARM_TIMER_OK 0
#define ARM_TIMER_ERR_ARGUMENT -1
#define ARM_TIMER_ERR_UNAVAILABLE -2
#define ARM_TIMER_ERR_RANGE -3

/*
 * EL1 physical timer interface. One-shot programming deliberately keeps
 * IMASK set: this stage can measure/program time but cannot deliver IRQs.
 */
int arm_timer_init(void);
uint64_t arm_timer_frequency(void);
uint64_t arm_timer_now(void);
int arm_timer_disable(void);
int arm_timer_set_oneshot_ns(uint64_t duration_ns, uint32_t *programmed_ticks);

/* Pure conversion helper, shared by host tests and the hardware driver. */
int arm_timer_ticks_from_ns(uint64_t frequency, uint64_t duration_ns,
                            uint32_t *ticks);

#endif
