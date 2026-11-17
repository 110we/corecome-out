#ifndef COXVBN_VM_PAGE_H
#define COXVBN_VM_PAGE_H

#include <stdint.h>

#define VM_PAGE_SIZE 4096ULL
#define VM_L3_SPAN (512ULL * VM_PAGE_SIZE)

#define VM_MAP_READ   (1U << 0)
#define VM_MAP_WRITE  (1U << 1)
#define VM_MAP_EXEC   (1U << 2)
#define VM_MAP_USER   (1U << 3)
#define VM_MAP_DEVICE (1U << 4)

struct vm_page_table {
    uint64_t virtual_base;
    uint64_t entries[512] __attribute__((aligned(4096)));
};

struct vm_mapping {
    uint64_t physical_address;
    uint32_t flags;
};

int vm_page_table_init(struct vm_page_table *table, uint64_t virtual_base);
int vm_page_map(struct vm_page_table *table, uint64_t virtual_address,
                uint64_t physical_address, uint32_t flags);
int vm_page_unmap(struct vm_page_table *table, uint64_t virtual_address);
int vm_page_lookup(const struct vm_page_table *table, uint64_t virtual_address,
                   struct vm_mapping *mapping);

#endif
