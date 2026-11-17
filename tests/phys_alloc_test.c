#include "../kernel/phys_alloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct fdt_info fdt;
    struct phys_allocator_stats stats;
    uintptr_t first;
    uintptr_t second;

    memset(&fdt, 0, sizeof(fdt));
    fdt.memory_count = 1U;
    fdt.memory[0].base = 0x40000000ULL;
    fdt.memory[0].size = 0x01000000ULL;
    fdt.reserved_count = 1U;
    fdt.reserved[0].base = 0x40002000ULL;
    fdt.reserved[0].size = 0x2000ULL;

    assert(phys_allocator_init(&fdt, 0x40008000U, 0x40010000U,
                               0x40010000U, 0x40011000U) == 0);
    phys_allocator_get_stats(&stats);
    assert(stats.total_pages == 4096U && stats.free_pages > 0U);

    /* Reserved, kernel, and boot-FDT pages are not caller-owned allocations. */
    assert(phys_free_page(0x40002000U) == -3);
    assert(phys_free_page(0x40008000U) == -3);
    assert(phys_free_page(0x40010000U) == -3);

    first = phys_alloc_page();
    second = phys_alloc_page();
    assert(first != 0U && second != 0U && first != second);
    assert((first & 4095U) == 0U && (second & 4095U) == 0U);
    assert(first < 0x40002000U || first >= 0x40004000U);
    assert(first < 0x40008000U || first >= 0x40011000U);

    assert(phys_free_page(first) == 0);
    assert(phys_free_page(first) == -3);
    assert(phys_free_page(second + 1U) == -1);
    assert(phys_free_page(second) == 0);
    assert(phys_free_page(0U) == -2);

    puts("COXVBN physical allocator tests: PASS");
    return 0;
}
