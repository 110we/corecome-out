#include "vm_page.h"

#define DESC_VALID       (1ULL << 0)
#define DESC_TABLE_PAGE  (1ULL << 1)
#define DESC_ATTR_DEVICE (1ULL << 2)
#define DESC_AP_USER     (1ULL << 6)
#define DESC_AP_RDONLY   (1ULL << 7)
#define DESC_SH_INNER    (3ULL << 8)
#define DESC_AF          (1ULL << 10)
#define DESC_PXN         (1ULL << 53)
#define DESC_UXN         (1ULL << 54)
#define DESC_ADDR_MASK   0x0000fffffffff000ULL
#define VM_ALLOWED_FLAGS (VM_MAP_READ | VM_MAP_WRITE | VM_MAP_EXEC | VM_MAP_USER | VM_MAP_DEVICE)

static int aligned_page(uint64_t value)
{
    return (value & (VM_PAGE_SIZE - 1ULL)) == 0ULL;
}

static int address_valid(uint64_t value)
{
    return (value & ~DESC_ADDR_MASK) == 0ULL;
}

int vm_page_table_init(struct vm_page_table *table, uint64_t virtual_base)
{
    uint32_t i;
    if (table == 0 || (virtual_base & (VM_L3_SPAN - 1ULL)) != 0ULL ||
        !address_valid(virtual_base))
        return -1;
    table->virtual_base = virtual_base;
    for (i = 0; i < 512U; ++i)
        table->entries[i] = 0ULL;
    return 0;
}

static int page_index(const struct vm_page_table *table, uint64_t va,
                      uint32_t *index)
{
    if (table == 0 || index == 0 || !aligned_page(va) ||
        !address_valid(va) ||
        va < table->virtual_base || va - table->virtual_base >= VM_L3_SPAN)
        return -1;
    *index = (uint32_t)((va - table->virtual_base) / VM_PAGE_SIZE);
    return 0;
}

int vm_page_map(struct vm_page_table *table, uint64_t va, uint64_t pa,
                uint32_t flags)
{
    uint32_t index;
    uint64_t descriptor;
    if (page_index(table, va, &index) != 0 || !aligned_page(pa) ||
        !address_valid(pa) || (flags & ~VM_ALLOWED_FLAGS) != 0U ||
        (flags & VM_MAP_READ) == 0U ||
        ((flags & VM_MAP_WRITE) != 0U && (flags & VM_MAP_EXEC) != 0U) ||
        ((flags & VM_MAP_DEVICE) != 0U && (flags & VM_MAP_EXEC) != 0U))
        return -1;
    if (table->entries[index] != 0ULL)
        return -2;

    descriptor = (pa & DESC_ADDR_MASK) | DESC_VALID | DESC_TABLE_PAGE |
                 DESC_AF;
    if ((flags & VM_MAP_DEVICE) != 0U)
        descriptor |= DESC_ATTR_DEVICE;
    else
        descriptor |= DESC_SH_INNER;
    if ((flags & VM_MAP_USER) != 0U)
        descriptor |= DESC_AP_USER;
    if ((flags & VM_MAP_WRITE) == 0U)
        descriptor |= DESC_AP_RDONLY;
    if ((flags & VM_MAP_EXEC) == 0U)
        descriptor |= DESC_PXN | DESC_UXN;
    else if ((flags & VM_MAP_USER) != 0U)
        descriptor |= DESC_PXN;
    else
        descriptor |= DESC_UXN;
    table->entries[index] = descriptor;
    return 0;
}

int vm_page_unmap(struct vm_page_table *table, uint64_t va)
{
    uint32_t index;
    if (page_index(table, va, &index) != 0)
        return -1;
    if ((table->entries[index] & DESC_VALID) == 0ULL)
        return -2;
    table->entries[index] = 0ULL;
    return 0;
}

int vm_page_lookup(const struct vm_page_table *table, uint64_t va,
                   struct vm_mapping *mapping)
{
    uint32_t index;
    uint64_t descriptor;
    if (mapping == 0 || page_index(table, va, &index) != 0)
        return -1;
    descriptor = table->entries[index];
    if ((descriptor & DESC_VALID) == 0ULL)
        return -2;
    mapping->physical_address = descriptor & DESC_ADDR_MASK;
    mapping->flags = VM_MAP_READ;
    if ((descriptor & DESC_AP_USER) != 0ULL)
        mapping->flags |= VM_MAP_USER;
    if ((descriptor & DESC_AP_RDONLY) == 0ULL)
        mapping->flags |= VM_MAP_WRITE;
    if ((descriptor & DESC_ATTR_DEVICE) != 0ULL)
        mapping->flags |= VM_MAP_DEVICE;
    if ((descriptor & DESC_PXN) != (descriptor & DESC_UXN))
        mapping->flags |= VM_MAP_EXEC;
    return 0;
}
