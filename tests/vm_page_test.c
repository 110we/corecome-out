#include "vm_page.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    static struct vm_page_table table;
    static struct vm_address_space space;
    struct vm_mapping mapping;
    const uint64_t base = 0x0000000040000000ULL;

    assert(vm_page_table_init(&table, base) == 0);
    assert(vm_page_map(&table, base, 0x40000000ULL,
                       VM_MAP_READ | VM_MAP_WRITE) == 0);
    assert(vm_page_lookup(&table, base, &mapping) == 0);
    assert(mapping.physical_address == 0x40000000ULL);
    assert((mapping.flags & (VM_MAP_READ | VM_MAP_WRITE)) ==
           (VM_MAP_READ | VM_MAP_WRITE));
    assert((mapping.flags & VM_MAP_EXEC) == 0U);
    assert(vm_page_map(&table, base, 0x40001000ULL, VM_MAP_READ) == -2);
    assert(vm_page_unmap(&table, base) == 0);
    assert(vm_page_lookup(&table, base, &mapping) == -2);
    assert(vm_page_unmap(&table, base) == -2);

    assert(vm_page_map(&table, base + VM_PAGE_SIZE, 0x50000000ULL,
                       VM_MAP_READ | VM_MAP_USER | VM_MAP_EXEC) == 0);
    assert(vm_page_lookup(&table, base + VM_PAGE_SIZE, &mapping) == 0);
    assert((mapping.flags & (VM_MAP_USER | VM_MAP_EXEC)) ==
           (VM_MAP_USER | VM_MAP_EXEC));
    assert((mapping.flags & VM_MAP_WRITE) == 0U);

    assert(vm_page_map(&table, base + 3U * VM_PAGE_SIZE, 0x50003000ULL,
                       VM_MAP_READ | VM_MAP_EXEC) == 0);
    assert(vm_page_lookup(&table, base + 3U * VM_PAGE_SIZE, &mapping) == 0);
    assert((mapping.flags & VM_MAP_EXEC) != 0U);

    assert(vm_page_map(&table, base + 2U * VM_PAGE_SIZE, 0x50001000ULL,
                       VM_MAP_READ | VM_MAP_WRITE | VM_MAP_EXEC) == -1);
    assert(vm_page_map(&table, base + 2U * VM_PAGE_SIZE, 0x50001001ULL,
                       VM_MAP_READ) == -1);
    assert(vm_page_map(&table, base + 2U * VM_PAGE_SIZE, 0x50001000ULL,
                       VM_MAP_READ | VM_MAP_DEVICE | VM_MAP_EXEC) == -1);
    assert(vm_page_map(&table, base + VM_L3_SPAN, 0x50002000ULL,
                       VM_MAP_READ) == -1);
    assert(vm_page_map(&table, base + 2U * VM_PAGE_SIZE,
                       0x0001000000000000ULL, VM_MAP_READ) == -1);
    assert(vm_page_table_init(&table, base + VM_PAGE_SIZE) == -1);
    assert(vm_page_lookup(&table, 0x0001000040000000ULL, &mapping) == -1);

    {
        const uint64_t as_base = 0x0000000080000000ULL;
        uint32_t i;
        assert(vm_address_space_init(&space, as_base) == 0);
        assert(vm_address_space_map(&space, as_base, 0x60000000ULL,
                                    VM_MAP_READ | VM_MAP_WRITE | VM_MAP_USER) == 0);
        assert(vm_address_space_map(&space, as_base + VM_L3_SPAN,
                                    0x60001000ULL, VM_MAP_READ | VM_MAP_USER) == 0);
        assert(vm_address_space_lookup(&space, as_base + VM_L3_SPAN,
                                       &mapping) == 0);
        assert(mapping.physical_address == 0x60001000ULL);
        assert((mapping.flags & VM_MAP_USER) != 0U);
        assert(vm_address_space_map(&space, as_base,
                                    0x60002000ULL, VM_MAP_READ) == -2);
        assert(vm_address_space_unmap(&space, as_base) == 0);
        assert(vm_address_space_unmap(&space, as_base + VM_L3_SPAN) == 0);
        assert(vm_address_space_lookup(&space, as_base, &mapping) == -2);

        for (i = 0; i < VM_ADDRESS_SPACE_L3_TABLES; ++i)
            assert(vm_address_space_map(&space,
                       as_base + (uint64_t)i * VM_L3_SPAN,
                       0x61000000ULL + (uint64_t)i * VM_PAGE_SIZE,
                       VM_MAP_READ) == 0);
        assert(vm_address_space_map(&space,
                   as_base + (uint64_t)VM_ADDRESS_SPACE_L3_TABLES * VM_L3_SPAN,
                   0x62000000ULL, VM_MAP_READ) == -3);
        assert(vm_address_space_unmap(&space, as_base) == 0);
        assert(vm_address_space_map(&space,
                   as_base + (uint64_t)VM_ADDRESS_SPACE_L3_TABLES * VM_L3_SPAN,
                   0x62000000ULL, VM_MAP_READ) == 0);
        assert(vm_address_space_init(&space, as_base + VM_PAGE_SIZE) == -1);

        assert(vm_address_space_map_range(&space, as_base + 0x8000ULL,
                   0x63000000ULL, 3ULL * VM_PAGE_SIZE,
                   VM_MAP_READ | VM_MAP_WRITE) == 0);
        assert(vm_address_space_lookup(&space, as_base + 0xa000ULL,
                                       &mapping) == 0);
        assert(mapping.physical_address == 0x63002000ULL);
        assert(vm_address_space_map(&space, as_base + 0x11000ULL,
                   0x64000000ULL, VM_MAP_READ) == 0);
        assert(vm_address_space_map_range(&space, as_base + 0x10000ULL,
                   0x65000000ULL, 3ULL * VM_PAGE_SIZE,
                   VM_MAP_READ) == -2);
        assert(vm_address_space_lookup(&space, as_base + 0x10000ULL,
                                       &mapping) == -2);
        assert(vm_address_space_lookup(&space, as_base + 0x11000ULL,
                                       &mapping) == 0);
        assert(mapping.physical_address == 0x64000000ULL);
        assert(vm_address_space_map_range(&space, as_base, 0x60000000ULL,
                   0ULL, VM_MAP_READ) == -1);
        assert(vm_address_space_map_range(&space, UINT64_MAX - 0xfffULL,
                   0x66000000ULL, 2ULL * VM_PAGE_SIZE,
                   VM_MAP_READ) == -1);
    }

    puts("vm page table tests: ok");
    return 0;
}
