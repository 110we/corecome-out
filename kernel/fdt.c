#include "fdt.h"

#include <stddef.h>
#include <stdint.h>

#define FDT_MAGIC 0xd00dfeedU
#define FDT_HEADER_SIZE 40U
#define FDT_VERSION_MIN 17U

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

int fdt_inspect(const void *blob, struct fdt_info *info)
{
    const uint8_t *bytes = (const uint8_t *)blob;
    uint32_t total, struct_off, strings_off, struct_size, strings_size;
    uint32_t version, last_compatible;

    if (blob == NULL || info == NULL) {
        return -1;
    }

    if (((uintptr_t)blob & 3U) != 0U) {
        return -2;
    }

    if (read_be32(bytes) != FDT_MAGIC) {
        return -3;
    }

    total = read_be32(bytes + 4);
    struct_off = read_be32(bytes + 8);
    strings_off = read_be32(bytes + 12);
    version = read_be32(bytes + 20);
    last_compatible = read_be32(bytes + 24);
    strings_size = read_be32(bytes + 32);
    struct_size = read_be32(bytes + 36);

    if (total < FDT_HEADER_SIZE || version < FDT_VERSION_MIN ||
        last_compatible > version) {
        return -4;
    }

    if (struct_off > total || struct_size > total - struct_off ||
        strings_off > total || strings_size > total - strings_off) {
        return -5;
    }

    info->address = (uintptr_t)blob;
    info->total_size = total;
    info->structure_size = struct_size;
    info->strings_size = strings_size;
    return 0;
}
