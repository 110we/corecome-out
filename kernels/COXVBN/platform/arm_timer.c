#include "arm_timer.h"

#define CNTP_CTL_ENABLE (1U << 0)
#define CNTP_CTL_IMASK  (1U << 1)
#define CNTP_MAX_TVAL   0x7fffffffU
#define NANOSECONDS_PER_SECOND 1000000000ULL

static uint64_t timer_frequency;

static void timer_isb(void)
{
#if defined(__aarch64__)
    __asm__ volatile("isb" ::: "memory");
#endif
}

static void write_cntp_ctl(uint32_t value)
{
#if defined(__aarch64__)
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)value) : "memory");
#else
    (void)value;
#endif
}

static void write_cntp_tval(uint32_t value)
{
#if defined(__aarch64__)
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"((uint64_t)value) : "memory");
#else
    (void)value;
#endif
}

int arm_timer_ticks_from_ns(uint64_t frequency, uint64_t duration_ns,
                            uint32_t *ticks)
{
    uint64_t product;
    uint64_t rounded;

    if (ticks == 0 || frequency == 0U || duration_ns == 0U)
        return ARM_TIMER_ERR_ARGUMENT;
    if (frequency > UINT64_MAX / duration_ns)
        return ARM_TIMER_ERR_RANGE;

    product = frequency * duration_ns;
    rounded = product / NANOSECONDS_PER_SECOND;
    if ((product % NANOSECONDS_PER_SECOND) != 0U)
        ++rounded;
    if (rounded == 0U)
        rounded = 1U;
    if (rounded > CNTP_MAX_TVAL)
        return ARM_TIMER_ERR_RANGE;

    *ticks = (uint32_t)rounded;
    return ARM_TIMER_OK;
}

int arm_timer_init(void)
{
    uint64_t frequency = 0U;

#if defined(__aarch64__)
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
#else
    return ARM_TIMER_ERR_UNAVAILABLE;
#endif

    if (frequency == 0U)
        return ARM_TIMER_ERR_UNAVAILABLE;

    write_cntp_ctl(0U);
    timer_isb();
    write_cntp_ctl(CNTP_CTL_IMASK);
    timer_isb();
    timer_frequency = frequency;
    return ARM_TIMER_OK;
}

uint64_t arm_timer_frequency(void)
{
    return timer_frequency;
}

uint64_t arm_timer_now(void)
{
#if defined(__aarch64__)
    uint64_t counter;
    __asm__ volatile("isb\n\tmrs %0, cntpct_el0"
                     : "=r"(counter) :: "memory");
    return counter;
#else
    return 0U;
#endif
}

int arm_timer_disable(void)
{
    if (timer_frequency == 0U)
        return ARM_TIMER_ERR_UNAVAILABLE;
    write_cntp_ctl(0U);
    timer_isb();
    return ARM_TIMER_OK;
}

int arm_timer_set_oneshot_ns(uint64_t duration_ns, uint32_t *programmed_ticks)
{
    uint32_t ticks;
    int rc;

    if (timer_frequency == 0U)
        return ARM_TIMER_ERR_UNAVAILABLE;
    if (programmed_ticks == 0)
        return ARM_TIMER_ERR_ARGUMENT;

    rc = arm_timer_ticks_from_ns(timer_frequency, duration_ns, &ticks);
    if (rc != ARM_TIMER_OK)
        return rc;

    /* Keep the interrupt masked while enabling the physical counter. */
    write_cntp_ctl(CNTP_CTL_IMASK);
    write_cntp_tval(ticks);
    timer_isb();
    write_cntp_ctl(CNTP_CTL_ENABLE | CNTP_CTL_IMASK);
    timer_isb();

    *programmed_ticks = ticks;
    return ARM_TIMER_OK;
}
