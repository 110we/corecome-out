#ifndef COXVBN_PHYS_ALLOC_H
#define COXVBN_PHYS_ALLOC_H
#include "fdt.h"
#include <stdint.h>
#define COXVBN_PAGE_SIZE 4096U
#define COXVBN_PHYS_MAX_PAGES 1048576U
struct phys_allocator_stats { uint32_t total_pages; uint32_t free_pages; };
int phys_allocator_init(const struct fdt_info *,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
uintptr_t phys_alloc_page(void);
int phys_free_page(uintptr_t);
void phys_allocator_get_stats(struct phys_allocator_stats *);
#endif
