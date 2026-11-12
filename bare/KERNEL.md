# bare/ 能力状态与路线图（不做“和 Linux 起平”的宣称）

这张表回答两个问题：**哪一格有真实证据？哪一格只是计划？**
不回答“能不能替代 Linux”。

状态定义（只有三种）：

- **已验证**：`make test` 里有断言或串口输出，失败会红。
- **部分**：有实现，但缺口写在“差距”列，别按 Linux 语义理解。
- **计划中**：没有实现，不代表有半成品。

| 维度 | Linux 语义 | 本内核现状 | 状态 | 差距 / 证据 |
|---|---|---|---|---|
| 内核形态 | monolithic | 同镜像直调 | 已验证 | 调度/内存/VFS/设备/syscall 同 EL1 |
| 特权级 | EL0 / EL1 | 同 | 已验证 | 用户程序跑在 EL0，异常回 EL1 |
| 页表 | 4K 多级、每进程 TTBR0 | 4K，L1/L2/L3，每进程 aspace | 已验证 | `pg_activate` 切 TTBR0 |
| 隔离 | PTE U/AP/UXN + 故障杀进程 | 同三层 | 已验证 | `insn-fault` / `data-fault` 杀线程；边界见技术债 |
| 进程创建 | fork + COW | fork 共享页 + 引用计数 | 已验证 | `cow: parent intact` |
| 程序装载 | execve 换 mm、加载 ELF | `SYS_EXEC` 换 aspace，**只装内置 blob** | 部分 | **不是 ELF 加载器**，没有 argv/envp |
| 退出/回收 | 僵尸 + wait4 | 僵尸 + `SYS_WAIT` | 已验证 | `wait pid=… code=7/-15`；wait 是让出轮询，非睡眠队列 |
| 调度 | CFS/EEVDF、多核、就绪队列 | 100 Hz 固定片轮转、单核、数组表 | 部分 | **无就绪队列/优先级/负载均衡**；仅用户态可抢占 |
| 抢占 | 内核可抢占 | 用户态可抢占，内核靠关中断 | 部分 | 长 syscall 拖拍 |
| 文件 | VFS + 页缓存 + 多 FS | VFS + ramfs root + `/dev/console` + ext2 只读 | 部分 | **ext2 无挂载点**、无页缓存、无写、无日志 |
| fd | 每进程 files_struct | 每进程 filetable | 已验证 | fork 深拷贝，clone 共享 |
| 设备 | 驱动模型 + DT/ACPI | PL011/GICv2/timer/virtio-mmio blk 手写 | 部分 | 无设备树、无驱动注册表（M5） |
| 网络 | TCP/IP + netfilter | 无 | 计划中 | — |
| 信号 | 完整、可注册 handler | 仅 KILL/TERM/SEGV 默认动作 | 部分 | 无 handler / sigreturn |
| 线程 | clone(CLONE_VM) + futex/TLS | clone 共享 mm/fd | 部分 | 无 futex、无 TLS |
| SMP | 成熟 | 单核 CPU0 | 计划中 | 无 per-CPU/自旋锁/IPI |
| 模块/安全 | LSM/namespace/cgroup | 无 | 计划中 | — |
| 代码量 | ~30,000,000 行 | ~4,000 行 | 事实 | 差 4 个数量级 |

**结论只有一句**：标“已验证”的格子是“机制角色对得上、有断言”；
标“部分”的是“能跑但不完整”，缺口写在同行；标“计划中”的是“没有”。
不要把这三种读成“差不多都行”。

## 路线图（M0–M5）

| 里程碑 | 验收物 | 状态 | 现有证据 / 缺口 |
|---|---|---|---|
| **M0 EL0 闭环** | 静态 EL0 程序 `svc` 陷入 → 表分发 → `eret` 回 EL0 | **已验证** | `hello from EL0`；write/exit 及 18 号表；UXN 越权 `insn-fault` 杀线程 |
| **M1 每进程地址空间** | aspace 创建/销毁、切 TTBR0、fork/exec | **已验证**（exec 见差距） | `pg_aspace_clone`、COW、`cow: parent intact`；exec 只换内置 blob |
| **M2 VFS + fd 表** | 每进程 fd、open/read/write/close、ramfs root、ext2 只读挂载点、/dev 字符设备 | **部分** | fd/ramfs/`/dev/console` 已有；ext2 只能按块读，**没有挂载点** |
| **M3 调度器重写** | 用户进程调度、就绪队列、时间片、抢占、切换含页表 | **部分** | 切页表与时间片已有；**无就绪队列/优先级**，内核不可抢占 |
| **M4 init + shell** | 内核加载 init，init 拉起 shell，`ls /`、`cat` 一个文件 | **未开始** | 没有 init/shell；现在靠内核线程跑自检 |
| **M5 驱动模型 + DTB** | DTB 解析、驱动注册表、virtio-blk 改走模型 | **未开始** | 设备地址写死；无 FDT 解析 |

**下一个动手项是 M4**，因为它同时压 M0–M3 的边界：init 必须由内核以 EL0 启动，
shell 必须用 fd/open/read/write，`cat` 必须走 VFS。M4 跑通之前，
不接受任何“宏内核闭环完成”的说法。

## Non-goals（与路线图同级，不是免责声明）

- **不追求 Linux ABI**：不跑 glibc、不跑现成 Linux 二进制、不实现完整 POSIX。
  系统调用号只是内部约定，随时可变。
- **不做动态链接/ELF 解释器**；M4 的程序是内核内置或简单静态镜像。
- 不做 `/proc`、`/sys`、udev、systemd 兼容；没有用户/权限模型。
- 不承诺稳定驱动接口；M5 的“驱动模型”是教学级注册表，不是 Linux bus/driver 模型。
- 不承诺 SMP、实时性、安全隔离（见下表技术债）。

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
