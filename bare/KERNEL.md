# 与 Linux 的对照表（以及“起平”到底指什么）

先把话说直：**行数、驱动数量、生态、SMP 规模上不可能起平**，Linux 是 3000 万行、
几十年、上千人。任何声称“几天写出一个 Linux”的说法都是假的。

这里追的“起平”只有一件事：**核心机制的语义对齐**——同样的角色分工、同样的
隔离边界、同样的进程语义。也就是说：能指着这张表说“这一格我实现了，
它和 Linux 做的是同一件事”，而不是“我有一个名字很像的函数”。

| 维度 | Linux | 本内核 | 状态 |
|---|---|---|---|
| 内核形态 | monolithic，全在一个 EL1 镜像 | 同：调度/内存/VFS/设备/系统调用同镜像直调 | ✅ 语义对齐 |
| 特权级 | EL0 / EL1 | 同 | ✅ |
| 页表 | 4K 粒度、多级、每进程 TTBR0 | 4K，L1/L2/L3，每进程 aspace | ✅ |
| 隔离 | PTE 的 U/AP/UXN + 故障杀进程 | 三层：页表 U/UXN、EL0 故障杀线程、syscall 指针校验 | ✅ |
| 进程 | fork 复制地址空间（COW） | fork 共享页 + 引用计数，写故障分页 | ✅ 语义对齐 |
| 程序装载 | execve 换 mm | `SYS_EXEC` 换 aspace 后 eret | ✅ 语义对齐 |
| 退出/回收 | 僵尸 + wait4 收尸 | 僵尸 + `SYS_WAIT` 取 code 并清槽 | ✅（wait 是让出轮询，非睡眠队列） |
| 调度 | CFS/EEVDF，多核 | 100 Hz 时间片轮转，单核 | ◐ 单核可用 |
| 抢占 | 内核可抢占（PREEMPT） | 用户态可抢占；内核临界区靠 daif 关中断 | ◐ |
| 文件 | VFS + 页缓存 + 块设备 + 十几种 FS | VFS + ramfs + virtio-blk + ext2（只读） | ◐ 无页缓存/无写/单一 FS |
| fd | 每进程 files_struct，fd 表 | 每进程 filetable，fork 深拷贝，clone 共享 | ✅ 语义对齐 |
| 设备 | 驱动模型 + DT/ACPI | PL011、GICv2、timer、virtio-mmio blk，手写探测 | ◐ |
| 网络 | 完整 TCP/IP + netfilter | 无 | ❌ |
| 信号 | 完整 | 默认动作（KILL/TERM/SEGV），无 handler/sigreturn | ◐ 能被打断，不能接管 |
| 线程 | clone(CLONE_VM) | clone(CLONE_VM)：共享 mm/fd，单核；无 futex/TLS | ◐ 语义对，缺同步原语 |
| SMP | 成熟 | 单核 | ❌ |
| 模块/安全 | LSM、namespace、cgroup、seccomp | 无 | ❌ |
| 代码量 | ~30,000,000 行 | ~4,000 行 | 差 4 个数量级 |
结论：**“起平”在这张表的 ✅ 行里成立，在 ❌ 行里不成立。**
COW、块设备 + 真文件系统这一轮已经补上；下一步能真正缩小差距的是
SMP、内核可抢占、页缓存与可写文件系统；网络栈是另一个量级。

## P1 补上的东西（COW / 线程 / 信号 / 块设备 + ext2）

| 项 | 落点 | 验证输出 |
|---|---|---|
| 写时复制 | `paging.c` 的 `pg_aspace_clone` / `pg_cow_fault`、`mm.c` 的页引用计数 | `cow: child wrote` / `cow: parent intact` / `cow: pid=… copy ok` |
| 线程 | `SYS_CLONE`、`sched_clone_thread`、aspace 引用计数 | `proc: clone tid=… tgid=…` / `thread: shared ok`（共享计数器 = 6） |
| 信号 | `SYS_KILL`、投递点 = 时钟中断回来 + syscall 返回前 | `sign: kill pid=… sig=15` / `wait … code=-15` |
| 块设备 | `virtio_blk.c`：virtio-mmio legacy、三描述符链、轮询 used | `blk: virtio-mmio sectors=1024` |
| 文件系统 | `ext2.c`：超级块、组描述符、inode、目录项、直接块 + 一级间接 | `ext2: hello from ext2`（镜像由 `mke2fs` 生成） |

两个必须记住的坑：

1. **legacy virtio-mmio 的寄存器表和 modern 不是同一张。** 0x028 是
   GuestPageSize 而不是 QueueSel，QueueNotify/Status 在 0x050/0x070。
   照 modern 的表写，现象是“写进去读回来全是 0”，而且 poll 永远超时。
