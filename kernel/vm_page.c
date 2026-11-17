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
#define DESC_LEAF_ALLOWED (DESC_ADDR_MASK | DESC_VALID | DESC_TABLE_PAGE | DESC_ATTR_DEVICE | \
                           DESC_AP_USER | DESC_AP_RDONLY | DESC_SH_INNER | \
                           DESC_AF | DESC_PXN | DESC_UXN)
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
        ((flags & VM_MAP_DEVICE) != 0U &&
         (flags & (VM_MAP_EXEC | VM_MAP_USER)) != 0U))
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
    if ((descriptor & (DESC_PXN | DESC_UXN)) !=
        (DESC_PXN | DESC_UXN))
        mapping->flags |= VM_MAP_EXEC;
    return 0;
}


#define DESC_TABLE_ADDR_MASK 0x0000fffffffff000ULL
#define VM_L1_SPAN (512ULL * VM_L2_SPAN)

static int table_pointer_valid(const void *pointer)
{
    uint64_t address = (uint64_t)(uintptr_t)pointer;
    return (address & 0xfffULL) == 0ULL &&
           (address & ~DESC_TABLE_ADDR_MASK) == 0ULL;
}

int vm_address_space_init(struct vm_address_space *space,
                          uint64_t virtual_base)
{
    uint32_t i;
    if (space == 0 || (virtual_base & (VM_L2_SPAN - 1ULL)) != 0ULL ||
        virtual_base >= (1ULL << 38) ||
        !table_pointer_valid(space->l1) ||
        !table_pointer_valid(space->l2))
        return -1;
    space->virtual_base = virtual_base;
    for (i = 0; i < 512U; ++i) {
        space->l1[i] = 0ULL;
        space->l2[i] = 0ULL;
    }
    for (i = 0; i < VM_ADDRESS_SPACE_L3_TABLES; ++i) {
        space->l3_used[i] = 0U;
        space->l3_l2_index[i] = 0U;
    }
    space->l1[virtual_base >> 30] =
        ((uint64_t)(uintptr_t)space->l2 & DESC_TABLE_ADDR_MASK) |
        DESC_VALID | DESC_TABLE_PAGE;
    return 0;
}

static int address_space_indices(const struct vm_address_space *space,
                                 uint64_t va, uint32_t *l2_index)
{
    if (space == 0 || l2_index == 0 || (va & (VM_PAGE_SIZE - 1ULL)) != 0ULL ||
        va < space->virtual_base ||
        va - space->virtual_base >= VM_L2_SPAN)
        return -1;
    *l2_index = (uint32_t)((va >> 21) & 0x1ffULL);
    return 0;
}

static int find_l3(const struct vm_address_space *space, uint32_t l2_index)
{
    uint32_t i;
    for (i = 0; i < VM_ADDRESS_SPACE_L3_TABLES; ++i)
        if (space->l3_used[i] != 0U &&
            space->l3_l2_index[i] == l2_index)
            return (int)i;
    return -1;
}

int vm_address_space_map(struct vm_address_space *space, uint64_t va,
                         uint64_t pa, uint32_t flags)
{
    uint32_t l2_index, i;
    int slot, created = 0;
    uint64_t l3_base;
    if (address_space_indices(space, va, &l2_index) != 0)
        return -1;
    slot = find_l3(space, l2_index);
    if (slot < 0) {
        for (i = 0; i < VM_ADDRESS_SPACE_L3_TABLES; ++i)
            if (space->l3_used[i] == 0U)
                break;
        if (i == VM_ADDRESS_SPACE_L3_TABLES)
            return -3;
        slot = (int)i;
        l3_base = va & ~(VM_L3_SPAN - 1ULL);
        if (vm_page_table_init(&space->l3[slot], l3_base) != 0 ||
            !table_pointer_valid(space->l3[slot].entries))
            return -4;
        space->l3_used[slot] = 1U;
        space->l3_l2_index[slot] = (uint16_t)l2_index;
        space->l2[l2_index] =
            ((uint64_t)(uintptr_t)space->l3[slot].entries &
             DESC_TABLE_ADDR_MASK) | DESC_VALID | DESC_TABLE_PAGE;
        created = 1;
    }
    if (vm_page_map(&space->l3[slot], va, pa, flags) != 0) {
        if (created != 0) {
            space->l2[l2_index] = 0ULL;
            space->l3_used[slot] = 0U;
        }
        return -2;
    }
    return 0;
}

int vm_address_space_unmap(struct vm_address_space *space, uint64_t va)
{
    uint32_t l2_index, i;
    int slot;
    if (address_space_indices(space, va, &l2_index) != 0)
        return -1;
    slot = find_l3(space, l2_index);
    if (slot < 0)
        return -2;
    if (vm_page_unmap(&space->l3[slot], va) != 0)
        return -2;
    for (i = 0; i < 512U; ++i)
        if (space->l3[slot].entries[i] != 0ULL)
            return 0;
    space->l2[l2_index] = 0ULL;
    space->l3_used[slot] = 0U;
    return 0;
}

