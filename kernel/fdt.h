#ifndef COXVBN_FDT_H
#define COXVBN_FDT_H

#include <stdint.h>

#define COXVBN_FDT_MAX_RANGES 32U
#define COXVBN_FDT_OK 0

struct fdt_range {
    uint64_t base;
    uint64_t size;
};

struct fdt_info {
    uintptr_t address;
    uint32_t total_size;
    uint32_t structure_size;
    uint32_t strings_size;
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t memory_count;
    uint32_t reserved_count;
    struct fdt_range memory[COXVBN_FDT_MAX_RANGES];
    struct fdt_range reserved[COXVBN_FDT_MAX_RANGES];
};

/* Returns zero on success; negative values identify malformed input. */
int fdt_inspect(const void *blob, struct fdt_info *info);
int fdt_parse(const void *blob, struct fdt_info *info);

#endif