2. **fork/wait 的返回值曾经被 dispatch 覆盖。** 这些号在 `do_sync`
   里用整份 trap frame 处理，处理完又走了一遍通用 dispatch，
   对没有实现的号返回 -1，把 x0 上的 pid/code 当场冲掉。
   不崩、不报错，只是“父进程拿到 -1”——这类错只能靠断言抓。

## 技术债与推迟项（明确接受什么、不装作有什么）

这是一个**单核 EL1 大内核**。它验证的是“机制能跑通、语义对得上”，
不是“生产级隔离”。以下债务是**主动接受**的，不是没发现：

| 项 | 现状 | 风险 | 推迟到 |
|---|---|---|---|
| SMP | 单核，只启 CPU0 | 无 per-CPU、无自旋锁、无 IPI/TLB shootdown；多核下页表与调度会直接坏 | SMP 分支（第一个要做的） |
| 地址空间隔离验证 | 只有功能性故障测试（EL0 越权→杀进程、UXN） | **没有**形式化验证、没有侧信道评估、没有 ASID 复用证明 | 与 SMP 一起做 per-ASID 与 TLB 模型 |
| DMA 隔离 | 无 IOMMU/SMMU；virtio 描述符用物理==虚拟的静态缓冲 | 设备可写内核任意物理页；真机 cache 一致性未处理（只靠 dsb 保序） | 接入非一致性设备 / 开 SMMU 时 |
| 文件系统 | ext2 只读，无页缓存、无写、无日志 | 不能承载运行时数据；单块大小上限 4K | 页缓存 + 可写 ext2/日志分支 |
| 信号 | 仅 KILL/TERM/SEGV 默认动作 | 不能注册 handler，没有 sigreturn/信号栈 | 用户态信号分支 |
| 调度 | 100 Hz 固定片轮转，内核不可抢占 | 无优先级/公平性/实时性承诺；长 syscall 会拖拍 | 需要时再做（不在 Non-goals 之外承诺） |
| 驱动 | virtio-mmio legacy、PL011、GICv2 均手写轮询/最小实现 | 无错误恢复、无热插拔、无电源管理 | 按设备逐个补 |
| 内存 | 固定页池 + 简单 slab，无 swap/回收 | 页耗尽即失败；无 OOM 策略 | 不计划 |

**隔离验证的边界**：`make test` 证明的是「已知的越权路径会失败」，
不证明「不存在未知越权路径」。在 SMP、cache 一致性、DMA 隔离三项补齐之前，
不要把这里的 ✅ 当成可承载不可信代码的依据。

## 这一轮补上的东西

### 1. 进程语义（proc.c）

```
u4(init) --fork--> p8 --exec--> "exec: hello from child" --exit(7)--> zombie
   |                                                                   ^
   +------------------------- wait(8) -> code=7 -----------------------+
```

- `fork`：`pg_aspace_clone` 逐页复制 + `vfs_ftab_clone` 复制 fd 表 +
  从父进程 trap frame 造子线程，`x0=0`。
- `exec`：新 aspace、新代码页/栈页，直接改当前 trap frame 的
  ELR/SPSR/SP_EL0，`do_sync` 返回时 eret 进新程序。
- `exit`：`ppid>0` 的进僵尸态并保留退出码；内核线程/自检线程仍走注销。
- `wait`：收自己孩子的僵尸，取 code，状态置 free。
- `ps`：`PID PPID STATE CODE NAME`。

### 2. 一个真 bug（fork 变“无限分身”）

`sched_fork_from` 一开始把父进程的 `x30` 抄进 `tcb->lr`。第一次被切到时
`switch_to` 恢复 `x30` 后 `ret`，于是子进程没有走 `thread_boot` 的 eret，
而是**借父进程的返回路径重新 eret 回程序开头**——现象是 pid 一路
8、9、10… 每个“孩子”都在 fork。

断言能过是因为每一层都“成功 fork 了”，只是语义完全错了。修法是一行：

```c
t->lr = (u64)&thread_boot;   /* 而不是 f[30] */
```

这类 bug 提醒一件事：**调度器的“第一次运行”路径必须和“恢复运行”路径分开**，
否则寄存器的来历会被悄悄改变。

### 3. SP_EL0 进 trap frame

`fork` 需要知道父进程的用户栈指针，而原来的 trap frame（288 字节）只存了
x0–x30、ELR、SPSR。现在 SAVE_ALL/RESTORE_ALL/thread_boot 都带上 `sp_el0`，
孩子在 EL0 里用的是自己那份栈指针。

## 怎么验

```
make            # 编
make test       # 开机跑全部断言（含 fork/exec/wait code=7）
make test-cmd   # 串口命令：help / ps / kstruct / ticks / mmu / exc / fault
```

当前断言里和进程相关的三条：

```
[kern] proc: fork pid=8 ppid=7
exec: hello from child
[kern] proc: wait pid=8 code=7
```

`kstruct` 里也能看到 p8 的 aspace：代码页 `RX`、栈页 `RW`，
以及 syscall 表扩到 15 号（fork/exec/wait/pid/ppid）。
