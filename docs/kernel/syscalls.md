# 系统调用表（bare-kernel）

约定：`svc #0x80`，`x8` = 号，`x0..x2` = 参数，`x0` = 返回值。
从 EL0 进来时，所有吃指针的参数都先过用户窗口校验；不通过返回 `-2`（EFAULT）。

| 号 | 名称 | 参数 | 返回 | 说明 |
|---|---|---|---|---|
| 1 | PUTS | `x0`=字符串 | 0 | 打印 C 字符串 |
| 2 | TICKS | — | tick 数 | 内核时钟计数 |
| 3 | HEAP | — | 已用字节 | 内核堆用量 |
| 4 | WRITE | `x0`=fd `x1`=buf `x2`=n | 写入字节数 | 控制台或 ramfs 文件 |
| 5 | OPEN | `x0`=路径 `x1`=flags | fd | flags：1=O_CREAT，2=O_TRUNC，4=O_APPEND |
| 6 | READ | `x0`=fd `x1`=buf `x2`=n | 读出字节数 | 控制台读非阻塞，无数据返回 0 |
| 7 | CLOSE | `x0`=fd | 0 | 0/1/2 是控制台，关不掉 |
| 8 | YIELD | — | 0 | 让出 CPU |
| 9 | EXIT | `x0`=code | 不返回 | 有父进程则变僵尸 |
| 10 | LIST | `x0`=路径 `x1`=buf `x2`=cap | 字节数 | 列目录 |
| 11 | FORK | — | 子 pid / 0 | 复制地址空间（COW）与 fd 表 |
| 12 | EXEC | `x0`=id | 不返回 | 按 id 选内置镜像（0=自检程序 1=shell） |
| 13 | WAIT | `x0`=pid（-1=任意） | 退出码 | 让出轮询，不是睡眠队列 |
| 14 | PID | — | pid | |
| 15 | PPID | — | ppid | |
| 16 | CLONE | `x0`=entry `x1`=arg `x2`=sp | tid | 共享 mm/fd 的线程 |
| 17 | GETTID | — | tgid | |
| 18 | KILL | `x0`=pid `x1`=sig | 0 | 仅默认动作 |
| 19 | EXEC_PATH | `x0`=路径 `x1`=参数字符串(可空) | 不返回 | ELF64（PT_LOAD 逐页映射）或裸代码；ELF 收到 `argc/argv`（x0/x1，自定义约定） |
| 20 | MKDIR | `x0`=路径 | 0 / -1 | |
| 21 | UNLINK | `x0`=路径 | 0 / -1 | 目录非空拒绝；文件仍被打开时名字先失效、数据留到最后一个 close |
| 22 | STAT | `x0`=路径 | 大小 / -1 | |
| 23 | TIME | — | tick 数 | |
| 24 | SLEEP | `x0`=ticks | 0 | yield 轮询到目标 tick |
| 25 | SEEK | `x0`=fd `x1`=off | 新偏移 / -1 | |
| 26 | RENAME | `x0`=旧路径 `x1`=新路径 | 0 / -1 | 支持跨目录 |
| 27 | DUP | `x0`=fd | 新 fd / -1 | |
| 28 | PIPE | `x0`=两个 int 的用户缓冲 | 0 / -1 | 4 KiB 环形缓冲，非阻塞 |
| 29 | ACCESS | `x0`=路径 | 0 / -1 | 存在性检查 |
| 31 | SIGNAL | `x0`=信号号 `x1`=handler | 0 / -1 | 注册用户态 handler |
| 32 | SIGRETURN | — | 不返回 | 从用户栈 sigframe 恢复 |

## 边界

- 没有完整 `envp`；ELF 只收一个参数字符串（`argc=1`）；ELF 要求静态、PT_LOAD 落在用户窗内（2 MiB），镜像本身受 ramfs 4 KiB 上限。
- 没有 `socket`；`PIPE` 是非阻塞的 4 KiB 环形缓冲，读空返回 0。
- `WAIT`/`SLEEP` 都是让出轮询，不是真正的睡眠队列。
- 文件系统只有 ramfs（可写）与 ext2（只读，挂 `/mnt`），无页缓存。
