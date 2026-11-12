/* kern.h —— 大内核的公共边界。
 *
 * 大内核（monolithic）：调度、内存、VFS、设备、系统调用全部编进同一个
 * EL1 镜像、同一个地址空间，相互之间是函数调用，不是 IPC。
 * 这里是它们唯一的公共头：谁都不许再各自 typedef 一套。
 */
#ifndef KERN_H
#define KERN_H

typedef unsigned char u8;
typedef unsigned short u16;
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
/* 页引用计数：COW 的地基。共享一页 = ref+1；最后一个使用者归还才回池子。 */
void mm_page_ref(void *p);
int  mm_page_unref(void *p);
u64  mm_page_refcnt(void *p);
void mm_page_set_cow(void *p, int on);
int  mm_page_cow(void *p);

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
#define KTHREAD_MAX 24
int kthread_create(const char *name, void (*fn)(void *), void *arg);
int uart_tryget(void);
extern volatile int g_shell_mode;
void user_m4_thread(void *arg);
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
void user_bad2_thread(void *arg);
void user_bad3_thread(void *arg);
void user_init_thread(void *arg);
void user_cow_thread(void *arg);
void user_thr_thread(void *arg);
void user_sig_thread(void *arg);
int proc_fork(u64 *f);
void proc_exec(u64 *f, u64 id);
u64 proc_wait(u64 *f);
u64 user_enter_ticks(void);
u64 *sched_get_l1(void);
void sched_set_l1(u64 *l1);
u64 tick_count(void);
extern volatile int g_from_el0;
void kernel_structure(void);
void sched_dump(void);
void ps_dump(void);
void pg_dump(u64 *l1);
u64 *pg_aspace_clone(u64 *src);
int pg_cow_fault(u64 far);
u64 pg_cow_faults(void);
void pg_cow_stats(u64 *copies, u64 *shared);
void pg_aspace_ref(u64 *l1);
void pg_aspace_unref(u64 *l1);
/* ---- 进程：fork / 僵尸 / wait / 每进程 fd 表 ---- */
int sched_fork_from(u64 *f, u64 *child_l1, u64 ftab, const char *nm);
int sched_clone_thread(u64 *f, u64 *l1, u64 ftab, u64 entry, u64 arg, u64 sp);
int sched_tgid(void);
int proc_clone(u64 *f);
int sched_kill(int pid, int sig);
int sched_signal_self(void);
void sched_exit_zombie(int code);
int sched_wait_child(int pid, int *code);
int sched_has_child(void);
int sched_pid(void);
int sched_ppid(void);
u64 sched_get_ftab(void);
void sched_set_ftab(u64 ftab);
struct filetable;
struct filetable *vfs_ftab_new(void);
struct filetable *vfs_ftab_clone(void);
void syscall_dump(void);
u64 heap_size(void);
u64 heap_used(void);

/* ---- VFS / ramfs / 设备 ---- */
#define VFS_PATH_MAX 96
void vfs_init(void);
i64 vfs_open(const char *path, int flags);
i64 vfs_read(int fd, void *buf, u64 n);
i64 vfs_write(int fd, const void *buf, u64 n);
i64 vfs_close(int fd);
i64 vfs_list(const char *path, char *out, u64 cap);

#endif
