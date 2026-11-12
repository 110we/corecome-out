# bare-kernel：能被看见的内核

QEMU `virt` 板真的开机。不是"能编译"，也不是"跑起来了"——每一项都有断言，
失败会指出是哪一层：boot / vec / gic / timer / mmu / fault / svc / mm / slab / vfs / sched / cmd。

## 十三个文件

| 文件 | 职责 |
|---|---|
| `boot.S` | 只留 CPU0、立栈、清 `.bss`、装 `VBAR_EL1`，进 C |
| `vectors.S` | 16 个异常入口 ×0x80，`SAVE_ALL` 建 **288B** trap frame（x0–x29、x30、ELR、SPSR） |
| `switch.S` | 上下文切换：`switch_to` 存 x19–x30/sp；`thread_boot` 给新线程伪造返回 |
| `kernel.c` | 主体：PL011、printk、异常解释、GICv2、CNTP、MMU、UART 收、命令栈 |
| `kern.h` | 大内核的公共边界：所有子系统只认这一份类型与声明 |
| `mm.c` | 页分配（128×4K）+ slab（7 档），大内核对象都从这里出 |
| `sched.c` | 内核线程、轮转、100 Hz 抢占 |
| `vfs.c` | inode 树、fd 表、ramfs、`/dev/console` |
| `heap.c` | S1：bump 分配器（64KB，16B 对齐），留作对照 |
| `syscall.c` | S0：`svc #0x80` 分派，11 个号 |
| `linker.ld` | 加载到 `0x40000000`，`.text.boot` 置首，代码只读 |
| `Makefile` | `make` / `run` / `test` / `test-cmd`，`-Werror`，出 `.map` |
| `README.md` | 这份 |

## 大内核（monolithic）的形状

不是微内核：没有 IPC、没有用户态服务。调度、内存、文件、设备、系统调用
全部编进**同一个 ELF、同一个 EL1 地址空间**，彼此是函数调用。

```
kernel/bare/  （一个镜像，一张页表，10 个 .o）
├── 启动/异常   boot.S  vectors.S  switch.S
├── 调度       sched.c     tcb ×8、8K 栈、轮转、do_irq 末尾抢占
├── 内存       mm.c        128 页池 + slab 16..1024
├── 文件       vfs.c       inode 树 + fd 表 + ramfs
├── 设备       vfs 里的 /dev/console → kputc → uart_putc
└── 接口       syscall.c   puts/ticks/heap/write/open/read/close/yield/exit/list
```

现在做得最实的四件事：

1. **内存**：`mm: pages=128 free=127`，slab 切 24B/200B 两块再回收，页数能对上。
2. **文件**：`/etc/motd` 从 inode 读出 `hello from ramfs`；`/` 能列出 4 个目录项。
3. **调度**：两个线程各数 4 行，输出严格 `A0 B0 A1 B1 …`，说明真的被抢断又接回。
4. **隔离的边界**：这仍是 EL1 的大内核，**不是**微内核，也**还没有** EL0。
   用户态隔离要等 S3。

## P0 现在有什么

- **向量表**：`VBAR_EL1 = 0x40000800`（2KB 对齐），同 EL/SPx 组正确落位
- **异常现场**：打印 `EC / ELR / FAR`；SVC 与 data abort 可恢复，其余进 `panic`
- **GICv2**：GICD `0x08000000` / GICC `0x08010000`，PPI30（定时器）+ SPI33（UART）
- **定时器**：`CNTP_TVAL_EL0`，100 Hz 的 tick
- **MMU**：39-bit VA，L1 512×1GB block；`0x00000000` 段 Device-nGnRnE，
  `0x40000000` 段 Normal-WB；恒等映射，开 M/A/C/I
- **UART 收**：RX 中断 + FIFO + 主循环轮询兜底；命令栈 `help / ticks / mmu / exc / fault`

## 实测输出

### 大内核阶段（节选，完整在 `.ci/local-verify.log`）

```
[kern] init: mm
[kern] init: vfs
[kern] init: sched
[kern] init: done
[kern] mm: pages=128 free=127
[kern] slab ok pages_free=126
[kern] vfs: hello from ramfs
[kern] rootfs:
dev/
etc/
tmp/
hello.txt
[kern] sched: threads=2 cur=kmain slice=5
[thr] A 0 cur=ta
[thr] B 0 cur=tb
[thr] A 1 cur=ta
[thr] B 1 cur=tb
[thr] A 2 cur=ta
[thr] B 2 cur=tb
[thr] A 3 cur=ta
[thr] B 3 cur=tb
[thr] A exit
[thr] B exit
[kern] sched: done cur=kmain
[kern] subsystems: mm sched vfs dev syscall
[kern] BIG_KERNEL_OK
```

### P0 阶段（历史）

