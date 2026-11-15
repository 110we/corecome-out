#ifndef CORECOME_FDT_H
#define CORECOME_FDT_H

#include <stdint.h>

struct fdt_info {
    uintptr_t address;
    uint32_t total_size;
    uint32_t structure_size;
    uint32_t strings_size;
};

int fdt_inspect(const void *blob, struct fdt_info *info);

#endif
