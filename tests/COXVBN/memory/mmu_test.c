#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "mmu.h"

#define BLOCK UINT64_C(0x200000)
#define BASE  UINT64_C(0x40000000)
#define VALID UINT64_C(1)
#define ADDRESS_MASK UINT64_C(0x0000ffffffe00000)

int main(void)
{
    uint64_t l2[4] = {0};
    l2[0] = BASE | VALID;
    l2[1] = (BASE + BLOCK) | VALID;
    l2[3] = (BASE + 3U * BLOCK) | VALID;

    assert(coxvbn_mmu_range_mapped(l2, 4U, BASE, BASE + 1U));
    assert(coxvbn_mmu_range_mapped(l2, 4U, BASE + 17U,
                                   BASE + BLOCK + 9U));
    assert(!coxvbn_mmu_range_mapped(l2, 4U, BASE + 2U * BLOCK,
                                    BASE + 3U * BLOCK));
    assert(!coxvbn_mmu_range_mapped(l2, 4U, BASE + 3U * BLOCK,
                                    BASE + 4U * BLOCK + 1U));
    assert(!coxvbn_mmu_range_mapped(l2, 4U, BASE + 2U * BLOCK,
                                    BASE + 2U * BLOCK));
    l2[2] = (BASE + 2U * BLOCK) | VALID;
    assert(coxvbn_mmu_range_mapped(l2, 4U, BASE + BLOCK,
                                   BASE + 3U * BLOCK));
    l2[2] = (BASE + 3U * BLOCK) | VALID;
    assert(!coxvbn_mmu_range_mapped(l2, 4U, BASE + 2U * BLOCK,
                                    BASE + 3U * BLOCK));
    l2[2] = (BASE + 2U * BLOCK);
    assert(!coxvbn_mmu_range_mapped(l2, 4U, BASE + 2U * BLOCK,
                                    BASE + 3U * BLOCK));
    puts("COXVBN MMU mapping coverage tests: PASS");
    return 0;
}