```
[bare] aarch64 virt, PL011 UART up
[bare] el=1  sp=0x0000000040024f70
[bare] entry=0x0000000040001540  dtb=0x0000000000000000
[bare] dtb magic=0x0 BAD
[bare] check: sp in-range ok
[bare] check: bss zeroed ok
[bare] vec: vbar=0x0000000040000800 armed
[bare] gic: ready
[bare] timer: 100 Hz
[bare] irq: ticks=5 ok
[bare] mmu: sctlr=0x0000000000c5183f on
[bare] mmu: probe=0x0000000040003040 (RAM 里读回)
[exc] sync ec=0x25 elr=0x40001804 far=0x80000000
[bare] fault: caught=1 ok
[exc] sync ec=0x15 elr=0x4000182c far=0x80000000
[bare] svc: caught=1 ok
[sys] heap ok used=164/65536
svc via 0x80
[exc] sync ec=0x15 elr=0x4000188c far=0x80000000
[sys] syscall ok
[bare] uart: rx irq on
[bare] ALL_OK
bare>
```

大内核阶段已把 S2 与 S4 接进同一个镜像：`[sys] heap ok` 是旧 bump 堆切出 164B；
`svc via 0x80` 这行字本身是**经过系统调用门**打出来的；`/etc/motd` 走的是
`vfs_open → vfs_read → inode`，不是硬编码字符串。但要说清边界：
**这还是 EL1 的大内核**。EL0 没上，用户态仍可直接碰 UART 寄存器，
所谓"文件"也只是核内 ramfs 的 inode 树，不是块设备上的文件系统。

命令栈实测（`make test-cmd`）：

```
bare> help
cmds: help ticks mmu exc fault
bare> ticks
ticks=73
bare> exc
[exc] sync ec=0x15 elr=0x40001c00 far=0x80000000
svc handled, count=2
```

`dtb magic=0x0 BAD` 不是缺陷：QEMU 用 `-kernel` 加载 ELF 时 x0 传 0，DTB 解析留到 P1。

## 跑

执行只在仓库内做，不在仓库外的散落目录跑。

```sh
make -C kernel/bare test      # 编译 + QEMU + P0 断言 → BARE_P0_OK / BARE_CMD_OK
make -C kernel/bare run       # 前台开机，Ctrl-A X 退出
make -C kernel/bare test-cmd  # 只跑串口命令栈
```

结果落 `kernel/bare/.ci/local-verify.log` 与 `local-cmd.log`。
CI（`.github/workflows/kernel.yml`）跑同一套断言；本仓库 Actions 未回写，按未启用处理。

## 四个真实踩过的坑

1. **SVC 的 ELR 已经指向下一条**。再 `+4` 会跳过一条真实指令（表现为 `svc_seen=144`）。
   data abort 的 ELR 才指向故障指令本身，只有它需要 `+4`。
2. **`do_sync` 里先 `printk` 再改 `f[30]`**：frame 指针落在 x14（caller-saved），
   `printk` 会把它踩掉，`+= 4` 写到别处。改成先改 ELR、再打印。
3. **PL011 复位后 FIFO 是关的**（FEN=0），多字节输入直接丢。开 FEN 后仍可能剩字节，
   所以主循环每 10ms 轮询一次 DR 作兜底——中断负责快，轮询负责不漏。
4. **`SAVE_ALL` 必须保存 x30**。硬件只把 PC 放进 ELR，LR 是普通寄存器。第一版没存，
   IRQ 打在一个随后要 `ret` 的函数中间，返回后 x30 仍停在 `bl do_irq` 的返回地址，
   函数一 `ret` 就跳回向量表、再执行一次 `eret`，带着 0 地址掉进 EL0。
   现象是"有时过、有时崩"：`EC=0x20, ELR=0, FAR=0`，PSTATE/串口都正常到一半。
   trap frame 因此从 272B 扩到 **288B**（x30@240、ELR@256、SPSR@264）。

## 下一步

P1（内核能力）：slab 页回收与更多尺寸档、SMP（PSCI `CPU_ON` + 自旋锁）、
DTB 解析、PSCI 关机/复位、栈守卫页、panic 带寄存器转储。

系统层：
- [x] **S0 系统调用约定**：`svc #0x80`，x8=号，x0–x2 参数，x0 返回；11 个号
- [x] **S1 堆**：bump 64KB + 16B 对齐（留作对照）
- [x] **S2 内核线程**：tcb 存 x19–x30/sp，8K 独立栈，100 Hz 轮转，
      切换点只在 `switch.S`；`[thr] A0 B0 A1 B1 …` 是实测交错
- [x] **S4 文件**：ramfs + 最小 VFS（open/read/write/close/list），
      `/dev/console` 走设备分支落到 `kputc`
- [ ] **S3 用户态**：EL0 + 用户页表 + 页权限 + syscall 门
- [ ] **S5 init + shell**：从串口进用户态，`ls/cat/echo` 全部走 syscall

验收标准：开机 → init 打印 → 用户态 shell 执行的 `cat` 能读出 ramdisk 内容，
且关掉内核后门（不允许用户态直接碰 UART 寄存器）。