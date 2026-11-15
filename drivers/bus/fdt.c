/* fdt.c —— M5 第一步：设备树（FDT/DTB）只读解析。
 *
 * 这一步只回答“设备在哪”，不回答“怎么驱动”：
 *   - 校验 magic / totalsize / 结构块与字符串块边界 / 版本；
 *   - 遍历结构块，收集根节点、/chosen、memory、PL011、GICv2；
 *   - UART / GIC 基址从 DTB 取，写死值只作为解析失败时的回落。
 *
 * 明确不做：overlay、phandle / 中断映射、driver binding、热插拔。
 * 解析失败不 panic：回落到编译期地址，并把原因打到串口——
 * 这样“DTB 坏了”在日志里是一条能读的话，而不是一次静默换址。
 */
#include "kern.h"

#define FDT_MAGIC     0xd00dfeedUL
#define FDT_HDR_SIZE  40UL
#define FDT_CUR_VER   17UL

/* 结构块 token（大端 u32） */
#define FDT_BEGIN_NODE 1UL
#define FDT_END_NODE   2UL
#define FDT_PROP       3UL
#define FDT_NOP        4UL
#define FDT_END        9UL

/* 设备窗口：paging.c 的 boot_l2 只映 0x00000000–0x0A1FFFFF。
 * DTB 给出窗口外的地址不能直接用，否则一访问就是 data abort。 */
#define DEV_WIN_TOP 0x0A200000UL
#define KERN_WIN_BASE 0x40000000UL
#define KERN_WIN_TOP  0x80000000UL

#define PATH_MAX 96
#define NODE_MAX 16

/* 节点分类：遍历时先收 reg/compatible，END_NODE 再落账。
 * 不能假设 "compatible" 一定排在 "reg" 前面——DTB 只是数据。 */
enum {
    NODE_PLAIN = 0,
    NODE_UART  = 1,
    NODE_GIC   = 2,
    NODE_MEM   = 3
};

struct node_state {
    int kind;
    int nseg;
    u64 base[2];
    u64 size[2];
};

static struct dtb_info g_dt;
static const u8 *g_base;
static u32 g_off_struct;
static u32 g_size_struct;
static u32 g_off_strings;
static u32 g_size_strings;
static struct node_state g_ns[NODE_MAX];

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static u32 align4(u32 v)
{
    return (v + 3u) & ~3u;
}

static void zero_dt(void)
{
    u8 *p = (u8 *)&g_dt;
    u32 i;

    for (i = 0; i < sizeof(g_dt); i++) {
        p[i] = 0;
    }
}

static int str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int str_has_prefix(const char *s, const char *p)
{
    while (*p != '\0') {
        if (*s++ != *p++) {
            return 0;
        }
    }
    return 1;
}

/* 在长度为 len 的区间里找第一个 NUL；找不到说明结构块被截断了。 */
static int cstr_len(const u8 *p, u32 len, u32 *out)
{
    u32 i;

    for (i = 0; i < len; i++) {
        if (p[i] == 0) {
            *out = i;
            return 0;
        }
    }
    return -1;
}

static void copy_cstr(const u8 *p, u32 len, char *dst, u32 cap)
{
    u32 n = 0;

    while (n < len && n + 1 < cap && p[n] != 0) {
        dst[n] = (char)p[n];
        n++;
    }
    dst[n] = '\0';
}

/* compatible 是 NUL 分隔的字符串表，不是单个字符串。 */
static int compat_has(const u8 *p, u32 len, const char *want)
{
    u32 i = 0;

    while (i < len) {
        const char *s = (const char *)(p + i);
        u32 n = 0;

        while (i + n < len && s[n] != 0) {
            n++;
        }
        if (str_eq(s, want)) {
            return 1;
        }
        i += n + 1;
    }
    return 0;
}

static u64 cells_u64(const u8 *p, u32 cells)
{
    if (cells == 0) {
        cells = 1;
    }
    if (cells == 1) {
        return (u64)be32(p);
    }
    return ((u64)be32(p) << 32) | (u64)be32(p + 4);
}

static void node_reset(struct node_state *n)
{
    n->kind = NODE_PLAIN;
    n->nseg = 0;
    n->base[0] = 0;
    n->base[1] = 0;
    n->size[0] = 0;
    n->size[1] = 0;
}

