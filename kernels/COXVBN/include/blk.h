/* blk.h —— 块层与只读 ext2 的边界。 */
#ifndef BLK_H
#define BLK_H

int blk_init(void);                 /* 0 = 找到并初始化，-1 = 没有设备 */
int blk_read(u64 sector, void *buf, u32 nsec);
u64 blk_sectors(void);
u64 blk_reqs(void);

int ext2_init(void);
u32 ext2_block_size(void);
int ext2_lookup(const char *path, u32 *ino);
i64 ext2_read(const char *path, void *out, u64 cap);
u32 ext2_inodes(void);

#endif