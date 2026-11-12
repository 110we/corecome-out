/* kern.h —— 大内核的公共边界。
 *
 * 大内核（monolithic）：调度、内存、VFS、设备、系统调用全部编进同一个
 * EL1 镜像、同一个地址空间，相互之间是函数调用，不是 IPC。
 * 这里是它们唯一的公共头：谁都不许再各自 typedef 一套。
 */
#ifndef KERN_H
#define KERN_H

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long i64;

/* ---- 控制台 ---- */
void kputc(char c);
void kputs(const char *s);
void printk(const char *f, ...);
void panic(const char *msg);

/* ---- 内存：页分配 + slab ---- */
void mm_init(void);
void *kmalloc(u64 n);
void kfree(void *p);
u64 mm_pages_free(void);
u64 mm_pages_total(void);
void *mm_page_alloc(void);   /* 4K，已清零；页表/用户页都从这里出 */
void mm_page_free(void *p);

/* ---- 分页：4K 粒度 + 用户地址空间 ---- */
#define PG_USER_RW  1UL
#define PG_USER_RO  2UL
#define PG_USER_RX  3UL
#define USER_WIN_BASE 0x10000000UL
#define USER_WIN_TOP  0x10200000UL
void pg_init(void);
u64 *pg_template(void);
u64 *pg_aspace_new(void);
int pg_map_user(u64 *l1, u64 va, u64 pa, u64 attr);
u64 pg_user_pa(u64 *l1, u64 va);
void pg_activate(u64 *l1);
int pg_selftest(void);

/* ---- 调度：内核线程 ---- */
#define KTHREAD_MAX 8
int kthread_create(const char *name, void (*fn)(void *), void *arg);
void sched_init(void);
void sched_go(void);
void schedule(void);
void kthread_exit(void);
void sched_tick(void);
u64 kthread_alive(void);
const char *kthread_cur(void);

/* ---- 用户态（EL0） ---- */
void user_thread(void *arg);
void user_bad_thread(void *arg);

/* ---- VFS / ramfs / 设备 ---- */
#define VFS_PATH_MAX 96
void vfs_init(void);
i64 vfs_open(const char *path, int flags);
i64 vfs_read(int fd, void *buf, u64 n);
i64 vfs_write(int fd, const void *buf, u64 n);
i64 vfs_close(int fd);
i64 vfs_list(const char *path, char *out, u64 cap);

#endif
