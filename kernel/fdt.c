#include "fdt.h"

#include <stddef.h>
#include <stdint.h>

#define FDT_MAGIC 0xd00dfeedU
#define FDT_HEADER_SIZE 40U
#define FDT_VERSION_MIN 17U
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U
#define FDT_MAX_DEPTH 32U

struct node_state {
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t reg_length;
    const uint8_t *reg;
    uint8_t is_memory;
    uint8_t is_reserved;
    uint8_t is_reserved_root;
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t be_cells(const uint8_t *p, uint32_t cells)
{
    uint64_t value = 0;
    uint32_t i;
    for (i = 0; i < cells; ++i)
        value = (value << 32) | be32(p + (size_t)i * 4U);
    return value;
}

static int bounded_string(const uint8_t *base, uint32_t size,
                          uint32_t offset, const char **out)
{
    uint32_t i;
    if (offset >= size) return -1;
    for (i = offset; i < size; ++i) {
        if (base[i] == 0) {
            *out = (const char *)(base + offset);
            return 0;
        }
    }
    return -1;
}

static int add_range(struct fdt_range *ranges, uint32_t *count,
                     uint64_t base, uint64_t size)
{
    if (size == 0 || base > UINT64_MAX - size ||
        *count >= COXVBN_FDT_MAX_RANGES) return -1;
    ranges[*count].base = base;
    ranges[*count].size = size;
    ++*count;
    return 0;
}

int fdt_inspect(const void *blob, struct fdt_info *info)
{
    const uint8_t *b = (const uint8_t *)blob;
    uint32_t total, so, ss, sto, sts, ver, compat, ro;
    if (blob == NULL || info == NULL) return -1;
    if (((uintptr_t)blob & 7U) != 0U) return -2;
    if (be32(b) != FDT_MAGIC) return -3;
    total = be32(b + 4); so = be32(b + 8); sto = be32(b + 12);
    ro = be32(b + 16); ver = be32(b + 20); compat = be32(b + 24);
    sts = be32(b + 32); ss = be32(b + 36);
    if (total < FDT_HEADER_SIZE || ver < FDT_VERSION_MIN ||
        compat > ver) return -4;
    if ((so & 3U) || (sto & 3U) || (ro & 7U) ||
        so > total || ss > total - so ||
        sto > total || sts > total - sto ||
        ro > total || total - ro < 16U) return -5;
    if (so < FDT_HEADER_SIZE || sto < FDT_HEADER_SIZE ||
        ro < FDT_HEADER_SIZE) return -6;
    info->address = (uintptr_t)blob;
    info->total_size = total;
    info->structure_size = ss;
    info->strings_size = sts;
    info->address_cells = 2;
    info->size_cells = 1;
    info->memory_count = 0;
    info->reserved_count = 0;
    return 0;
}

static int parse_reservations(const uint8_t *b, uint32_t total,
                              uint32_t offset, struct fdt_info *info)
{
    uint32_t pos = offset;
    while (pos <= total && total - pos >= 16U) {
        uint64_t base = ((uint64_t)be32(b + pos) << 32) | be32(b + pos + 4);
        uint64_t size = ((uint64_t)be32(b + pos + 8) << 32) | be32(b + pos + 12);
        pos += 16U;
        if (base == 0 && size == 0) return 0;
        if (add_range(info->reserved, &info->reserved_count, base, size))
            return -1;
    }
    return -1;
}

static int collect_node(struct node_state *n, struct fdt_info *info)
{
    uint32_t tuple, tuple_bytes, off;
    if (n->reg == NULL || (!n->is_memory && !n->is_reserved)) return 0;
    if (n->address_cells == 0 || n->address_cells > 2 ||
        n->size_cells == 0 || n->size_cells > 2) return -1;
    tuple = n->address_cells + n->size_cells;
    tuple_bytes = tuple * 4U;
    if (tuple_bytes == 0 || n->reg_length == 0 ||
        n->reg_length % tuple_bytes != 0) return -1;
    for (off = 0; off < n->reg_length; off += tuple_bytes) {
        uint64_t base = be_cells(n->reg + off, n->address_cells);
        uint64_t size = be_cells(n->reg + off + n->address_cells * 4U,
                                 n->size_cells);
        if (n->is_reserved) {
            if (add_range(info->reserved, &info->reserved_count, base, size))
                return -1;
        } else if (add_range(info->memory, &info->memory_count, base, size)) {
            return -1;
        }
    }
    return 0;
}

int fdt_parse(const void *blob, struct fdt_info *info)
{
    const uint8_t *b = (const uint8_t *)blob;
    const uint8_t *structure, *strings;
    struct node_state stack[FDT_MAX_DEPTH];
    uint32_t so, sto, ro, ss, sts, pos = 0, depth = 0;
    int ended = 0, rc;
    rc = fdt_inspect(blob, info);
    if (rc) return rc;
    so = be32(b + 8); sto = be32(b + 12); ro = be32(b + 16);
    ss = be32(b + 36); sts = be32(b + 32);
    structure = b + so; strings = b + sto;
    if (parse_reservations(b, info->total_size, ro, info)) return -7;

    while (pos <= ss && ss - pos >= 4U) {
        uint32_t token = be32(structure + pos);
        pos += 4U;
        if (token == FDT_BEGIN_NODE) {
            uint32_t start = pos, len;
            if (depth >= FDT_MAX_DEPTH) return -8;
            while (pos < ss && structure[pos] != 0) ++pos;
            if (pos >= ss) return -9;
            len = pos - start;
            ++pos;
            pos = (pos + 3U) & ~3U;
            if (pos > ss) return -9;
            if (depth == 0) {
                stack[depth] = (struct node_state){2, 1, 0, NULL, 0, 0, 0};
            } else {
                stack[depth] = stack[depth - 1U];
                stack[depth].reg = NULL;
                stack[depth].reg_length = 0;
                stack[depth].is_memory = 0;
                stack[depth].is_reserved_root = 0;
            }
            if (len == 6U) {
                const char *nm = (const char *)(structure + start);
                if (nm[0]=='m' && nm[1]=='e' && nm[2]=='m' &&
                    nm[3]=='o' && nm[4]=='r' && nm[5]=='y')
                    stack[depth].is_memory = 1;
            }
            if (len == 14U &&
                structure[start]=='r' && structure[start+1]=='e' &&
                structure[start+2]=='s' && structure[start+3]=='e' &&
                structure[start+4]=='r' && structure[start+5]=='v' &&
                structure[start+6]=='e' && structure[start+7]=='d' &&
                structure[start+8]=='-' && structure[start+9]=='m' &&
                structure[start+10]=='e' && structure[start+11]=='m' &&
                structure[start+12]=='o' && structure[start+13]=='r' &&
                depth > 0) {
                stack[depth].is_reserved_root = 1;
                stack[depth].is_reserved = 1;
            }
            ++depth;
        } else if (token == FDT_END_NODE) {
            if (depth == 0) return -10;
            if (collect_node(&stack[depth - 1U], info)) return -11;
            --depth;
        } else if (token == FDT_PROP) {
            uint32_t len, nameoff;
            const char *name;
            struct node_state *n;
            const uint8_t *value;
            if (depth == 0 || ss - pos < 8U) return -12;
            len = be32(structure + pos);
            nameoff = be32(structure + pos + 4U);
            pos += 8U;
            if (len > ss - pos || bounded_string(strings, sts, nameoff, &name))
                return -13;
            value = structure + pos;
            n = &stack[depth - 1U];
            if (name[0]=='#' && name[1]=='a' && name[2]=='d' &&
                name[3]=='d' && name[4]=='r' && name[5]=='e' &&
                name[6]=='s' && name[7]=='s' && name[8]=='-' &&
                name[9]=='c' && name[10]=='e' && name[11]=='l' &&
                name[12]=='l' && name[13]=='s' && len == 4U)
                n->address_cells = be32(value);
            else if (name[0]=='#' && name[1]=='s' && name[2]=='i' &&
                     name[3]=='z' && name[4]=='e' && name[5]=='-' &&
                     name[6]=='c' && name[7]=='e' && name[8]=='l' &&
                     name[9]=='l' && name[10]=='s' && len == 4U)
                n->size_cells = be32(value);
            else if (name[0]=='r' && name[1]=='e' && name[2]=='g' &&
                     name[3]==0) {
                n->reg = value;
                n->reg_length = len;
            } else if (name[0]=='d' && name[1]=='e' && name[2]=='v' &&
                       name[3]=='i' && name[4]=='c' && name[5]=='e' &&
                       name[6]=='_' && name[7]=='t' && name[8]=='y' &&
                       name[9]=='p' && name[10]=='e' && name[11]==0 &&
                       len >= 7U && value[0]=='m' && value[1]=='e' &&
                       value[2]=='m' && value[3]=='o' && value[4]=='r' &&
                       value[5]=='y' && value[6]==0)
                n->is_memory = 1;
            pos += len;
            pos = (pos + 3U) & ~3U;
            if (pos > ss) return -14;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            if (depth != 0) return -15;
            ended = 1;
            break;
        } else {
            return -16;
        }
    }
    if (!ended) return -17;
    info->address_cells = 2;
    info->size_cells = 1;
    return 0;
}
