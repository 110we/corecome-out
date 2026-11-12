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
| 进程 | fork 复制地址空间（COW） | fork 逐页深拷贝（**无 COW**） | ◐ 语义对，实现更笨 |
| 程序装载 | execve 换 mm | `SYS_EXEC` 换 aspace 后 eret | ✅ 语义对齐 |
| 退出/回收 | 僵尸 + wait4 收尸 | 僵尸 + `SYS_WAIT` 取 code 并清槽 | ✅（wait 是让出轮询，非睡眠队列） |
| 调度 | CFS/EEVDF，多核 | 100 Hz 时间片轮转，单核 | ◐ 单核可用 |
| 抢占 | 内核可抢占（PREEMPT） | 用户态可抢占；内核临界区靠 daif 关中断 | ◐ |
| 文件 | VFS + 页缓存 + 块设备 + 十几种 FS | VFS + ramfs（内存 inode），无块设备 | ◐ |
| fd | 每进程 files_struct，fd 表 | 每进程 filetable，fork 深拷贝 | ✅ 语义对齐 |
| 设备 | 驱动模型 + DT/ACPI | PL011、GICv2、ARM generic timer，手写探测 | ◐ |
| 网络 | 完整 TCP/IP + netfilter | 无 | ❌ |
| 信号 | 完整 | 无 | ❌ |
| 线程 | clone(CLONE_VM) | 无（进程即线程，无共享 mm） | ❌ |
| SMP | 成熟 | 单核 | ❌ |
| 模块/安全 | LSM、namespace、cgroup、seccomp | 无 | ❌ |
| 代码量 | ~30,000,000 行 | ~5,400 行 | 差 4 个数量级 |

结论：**“起平”在这张表的 ✅ 行里成立，在 ❌ 行里不成立。**
下一步能真正缩小差距的是 COW、块设备 + 真实文件系统、SMP；网络栈是另一个量级。

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
