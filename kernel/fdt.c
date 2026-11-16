#include "fdt.h"
#ifdef COXVBN_FDT_DEBUG
#include "uart.h"
#define FDT_TRACE(message) uart_write(message)
#else
#define FDT_TRACE(message) ((void)0)
#endif

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

#ifdef COXVBN_FDT_DEBUG
static void fdt_debug_hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char out[8];
    int i;
    for (i = 7; i >= 0; --i) {
        out[i] = digits[value & 0xfU];
        value >>= 4;
    }
    for (i = 0; i < 8; ++i) uart_putc(out[i]);
}
#else
#define fdt_debug_hex(value) ((void)(value))
#endif

struct node_state {
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t reg_length;
    const uint8_t *reg;
    uint8_t is_memory;
    uint8_t is_reserved;
    uint8_t is_reserved_container;
    uint8_t is_gicv2;
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

static int string_is(const char *s, const char *expected)
{
    uint32_t i = 0;
    while (expected[i] != 0) {
        if (s[i] != expected[i]) return 0;
        ++i;
    }
    return s[i] == 0;
}

static int string_prefix(const char *s, const char *prefix)
{
    uint32_t i = 0;
    while (prefix[i] != 0) {
        if (s[i] == 0 || s[i] != prefix[i]) return 0;
        ++i;
    }
    return 1;
}

static int compatible_has_gicv2(const uint8_t *value, uint32_t length)
{
    static const char *const names[] = {
        "arm,cortex-a15-gic", "arm,gic-400", "arm,arm11mp-gic"
    };
    uint32_t offset = 0U, i;
    while (offset < length) {
        uint32_t end = offset;
        while (end < length && value[end] != 0U) ++end;
        if (end == length) return 0;
        for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
            uint32_t n = 0U, j;
            while (names[i][n] != 0) ++n;
            if (end - offset != n) continue;
            for (j = 0U; j < n && value[offset + j] == (uint8_t)names[i][j]; ++j) {}
            if (j == n) return 1;
        }
        offset = end + 1U;
    }
    return 0;
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

static int ranges_overlap(uint32_t a, uint32_t as,
                          uint32_t b, uint32_t bs)
{
    return a < b + bs && b < a + as;
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
    uint32_t struct_end, strings_end;
    FDT_TRACE("FDTDBG: inspect start\n");
    if (blob == NULL || info == NULL) return -1;
    FDT_TRACE("FDTDBG: pointers ok\n");
    if (((uintptr_t)blob & 7U) != 0U) return -2;
    FDT_TRACE("FDTDBG: aligned\n");
    if (be32(b) != FDT_MAGIC) return -3;
    FDT_TRACE("FDTDBG: magic ok\n");
    total = be32(b + 4); so = be32(b + 8); sto = be32(b + 12);
    ro = be32(b + 16); ver = be32(b + 20); compat = be32(b + 24);
    FDT_TRACE("FDTDBG: header fields\n");
    sts = be32(b + 32); ss = be32(b + 36);
    FDT_TRACE("FDTDBG: header sizes\n");
    FDT_TRACE("FDTDBG: validate basic\n");
    if (total < FDT_HEADER_SIZE || ver < FDT_VERSION_MIN ||
        compat > ver) return -4;
    FDT_TRACE("FDTDBG: basic ok\n");
    if ((so & 3U) || (ro & 7U) || (ss & 3U) ||
        so < FDT_HEADER_SIZE || sto < FDT_HEADER_SIZE ||
        ro < FDT_HEADER_SIZE || so > total || ss > total - so ||
        sto > total || sts > total - sto ||
        ro > total || total - ro < 16U) return -5;
    FDT_TRACE("FDTDBG: offsets ok\n");
    struct_end = so + ss;
    strings_end = sto + sts;
    if (ss == 0U || sts == 0U ||
        ranges_overlap(so, ss, sto, sts)) return -6;
    FDT_TRACE("FDTDBG: blocks ok\n");
    /* The reservation map must precede both data blocks. */
    if (ro >= so || ro >= sto) return -7;
    FDT_TRACE("FDTDBG: reserve offset ok\n");
    if (struct_end > total || strings_end > total) return -8;
    FDT_TRACE("FDTDBG: bounds ok\n");

    FDT_TRACE("FDTDBG: info write start\n");
    info->address = (uintptr_t)blob;
    FDT_TRACE("FDTDBG: info address ok\n");
    info->total_size = total;
    FDT_TRACE("FDTDBG: info total ok\n");
    info->structure_size = ss;
    info->strings_size = sts;
    FDT_TRACE("FDTDBG: info sizes ok\n");
    /* Avoid a combined unaligned 64-bit store to adjacent 32-bit fields. */
    ((volatile uint32_t *)&info->address_cells)[0] = 2U;
    FDT_TRACE("FDTDBG: info address-cells write ok\n");
    ((volatile uint32_t *)&info->size_cells)[0] = 1U;
    FDT_TRACE("FDTDBG: info size-cells write ok\n");
    FDT_TRACE("FDTDBG: before memory-count store\n");
    ((volatile uint32_t *)&info->memory_count)[0] = 0U;
    FDT_TRACE("FDTDBG: memory-count store ok\n");
    ((volatile uint32_t *)&info->reserved_count)[0] = 0U;
    info->gicv2_found = 0U;
    info->gicv2_reserved = 0U;
    info->gicv2_distributor.base = 0U;
    info->gicv2_distributor.size = 0U;
    info->gicv2_cpu_interface.base = 0U;
    info->gicv2_cpu_interface.size = 0U;
    FDT_TRACE("FDTDBG: reserved-count store ok\n");
    FDT_TRACE("FDTDBG: info counts ok\n");
    return 0;
}

static int parse_reservations(const uint8_t *b, uint32_t limit,
                              uint32_t offset, struct fdt_info *info)
{
    uint32_t pos = offset;
    while (pos <= limit && limit - pos >= 16U) {
        uint64_t base = ((uint64_t)be32(b + pos) << 32) |
                        be32(b + pos + 4U);
        uint64_t size = ((uint64_t)be32(b + pos + 8U) << 32) |
                        be32(b + pos + 12U);
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
    if (n->is_gicv2) {
        uint32_t tuple, tuple_bytes;
        uint64_t db, ds, cb, cs;
        if (n->reg == NULL || info->gicv2_found != 0U ||
            n->address_cells == 0U || n->address_cells > 2U ||
            n->size_cells == 0U || n->size_cells > 2U) return -1;
        tuple = n->address_cells + n->size_cells;
        tuple_bytes = tuple * 4U;
        if (n->reg_length < tuple_bytes * 2U ||
            n->reg_length % tuple_bytes != 0U) return -1;
        db = be_cells(n->reg, n->address_cells);
        ds = be_cells(n->reg + n->address_cells * 4U, n->size_cells);
        cb = be_cells(n->reg + tuple_bytes, n->address_cells);
        cs = be_cells(n->reg + tuple_bytes + n->address_cells * 4U,
                      n->size_cells);
        if (ds == 0U || cs == 0U || db > UINT64_MAX - ds ||
            cb > UINT64_MAX - cs || (db < cb + cs && cb < db + ds))
            return -1;
        info->gicv2_distributor.base = db;
        info->gicv2_distributor.size = ds;
        info->gicv2_cpu_interface.base = cb;
        info->gicv2_cpu_interface.size = cs;
        info->gicv2_found = 1U;
        return 0;
    }
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
    uint32_t so, sto, ro, ss, sts, pos = 0, depth = 0, reserve_limit;
    int ended = 0, root_closed = 0, rc;
    FDT_TRACE("FDTDBG: enter\n");
    rc = fdt_inspect(blob, info);
    if (rc) { FDT_TRACE("FDTDBG: header reject\n"); return rc; }
    FDT_TRACE("FDTDBG: header ok\n");
    so = be32(b + 8); sto = be32(b + 12); ro = be32(b + 16);
    ss = be32(b + 36); sts = be32(b + 32);
    structure = b + so; strings = b + sto;
    reserve_limit = so < sto ? so : sto;
    FDT_TRACE("FDTDBG: reserve start\n");
    if (parse_reservations(b, reserve_limit, ro, info)) { FDT_TRACE("FDTDBG: reserve bad\n"); return -9; }
    FDT_TRACE("FDTDBG: reserve ok\n");

    FDT_TRACE("FDTDBG: structure start\n");
    FDT_TRACE("FDTDBG: structure pointer "); fdt_debug_hex((uint32_t)(uintptr_t)structure); FDT_TRACE(" offset "); fdt_debug_hex(so); FDT_TRACE(" size "); fdt_debug_hex(ss); FDT_TRACE("\n");
    FDT_TRACE("FDTDBG: before first token read\n");
    while (pos <= ss && ss - pos >= 4U) {
        uint32_t token;
        FDT_TRACE("FDTDBG: token address "); fdt_debug_hex((uint32_t)(uintptr_t)(structure + pos)); FDT_TRACE("\n");
        token = be32(structure + pos);
        FDT_TRACE("FDTDBG: first token read\n");
        pos += 4U;
        if (token == FDT_BEGIN_NODE) {
            uint32_t start = pos, len;
            uint64_t aligned;
            if (depth >= FDT_MAX_DEPTH || root_closed) return -10;
            while (pos < ss && structure[pos] != 0) ++pos;
            if (pos >= ss) return -11;
            len = pos - start;
            ++pos;
            aligned = ((uint64_t)pos + 3U) & ~(uint64_t)3U;
            if (aligned > ss) return -11;
            pos = (uint32_t)aligned;
            if (depth == 0) {
                if (len != 0U) return -12;
                stack[depth] = (struct node_state){2, 1, 0, NULL, 0, 0, 0, 0};
            } else {
                stack[depth] = stack[depth - 1U];
                stack[depth].reg = NULL;
                stack[depth].reg_length = 0;
                stack[depth].is_memory = 0;
                stack[depth].is_reserved =
                    stack[depth - 1U].is_reserved_container;
                stack[depth].is_reserved_container = 0;
                stack[depth].is_gicv2 = 0;
            }
            if (string_is((const char *)(structure + start), "memory") ||
                string_prefix((const char *)(structure + start), "memory@"))
                stack[depth].is_memory = 1;
            if (string_is((const char *)(structure + start), "reserved-memory") &&
                depth == 1U) {
                stack[depth].is_reserved_container = 1;
                stack[depth].is_reserved = 0;
            }
            ++depth;
        } else if (token == FDT_END_NODE) {
            if (depth == 0U) { FDT_TRACE("FDTDBG: END_NODE at depth zero\n"); return -13; }
            if (collect_node(&stack[depth - 1U], info)) {
                FDT_TRACE("FDTDBG: collect node failed\n");
                return -14;
            }
            if (depth == 1U) {
                info->address_cells = stack[0].address_cells;
                info->size_cells = stack[0].size_cells;
                root_closed = 1;
            }
            --depth;
        } else if (token == FDT_PROP) {
            uint32_t len, nameoff;
            uint64_t aligned;
            const char *name;
            struct node_state *n;
            const uint8_t *value;
            if (depth == 0U || ss - pos < 8U) return -15;
            len = be32(structure + pos);
            nameoff = be32(structure + pos + 4U);
            pos += 8U;
            if (len > ss - pos || bounded_string(strings, sts, nameoff, &name))
                return -16;
            value = structure + pos;
            n = &stack[depth - 1U];
            if (string_is(name, "#address-cells") && len == 4U)
                n->address_cells = be32(value);
            else if (string_is(name, "#size-cells") && len == 4U)
                n->size_cells = be32(value);
            else if (string_is(name, "compatible"))
                n->is_gicv2 = (uint8_t)compatible_has_gicv2(value, len);
            else if (string_is(name, "reg")) {
                n->reg = value;
                n->reg_length = len;
            } else if (string_is(name, "device_type") &&
                       len == 7U && string_is((const char *)value, "memory"))
                n->is_memory = 1;
            aligned = ((uint64_t)pos + len + 3U) & ~(uint64_t)3U;
            if (aligned > ss) return -17;
            pos = (uint32_t)aligned;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            if (depth != 0U) { FDT_TRACE("FDTDBG: END with open nodes\n"); return -18; }
            if (!root_closed) { FDT_TRACE("FDTDBG: END before root close\n"); return -21; }
            ended = 1;
            FDT_TRACE("FDTDBG: end token\n");
            break;
        } else {
            return -19;
        }
    }
    if (!ended) { FDT_TRACE("FDTDBG: structure ended without END token\n"); return -20; }
    if (info->memory_count == 0U) { FDT_TRACE("FDTDBG: no memory nodes found\n"); return -22; }
    return 0;
}
