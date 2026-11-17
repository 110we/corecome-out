#include "mmu_layout.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct fdt_info fixture(void)
{
    struct fdt_info fdt;
    memset(&fdt, 0, sizeof(fdt));
    fdt.total_size = 0x10000U;
    fdt.memory_count = 1U;
    fdt.memory[0].base = 0x40000000ULL;
    fdt.memory[0].size = 0x40000000ULL;
    return fdt;
}

int main(void)
{
    struct fdt_info fdt = fixture();
    assert(mmu_validate_boot_layout(&fdt, 0x40080000U, 0x40100000U,
                                    0x41000000U) == 0);

    assert(mmu_validate_boot_layout(&fdt, 0x3ff00000U, 0x40100000U,
                                    0x41000000U) != 0);
    assert(mmu_validate_boot_layout(&fdt, 0x40080000U, 0x40100000U,
                                    0x7fff8000U) != 0);

    fdt.memory[0].base = 0x40080000ULL;
    fdt.memory[0].size = 0x3ff80000ULL;
    assert(mmu_validate_boot_layout(&fdt, 0x40080000U, 0x40100000U,
                                    0x41000000U) != 0);

    fdt = fixture();
    fdt.memory_count = 2U;
    fdt.memory[0].base = 0x40000000ULL;
    fdt.memory[0].size = 0x40100000ULL;
    fdt.memory[1].base = 0x40080000ULL;
    fdt.memory[1].size = 0x100000ULL;
    assert(mmu_validate_boot_layout(&fdt, 0x40080000U, 0x40100000U,
                                    0x41000000U) != 0);

    fdt = fixture();
    fdt.memory[0].base = UINT64_MAX - 0xfffULL;
    fdt.memory[0].size = 0x2000ULL;
    assert(mmu_validate_boot_layout(&fdt, 0x40080000U, 0x40100000U,
                                    0x41000000U) != 0);

    puts("mmu layout tests: ok");
    return 0;
}
