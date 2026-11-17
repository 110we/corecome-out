#include "vm_page.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    static struct vm_page_table table;
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

    puts("vm page table tests: ok");
    return 0;
}