static void node_reg(struct node_state *n, const u8 *d, u32 len)
{
    u32 ac = g_dt.addr_cells ? g_dt.addr_cells : 2;
    u32 sc = g_dt.size_cells ? g_dt.size_cells : 2;
    u32 per = (ac + sc) * 4u;
    u32 off = 0;

    if (per == 0) {
        return;
    }
    while (off + per <= len && n->nseg < 2) {
        n->base[n->nseg] = cells_u64(d + off, ac);
        n->size[n->nseg] = cells_u64(d + off + ac * 4u, sc);
        n->nseg++;
        off += per;
    }
}

static void node_finish(struct node_state *n)
{
    if (n->kind == NODE_UART && n->nseg >= 1) {
        g_dt.uart_base = n->base[0];
        g_dt.uart_size = n->size[0];
    } else if (n->kind == NODE_GIC && n->nseg >= 1) {
        g_dt.gicd_base = n->base[0];
        g_dt.gicd_size = n->size[0];
        if (n->nseg >= 2) {
            g_dt.gicc_base = n->base[1];
            g_dt.gicc_size = n->size[1];
        } else {
            /* 老 GIC 绑定里 CPU 接口紧跟 distributor，差 0x10000 */
            g_dt.gicc_base = n->base[0] + 0x10000UL;
            g_dt.gicc_size = n->size[0];
        }
    } else if (n->kind == NODE_MEM && n->nseg >= 1) {
        g_dt.mem_base = n->base[0];
        g_dt.mem_size = n->size[0];
    }
}

static void handle_prop(u32 depth, const char *path, const char *name,
                        const u8 *d, u32 len)
{
    g_dt.props++;

    if (depth == 0) {
        if (str_eq(name, "model")) {
            copy_cstr(d, len, g_dt.model, sizeof(g_dt.model));
        } else if (str_eq(name, "#address-cells") && len >= 4) {
            g_dt.addr_cells = be32(d);
        } else if (str_eq(name, "#size-cells") && len >= 4) {
            g_dt.size_cells = be32(d);
        }
        return;
    }

    if (depth == 1 && str_eq(path, "/chosen") && str_eq(name, "bootargs")) {
        copy_cstr(d, len, g_dt.bootargs, sizeof(g_dt.bootargs));
        return;
    }

    if (str_eq(name, "compatible")) {
        if (compat_has(d, len, "arm,pl011")) {
            g_ns[depth].kind = NODE_UART;
        } else if (compat_has(d, len, "arm,cortex-a15-gic") ||
                   compat_has(d, len, "arm,gic-400")) {
            g_ns[depth].kind = NODE_GIC;
        }
    } else if (str_eq(name, "reg")) {
        node_reg(&g_ns[depth], d, len);
    }
}

static int path_push(char *path, u32 *plen, const char *name, u32 nlen)
{
    u32 need = (nlen + (*plen == 1 ? 0 : 1));

    if (*plen + need + 1 > PATH_MAX) {
        return -1;
    }
    if (*plen != 1) {
        path[(*plen)++] = '/';
    }
    while (nlen-- > 0) {
        path[(*plen)++] = *name++;
    }
    path[*plen] = '\0';
    return 0;
}

/* 一次线性遍历：token 之间的偏移全靠 size 算，不信任任何外部索引。 */
static int scan(void)
{
    u32 pos = g_off_struct;
    u32 end = g_off_struct + g_size_struct;
    char path[PATH_MAX];
    u32 plen = 0;
    u32 depth = 0;
    u32 saved[NODE_MAX];

    path[0] = '\0';

    while (pos + 4 <= end) {
        u32 tok = be32(g_base + pos);

        pos += 4;

        if (tok == FDT_BEGIN_NODE) {
            u32 nlen;

            if (depth >= NODE_MAX) {
                g_dt.err = "node nesting too deep";
                return -1;
            }
            if (cstr_len(g_base + pos, end - pos, &nlen) != 0) {
                g_dt.err = "unterminated node name";
                return -1;
            }
            saved[depth] = plen;
            if (depth == 0) {
                plen = 1;
                path[0] = '/';
                path[1] = '\0';
            } else if (path_push(path, &plen, (const char *)(g_base + pos), nlen) != 0) {
                g_dt.err = "node path too long";
                return -1;
            }
            node_reset(&g_ns[depth]);
            if (str_eq(path, "/memory") || str_has_prefix(path, "/memory@")) {
                g_ns[depth].kind = NODE_MEM;
            }
            depth++;
            g_dt.nodes++;
            pos += align4(nlen + 1);
        } else if (tok == FDT_END_NODE) {
            if (depth == 0) {
                g_dt.err = "unbalanced end-node";
                return -1;
            }
            depth--;
            node_finish(&g_ns[depth]);
            plen = saved[depth];
            path[plen] = '\0';
        } else if (tok == FDT_PROP) {
            u32 dlen;
            u32 noff;
            u32 nlen;
            const char *name;

            if (pos + 8 > end) {
                g_dt.err = "truncated property header";
                return -1;
            }
            dlen = be32(g_base + pos);
            noff = be32(g_base + pos + 4);
            pos += 8;
            if (noff >= g_size_strings) {
                g_dt.err = "property name out of strings block";
                return -1;
            }
            if (dlen > end - pos) {
                g_dt.err = "property data out of struct block";
                return -1;
            }
            name = (const char *)(g_base + g_off_strings + noff);
            if (cstr_len((const u8 *)name, g_size_strings - noff, &nlen) != 0) {
                g_dt.err = "unterminated property name";
                return -1;
            }
            handle_prop(depth > 0 ? depth - 1 : 0, path, name, g_base + pos, dlen);
            pos += align4(dlen);
        } else if (tok == FDT_NOP) {
            /* 空操作，继续 */
        } else if (tok == FDT_END) {
            break;
        } else {
            g_dt.err = "unknown struct token";
            return -1;
        }
    }

    if (depth != 0) {
        g_dt.err = "structure block not closed";
        return -1;
    }
    return 0;
}

