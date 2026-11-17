#ifndef COXVBN_MMU_LAYOUT_H
#define COXVBN_MMU_LAYOUT_H

#include "fdt.h"
#include <stdint.h>

/* Validate the machine-specific identity-map coverage before touching MMU
 * registers. Exposed separately so malformed layouts can be host-tested. */
int mmu_validate_boot_layout(const struct fdt_info *fdt,
                             uintptr_t kernel_start,
                             uintptr_t kernel_end,
                             uintptr_t boot_fdt);

#endif
