#ifndef COXVBN_MMU_H
#define COXVBN_MMU_H

#include "fdt.h"

/* Enable an identity-mapped EL1 stage-1 translation regime for QEMU virt. */
int mmu_enable(const struct fdt_info *fdt, uintptr_t kernel_start,
               uintptr_t kernel_end, uintptr_t boot_fdt);

#endif
