/* ext2.c —— 只读 ext2。不做位图、不做写、不做 extent（镜像用 -O ^extent）。
 * 认得的只有：超级块、组描述符、inode 表、目录项、直接块 + 一级间接。
 * 这已经够证明“块设备之上能挂真文件系统”，而不是又造一个内存 inode。
 */
#include "kern.h"
#include "blk.h"

#define EXT2_MAGIC 0xEF53

static u32 bs;              /* 1024 / 2048 / 4096 */
static u32 inodes_per_group;
static u32 inode_size;
static u32 inodes;
static u32 gdt_block;
static int mounted;

static u8 blkbuf[4096] __attribute__((aligned(8)));
static u8 inodebuf[256];

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u16 rd16(const u8 *p)
{
    return (u16)((u32)p[0] | ((u32)p[1] << 8));
}

static u32 block_read(u32 blk, u8 *out)
{
    if (bs == 1024) {
        return (u32)blk_read((u64)blk * 2, out, 2);
    }
    if (bs == 2048) {
        return (u32)blk_read((u64)blk * 4, out, 4);
    }
    return (u32)blk_read((u64)blk * 8, out, 8);
}

int ext2_init(void)
{
    u32 magic;
    u32 log;
    u32 rev;
    u32 first_data;

    mounted = 0;
    /* 超级块固定在字节 1024 */
    if (blk_read(2, blkbuf, 2) != 0) {
        return -1;
    }
    magic = rd16(blkbuf + 56);
    if (magic != EXT2_MAGIC) {
        return -1;
    }
    log = rd32(blkbuf + 24);
    if (log > 2) {
        return -1;
    }
    bs = 1024UL << log;
    inodes = rd32(blkbuf + 0);
    first_data = rd32(blkbuf + 20);
    inodes_per_group = rd32(blkbuf + 40);
    rev = rd32(blkbuf + 76);
    inode_size = rev >= 1 ? rd16(blkbuf + 88) : 128;
    if (inode_size < 128 || inode_size > 256 || inodes_per_group == 0) {
        return -1;
    }
    gdt_block = first_data + 1;
    mounted = 1;
    return 0;
}

u32 ext2_block_size(void)
{
    return bs;
}

u32 ext2_inodes(void)
{
    return inodes;
}

/* ino -> 128/256 字节的 inode 副本（inodebuf） */
static int inode_load(u32 ino)
{
    u32 group;
    u32 idx;
    u32 gd_blk;
    u32 gd_off;
    u32 table;
    u32 off;
    u32 blk;

    if (!mounted || ino == 0) {
        return -1;
    }
    group = (ino - 1) / inodes_per_group;
    idx = (ino - 1) % inodes_per_group;
    gd_blk = gdt_block + (group * 32) / bs;
    gd_off = (group * 32) % bs;
    if (block_read(gd_blk, blkbuf) != 0) {
        return -1;
    }
    table = rd32(blkbuf + gd_off + 8);
    off = idx * inode_size;
    blk = table + off / bs;
    if (block_read(blk, blkbuf) != 0) {
        return -1;
    }
    {
        u32 i;
        u8 *src = blkbuf + (off % bs);

        for (i = 0; i < inode_size && i < sizeof(inodebuf); i++) {
            inodebuf[i] = src[i];
        }
    }
    return 0;
}

/* inode 的第 n 个数据块号：12 个直接 + 一级间接 */
static u32 inode_block(u32 n)
{
    u32 p;

    if (n < 12) {
        return rd32(inodebuf + 40 + n * 4);
    }
    p = rd32(inodebuf + 40 + 12 * 4);
    if (p == 0) {
        return 0;
    }
    n -= 12;
    if (n >= bs / 4) {
        return 0;
    }
    if (block_read(p, blkbuf) != 0) {
        return 0;
    }
    return rd32(blkbuf + n * 4);
}
static int name_eq(const u8 *p, const char *s, u32 n)
{
    u32 i;

    for (i = 0; i < n; i++) {
        if (p[i] != (u8)s[i]) {
            return 0;
        }
    }
    return s[n] == '\0';
}

/* 在目录 inode 的数据块里找一个名字 */
static u32 dir_find(u32 dir, const char *name, u32 n)
{
    u32 size;
    u32 blocks;
    u32 b;

    if (inode_load(dir) != 0) {
        return 0;
    }
    if ((rd16(inodebuf + 0) & 0xF000) != 0x4000) { /* 不是目录 */
        return 0;
    }
    size = rd32(inodebuf + 4);
    blocks = (size + bs - 1) / bs;

    for (b = 0; b < blocks; b++) {
        u32 blk = inode_block(b);
        u32 off = 0;

        if (blk == 0 || block_read(blk, blkbuf) != 0) {
            return 0;
        }
        while (off + 8 <= bs && off < size) {
            u32 ino = rd32(blkbuf + off);
            u16 rec = rd16(blkbuf + off + 4);
            u32 nl = blkbuf[off + 6];

            if (ino != 0 && nl == n && name_eq(blkbuf + off + 8, name, n)) {
                return ino;
            }
            if (rec < 8) {
                break;      /* 坏目录项：宁可停下，不要走进死循环 */
            }
            off += rec;
        }
    }
    return 0;
}

int ext2_lookup(const char *path, u32 *ino)
{
    u32 cur = 2;            /* 根目录 */
    const char *p = path;

    while (*p == '/') {
        p++;
    }
    while (*p != '\0') {
        const char *q = p;
        u32 n;

        while (*q != '\0' && *q != '/') {
            q++;
        }
        n = (u32)(q - p);
        cur = dir_find(cur, p, n);
        if (cur == 0) {
            return -1;
        }
        p = q;
        while (*p == '/') {
            p++;
        }
    }
    *ino = cur;
    return 0;
}

/* 读一个文件的全部（或前 cap 字节）内容 */
i64 ext2_read(const char *path, void *out, u64 cap)
{
    u32 ino;
    u32 size;
    u64 done = 0;
    u8 *dst = (u8 *)out;

    if (ext2_lookup(path, &ino) != 0) {
        return -1;
    }
    if (inode_load(ino) != 0) {
        return -1;
    }
    size = rd32(inodebuf + 4);
    while (done < (u64)size && done < cap) {
        u32 b = (u32)(done / bs);
        u32 off = (u32)(done % bs);
        u32 blk = inode_block(b);
        u32 take;

        if (blk == 0) {
            break;
        }
        if (block_read(blk, blkbuf) != 0) {
            break;
        }
        take = bs - off;
        if (take > size - done) {
            take = size - (u32)done;
        }
        if (take > cap - done) {
            take = (u32)(cap - done);
        }
        {
            u32 i;
            for (i = 0; i < take; i++) {
                dst[done + i] = blkbuf[off + i];
            }
        }
        done += take;
    }
    return (i64)done;
}