int vm_address_space_lookup(const struct vm_address_space *space,
                            uint64_t va, struct vm_mapping *mapping)
{
    uint32_t l2_index;
    int slot;
    if (address_space_indices(space, va, &l2_index) != 0)
        return -1;
    slot = find_l3(space, l2_index);
    if (slot < 0)
        return -2;
    return vm_page_lookup(&space->l3[slot], va, mapping);
}

int vm_address_space_map_range(struct vm_address_space *space,
                               uint64_t va, uint64_t pa, uint64_t size,
                               uint32_t flags)
{
    uint64_t offset;
    if (space == 0 || size == 0ULL ||
        (va & (VM_PAGE_SIZE - 1ULL)) != 0ULL ||
        (pa & (VM_PAGE_SIZE - 1ULL)) != 0ULL ||
        (size & (VM_PAGE_SIZE - 1ULL)) != 0ULL ||
        va > UINT64_MAX - size || pa > UINT64_MAX - size ||
        va < space->virtual_base ||
        va - space->virtual_base > VM_L2_SPAN ||
        size > VM_L2_SPAN - (va - space->virtual_base))
        return -1;
    for (offset = 0ULL; offset < size; offset += VM_PAGE_SIZE) {
        if (vm_address_space_map(space, va + offset, pa + offset, flags) != 0) {
            while (offset != 0ULL) {
                offset -= VM_PAGE_SIZE;
                (void)vm_address_space_unmap(space, va + offset);
            }
            return -2;
        }
    }
    return 0;
}

int vm_address_space_validate(const struct vm_address_space *space)
{
    uint32_t i, j;
    uint32_t root_index;
    if (space == 0 ||
        (space->virtual_base & (VM_L2_SPAN - 1ULL)) != 0ULL ||
        space->virtual_base >= (1ULL << 38) ||
        !table_pointer_valid(space->l1) ||
        !table_pointer_valid(space->l2))
        return -1;

    root_index = (uint32_t)(space->virtual_base >> 30);
    for (i = 0; i < 512U; ++i) {
        uint64_t expected = 0ULL;
        if (i == root_index)
            expected = ((uint64_t)(uintptr_t)space->l2 &
                        DESC_TABLE_ADDR_MASK) |
                       DESC_VALID | DESC_TABLE_PAGE;
        if (space->l1[i] != expected)
            return -2;
    }

    for (i = 0; i < VM_ADDRESS_SPACE_L3_TABLES; ++i) {
        uint32_t index;
        uint64_t expected;
        if (space->l3_used[i] > 1U)
            return -3;
        if (space->l3_used[i] == 0U)
            continue;
        index = space->l3_l2_index[i];
        if (index >= 512U || !table_pointer_valid(space->l3[i].entries) ||
            space->l3[i].virtual_base !=
                space->virtual_base + (uint64_t)index * VM_L3_SPAN)
            return -4;
        for (j = i + 1U; j < VM_ADDRESS_SPACE_L3_TABLES; ++j)
            if (space->l3_used[j] != 0U &&
                space->l3_l2_index[j] == index)
                return -5;
        expected = ((uint64_t)(uintptr_t)space->l3[i].entries &
                    DESC_TABLE_ADDR_MASK) |
                   DESC_VALID | DESC_TABLE_PAGE;
        if (space->l2[index] != expected)
            return -6;
        for (j = 0; j < 512U; ++j) {
            uint64_t descriptor = space->l3[i].entries[j];
            if (descriptor == 0ULL)
                continue;
            if ((descriptor & DESC_TABLE_PAGE) != DESC_TABLE_PAGE ||
                (descriptor & ~DESC_LEAF_ALLOWED) != 0ULL ||
                (descriptor & DESC_AF) == 0ULL ||
                ((descriptor & (DESC_PXN | DESC_UXN)) !=
                 (DESC_PXN | DESC_UXN) &&
                 (descriptor & DESC_AP_RDONLY) == 0ULL) ||
                ((descriptor & DESC_ATTR_DEVICE) != 0ULL &&
                 ((descriptor & (DESC_PXN | DESC_UXN)) !=
                  (DESC_PXN | DESC_UXN) ||
                  (descriptor & (DESC_AP_USER | DESC_SH_INNER)) != 0ULL)) ||
                ((descriptor & DESC_ATTR_DEVICE) == 0ULL &&
                 (descriptor & DESC_SH_INNER) != DESC_SH_INNER))
                return -7;
        }
    }

    for (i = 0; i < 512U; ++i) {
        int found = 0;
        if (space->l2[i] == 0ULL)
            continue;
        for (j = 0; j < VM_ADDRESS_SPACE_L3_TABLES; ++j)
            if (space->l3_used[j] != 0U &&
                space->l3_l2_index[j] == i)
                found = 1;
        if (found == 0)
            return -8;
    }
    return 0;
}

uint64_t vm_address_space_root(const struct vm_address_space *space)
{
    if (vm_address_space_validate(space) != 0)
        return 0ULL;
    return (uint64_t)(uintptr_t)space->l1;
}