int fdt_init(u64 pa)
{
    const u8 *b = (const u8 *)pa;
    u32 total;

    zero_dt();
    g_base = b;

    if (pa < KERN_WIN_BASE || pa >= KERN_WIN_TOP || (pa & 7UL) != 0) {
        g_dt.err = "dtb pointer outside RAM window";
        return -1;
    }

    g_dt.magic = be32(b);
    if (g_dt.magic != FDT_MAGIC) {
        g_dt.err = "bad magic";
        return -1;
    }

    total = be32(b + 4);
    if (total < FDT_HDR_SIZE) {
        g_dt.err = "totalsize too small";
        return -1;
    }
    g_dt.totalsize = total;
    g_dt.version = be32(b + 20);

    g_off_struct = be32(b + 8);
    g_off_strings = be32(b + 12);
    g_size_struct = be32(b + 36);
    g_size_strings = be32(b + 32);

    if (g_dt.version < FDT_CUR_VER) {
        g_dt.err = "fdt version too old";
        return -1;
    }
    if (g_off_struct + g_size_struct > total || g_off_struct > total) {
        g_dt.err = "struct block out of blob";
        return -1;
    }
    if (g_off_strings + g_size_strings > total || g_off_strings > total) {
        g_dt.err = "strings block out of blob";
        return -1;
    }

    if (scan() != 0) {
        return -1;
    }

    if (g_dt.uart_base == 0) {
        g_dt.err = "no pl011 uart node";
    } else if (g_dt.gicd_base == 0) {
        g_dt.err = "no gicv2 node";
    } else if (g_dt.mem_size == 0) {
        g_dt.err = "no usable memory node";
    } else if (g_dt.uart_base >= DEV_WIN_TOP || g_dt.gicd_base >= DEV_WIN_TOP) {
        /* 现有页表只映了前 10MB 设备窗口，地址超出去不能装作能用 */
        g_dt.err = "device address outside mapped window";
    } else if (g_dt.mem_base < KERN_WIN_BASE ||
               g_dt.mem_base + g_dt.mem_size > KERN_WIN_TOP) {
        g_dt.err = "memory node outside kernel map";
    } else {
        g_dt.ok = 1;
    }

    return g_dt.ok ? 0 : -1;
}

const struct dtb_info *dtb_info(void)
{
    return &g_dt;
}

void fdt_dump(void)
{
    if (!g_dt.ok) {
        printk("[bare] dtb: magic=%x BAD reason=%s\n",
               (u64)g_dt.magic, g_dt.err ? g_dt.err : "unknown");
        return;
    }
    printk("[bare] dtb: magic=%x ok size=%u ver=%u nodes=%u props=%u\n",
           (u64)FDT_MAGIC, g_dt.totalsize, g_dt.version, g_dt.nodes, g_dt.props);
    printk("[bare] dtb: model=%s\n", g_dt.model);
    printk("[bare] dtb: mem=%x+%x\n", g_dt.mem_base, g_dt.mem_size);
    printk("[bare] dtb: uart=%x+%x gicd=%x gicc=%x\n",
           g_dt.uart_base, g_dt.uart_size, g_dt.gicd_base, g_dt.gicc_base);
}
