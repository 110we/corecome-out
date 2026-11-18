#ifndef COXVBN_MMU_H
#define COXVBN_MMU_H

#include "fdt.h"
#include <stddef.h>
#include <stdint.h>

#define COXVBN_MMU_BLOCK_SIZE UINT64_C(0x200000)
#define COXVBN_MMU_RAM_BASE UINT64_C(0x40000000)
#define COXVBN_MMU_RAM_END UINT64_C(0x80000000)
#define COXVBN_MMU_ADDRESS_MASK UINT64_C(0x0000ffffffe00000)

/* Check every 2-MiB block touched by [start, end), not merely its FDT range. */
static inline int coxvbn_mmu_range_mapped(const uint64_t *l2,
                                         size_t entries,
                                         uint64_t start, uint64_t end)
{
    uint64_t block;
    if (l2 == NULL || start >= end || start < COXVBN_MMU_RAM_BASE ||
        end > COXVBN_MMU_RAM_END ||
        entries > (size_t)((UINT64_MAX - COXVBN_MMU_RAM_BASE) /
                           COXVBN_MMU_BLOCK_SIZE) ||
        end > COXVBN_MMU_RAM_BASE +
              (uint64_t)entries * COXVBN_MMU_BLOCK_SIZE)
        return 0;
    block = start & ~(COXVBN_MMU_BLOCK_SIZE - UINT64_C(1));
    while (block < end) {
        uint64_t index = (block - COXVBN_MMU_RAM_BASE) /
                         COXVBN_MMU_BLOCK_SIZE;
        uint64_t descriptor;
        if (index >= entries) return 0;
        descriptor = l2[index];
        if ((descriptor & UINT64_C(1)) == 0U ||
            (descriptor & COXVBN_MMU_ADDRESS_MASK) != block)
            return 0;
        block += COXVBN_MMU_BLOCK_SIZE;
    }
    return 1;
}

/* Enable an identity-mapped EL1 stage-1 translation regime for QEMU virt. */
int mmu_enable(const struct fdt_info *fdt, uintptr_t kernel_start,
               uintptr_t kernel_end, uintptr_t boot_fdt);

#endif
