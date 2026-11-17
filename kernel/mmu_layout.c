#include "mmu_layout.h"

#include <stddef.h>
#include <stdint.h>

#define MMU_RAM_BASE 0x40000000ULL
#define MMU_RAM_END  0x80000000ULL
#define MMU_BLOCK_SIZE (2ULL * 1024ULL * 1024ULL)

static int contains(const struct fdt_info *fdt, uint64_t start,
                    uint64_t end)
{
    uint32_t i;
    for (i = 0; i < fdt->memory_count; ++i) {
        uint64_t base = fdt->memory[i].base;
        uint64_t size = fdt->memory[i].size;
        if (size != 0U && base <= start &&
            size <= UINT64_MAX - base && base + size >= end)
            return 1;
    }
    return 0;
}

/* The bootstrap mapper emits only complete 2-MiB RAM blocks. */
static int fully_block_mapped(const struct fdt_info *fdt,
                              uint64_t start, uint64_t end)
{
    uint64_t block;
    uint64_t rounded_end;

    if (start >= end || start < MMU_RAM_BASE || end > MMU_RAM_END)
        return 0;
    block = start & ~(MMU_BLOCK_SIZE - 1ULL);
    rounded_end = (end + MMU_BLOCK_SIZE - 1ULL) &
                  ~(MMU_BLOCK_SIZE - 1ULL);
    while (block < rounded_end) {
        if (block > UINT64_MAX - MMU_BLOCK_SIZE ||
            !contains(fdt, block, block + MMU_BLOCK_SIZE))
            return 0;
        block += MMU_BLOCK_SIZE;
    }
    return 1;
}

int mmu_validate_boot_layout(const struct fdt_info *fdt,
                             uintptr_t kernel_start,
                             uintptr_t kernel_end,
                             uintptr_t boot_fdt)
{
    uint64_t fdt_start = (uint64_t)boot_fdt;
    uint64_t fdt_end;
    uint32_t i;
    if (fdt == NULL || fdt->memory_count == 0U ||
        fdt->memory_count > COXVBN_FDT_MAX_RANGES ||
        fdt->total_size < 40U || kernel_start >= kernel_end ||
        (uint64_t)kernel_start < MMU_RAM_BASE ||
        (uint64_t)kernel_end > MMU_RAM_END ||
        fdt_start < MMU_RAM_BASE || fdt_start >= MMU_RAM_END ||
        fdt_start > UINT64_MAX - fdt->total_size)
        return -1;

    fdt_end = fdt_start + fdt->total_size;
    if (fdt_end > MMU_RAM_END ||
        !contains(fdt, (uint64_t)kernel_start, (uint64_t)kernel_end) ||
        !contains(fdt, fdt_start, fdt_end))
        return -2;

    for (i = 0; i < fdt->memory_count; ++i) {
        uint64_t base = fdt->memory[i].base;
        uint64_t size = fdt->memory[i].size;
        uint64_t end;
        uint32_t j;
        if (size == 0U || base > UINT64_MAX - size)
            return -3;
        end = base + size;
        if (base < MMU_RAM_BASE || end > MMU_RAM_END)
            return -4;
        for (j = 0; j < i; ++j) {
            uint64_t other_base = fdt->memory[j].base;
            uint64_t other_size = fdt->memory[j].size;
            uint64_t other_end;
            if (other_size == 0U ||
                other_base > UINT64_MAX - other_size)
                return -3;
            other_end = other_base + other_size;
            if (base < other_end && other_base < end)
                return -5;
        }
    }

    if (!fully_block_mapped(fdt, (uint64_t)kernel_start,
                            (uint64_t)kernel_end) ||
        !fully_block_mapped(fdt, fdt_start, fdt_end))
        return -6;
    return 0;
}
