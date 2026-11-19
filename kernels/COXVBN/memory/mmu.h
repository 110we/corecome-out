#ifndef COXVBN_MMU_H
#define COXVBN_MMU_H

#include "fdt.h"
#include <stddef.h>
#include <stdint.h>

#define COXVBN_MMU_BLOCK_SIZE UINT64_C(0x200000)
#define COXVBN_MMU_RAM_BASE UINT64_C(0x40000000)
#define COXVBN_MMU_RAM_END UINT64_C(0x80000000)
#define COXVBN_MMU_ADDRESS_MASK UINT64_C(0x0000ffffffe00000)
#define COXVBN_MMU_DESC_VALID UINT64_C(1)
#define COXVBN_MMU_DESC_TABLE UINT64_C(2)

/* Check every 2-MiB region touched by [start, end). A table descriptor
   denotes the kernel region's fully populated 4-KiB L3 page table. */
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
        if ((descriptor & COXVBN_MMU_DESC_VALID) == 0U)
            return 0;
        if ((descriptor & COXVBN_MMU_DESC_TABLE) == 0U &&
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
