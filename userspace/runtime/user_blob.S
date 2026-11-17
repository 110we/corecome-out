/* user_blob.S —— 第一个 EL0 程序。
 *
 * 约束：会被原样拷进用户页再执行，所以不许出现绝对地址。
 * 只用 adr（PC 相对）和 b，数据就贴在同一段里，复制后偏移不变，
 * 不需要重定位，也不需要链接器把 .userblob 摆在哪。
 *
 * 调用约定沿用内核：x8 = 号，x0..x2 = 参数，svc #0x80。
 *   1  = puts（打印 C 字符串）
 *   9  = exit
 */
    .section .userblob, "ax"
    .balign 8
    .global user_blob_start
    .global user_blob_end

user_blob_start:
    /* 先空转一段：这段时间里没有任何 svc，只能靠时钟中断进来抢占。
     * 退出时 preempt 计数 > 0，才证明 EL0 是真的可抢占。 */
    mov x2, #0x4000000
1:
    subs x2, x2, #1
    b.ne 1b

    adr x0, 2f
    mov x8, #1
    svc #0x80

    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
2:
    .asciz "hello from EL0\n"
    .balign 8
user_blob_end:

/* 故意越界伸手去摸内核镜像（0x40000000）。
 * EL0 的页表里那一块是 EL1-only，必须 abort —— 这是隔离的证据。 */
    .balign 8
    .global user_bad_start
    .global user_bad_end
user_bad_start:
    mov x1, #0x40000000
    ldr x0, [x1]
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
user_bad_end:

/* 第三层的靶子：不越界访存，而是**把内核地址当参数递进去**。
 * 页表拦不住这种（EL1 读用户 VA 合法），只有 syscall 层的 u_ok()
 * 能挡。期望：puts 返回 -2，随后 exit 1。 */
    .balign 8
    .global user_bad2_start
    .global user_bad2_end
user_bad2_start:
    mov x0, #0x40000000
    mov x8, #1
    svc #0x80

    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
user_bad2_end:
/* 第四个靶子：执行权。栈页是 RW + UXN，跳到栈上去必须 abort。
 * 以前 RW 没挂 UXN，这块页就是 RWX — 用户态能自己写指令再跳过去。
 * 期望：instruction abort（EC=0x20），线程被杀。 */
    .balign 8
    .global user_bad3_start
    .global user_bad3_end
user_bad3_start:
    mov x1, #0x10000000
    add x1, x1, #0x1000
    blr x1
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
user_bad3_end:
/* ---- init：fork 一个孩子，孩子 exec，父亲 wait 拿退出码 ----
 * 11=fork 12=exec 13=wait 9=exit */
    .balign 8
    .global user_init_start
    .global user_init_end
user_init_start:
    mov x8, #11
    svc #0x80              /* x0 = pid（父）或 0（子） */
    cbz x0, 1f
    mov x8, #13
    svc #0x80              /* wait(pid) -> code */
    mov x0, #0
    mov x8, #9
    svc #0x80
1:
    mov x0, #0
    mov x8, #12
    svc #0x80              /* exec(0) */
    b .
user_init_end:

/* ---- COW 靶子：父子共享一页栈，孩子先写，父亲再看 ----
 * 期望：孩子写自己的副本、父亲读到 0xAAAA；接着父亲自己写，也触发一次
 * COW，读回 0xBBBB。两条都对，才说明“共享读、分离写”成立。
 * 11=fork 13=wait 1=puts 9=exit */
    .balign 8
    .global user_cow_start
    .global user_cow_end
user_cow_start:
    mov x1, #0xAAAA
    str x1, [sp, #-16]      /* 共享页初值 */
    mov x8, #11
    svc #0x80
    cbnz x0, 2f

    /* 孩子：改自己那份 */
    mov x3, #0x111
    ldr x2, [sp, #-16]
    add x2, x2, x3
    str x2, [sp, #-16]      /* 这里必须 COW */
    adr x0, 3f
    mov x8, #1
    svc #0x80
    mov x0, #3
    mov x8, #9
    svc #0x80

2:  /* 父亲 */
    mov x0, #-1
    mov x8, #13
    svc #0x80               /* wait(any) -> code，顺带收尸 */
    ldr x2, [sp, #-16]
    mov x3, #0xAAAA
    cmp x2, x3
    b.ne 4f                 /* 孩子的写串到父亲脸上 = 失败 */
    mov x3, #0xBBBB
    str x3, [sp, #-16]      /* 父亲这一写也必须 COW */
    ldr x2, [sp, #-16]
    cmp x2, x3
    b.ne 4f
    adr x0, 5f
    mov x8, #1
    svc #0x80
    b 6f
4:
    adr x0, 7f
    mov x8, #1
    svc #0x80
6:
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
3:
    .asciz "cow: child wrote\n"
5:
    .asciz "cow: parent intact\n"
7:
    .asciz "cow: TAINTED\n"
    .balign 8
user_cow_end:

/* ---- clone 靶子：和父进程共用同一块内存，各自 +3 ----
 * 期望 counter 最后是 6：任何一段没跑，或者线程拿到了自己的一份拷贝
 * （而不是共享），这个数都会对不上。16=clone 8=yield 13=wait 9=exit */
    .balign 8
    .global user_thr_start
    .global user_thr_end
user_thr_start:
    mov x4, #0x10000000
    add x4, x4, #0x880
    add x4, x4, #0x880      /* +0x1100：add 的立即数只有 12 位，拆两次 */
    mov x5, #0
    str x5, [x4]

    adr x1, 3f              /* 线程入口 */
    mov x2, #0              /* arg */
    mov x5, #0x10000000
    add x5, x5, #0xC00
    add x5, x5, #0xC00      /* 线程自己的栈顶 0x10001800 */
    mov x3, x5
    mov x0, #1              /* flags：CLONE_VM|CLONE_THREAD 的占位 */
    mov x8, #16
    svc #0x80
    mov x19, x0             /* tid */

    mov x6, #3
4:
    ldr x5, [x4]
    add x5, x5, #1
    str x5, [x4]
    mov x8, #8
    svc #0x80               /* yield：让线程也跑起来 */
    subs x6, x6, #1
    b.ne 4b

    mov x0, x19
    mov x8, #13
    svc #0x80               /* wait(tid) */
    ldr x5, [x4]
    cmp x5, #6
    b.ne 5f
    adr x0, 6f
    mov x8, #1
    svc #0x80
    b 7f
5:
    adr x0, 8f
    mov x8, #1
    svc #0x80
7:
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .

3:  /* 线程体 */
    mov x6, #3
9:
    ldr x5, [x4]
    add x5, x5, #1
    str x5, [x4]
    mov x8, #8
    svc #0x80
    subs x6, x6, #1
    b.ne 9b
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
6:
    .asciz "thread: shared ok\n"
8:
    .asciz "thread: MISMATCH\n"
    .balign 8
user_thr_end:

/* ---- 信号靶子：父 kill 子，子的退出码必须是 -15 ----
 * 子进程死循环 yield（永远不 exit），父发 SIGTERM 后 wait。
 * 期望："sign: killed ok"，并且 wait 取到的 code = -15。
 * 11=fork 8=yield 18=kill 13=wait 1=puts 9=exit */
    .balign 8
    .global user_sig_start
    .global user_sig_end
user_sig_start:
    mov x8, #11
    svc #0x80
    mov x19, x0
    cbz x19, 3f             /* 子是 0：进死循环 */

    mov x6, #5
2:
    mov x8, #8
    svc #0x80               /* 让子先跑起来 */
    subs x6, x6, #1
    b.ne 2b

    mov x0, x19
    mov x1, #15             /* SIGTERM */
    mov x8, #18
    svc #0x80

    mov x0, x19
    mov x8, #13
    svc #0x80               /* wait -> -15 */
    mov x3, #-15
    cmp x0, x3
    b.ne 4f
    adr x0, 5f
    mov x8, #1
    svc #0x80
    b 6f
4:
    adr x0, 7f
    mov x8, #1
    svc #0x80
6:
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
3:
    mov x8, #8
    svc #0x80
    b 3b
5:
    .asciz "sign: killed ok\n"
7:
    .asciz "sign: FAIL\n"
    .balign 8
user_sig_end:

/* ---- exec 装载进去的那个程序 ---- */
    .balign 8
    .global user_exec_start
    .global user_exec_end
user_exec_start:
    adr x0, 1f
    mov x8, #1
    svc #0x80              /* puts("exec: hello from child") */
    mov x0, #7
    mov x8, #9
    svc #0x80              /* exit(7) */
    b .
1:
    .asciz "exec: hello from child\n"
    .balign 8
user_exec_end:

/* ---- M4 shell：读控制台、ls /、cat /hello.txt、exit ----
 * 4=write 5=open 6=read 7=close 8=yield 9=exit 10=list
 * 输入：read(0, sp-8, 1) 非阻塞；没数据就 yield。
 * 缓冲区全在栈页（RW）：代码页是 RX，往 .data 写会 fault。 */
    .balign 8
    .global user_shell_start
    .global user_shell_end
user_shell_start:
    mov x19, #0                 /* 行长度 */
1:
    adr x0, 20f
    mov x8, #1
    svc #0x80                   /* write("sh> ") */
2:
    mov x0, #0
    sub x1, sp, #8
    mov x2, #1
    mov x8, #6
    svc #0x80                   /* read(0, sp-8, 1) */
    cmp x0, #1
    b.lt 3f

    mov x0, #1
    sub x1, sp, #8
    mov x2, #1
    mov x8, #4
    svc #0x80                   /* echo */
    ldrb w9, [sp, #-8]
    cmp w9, #13
    b.eq 4f
    cmp w9, #10
    b.eq 4f
    cmp x19, #31
    b.hs 2b
    sub x10, sp, #64
    add x10, x10, x19
    strb w9, [x10]
    add x19, x19, #1
    b 2b
3:
    mov x8, #8
    svc #0x80                   /* yield：没有输入就别空转 */
    b 2b
4:
    adr x0, 21f
    mov x8, #1
    svc #0x80                   /* 换行 */
    cbz x19, 1b
    sub x0, sp, #64
    ldrb w9, [x0]
    mov w10, #'l'
    cmp w9, w10
    b.eq 5f
    mov w10, #'c'
    cmp w9, w10
    b.eq 6f
    mov w10, #'e'
    cmp w9, w10
    b.eq 7f
    adr x0, 22f
    mov x8, #1
    svc #0x80
    b 8f
5:                              /* ls / */
    adr x0, 23f
    sub x1, sp, #320
    mov x2, #128
    mov x8, #10
    svc #0x80
    cmp x0, #0
    b.le 8f
    mov x2, x0
    mov x0, #1
    sub x1, sp, #320
    mov x8, #4
    svc #0x80
    b 8f
6:                              /* cat /hello.txt */
    adr x0, 24f
    mov x1, #0
    mov x8, #5
    svc #0x80
    mov x20, x0
    cmp x20, #0
    b.lt 9f
    mov x0, x20
    sub x1, sp, #320
    mov x2, #128
    mov x8, #6
    svc #0x80
    cmp x0, #0
    b.le 10f
    mov x2, x0
    mov x0, #1
    sub x1, sp, #320
    mov x8, #4
    svc #0x80
10:
    mov x0, x20
    mov x8, #7
    svc #0x80
    b 8f
9:
    adr x0, 25f
    mov x8, #1
    svc #0x80
    b 8f
7:
    mov x0, #0
    mov x8, #9
    svc #0x80
8:
    mov x19, #0
    b 1b
20:
    .asciz "sh> "
21:
    .asciz "\n"
22:
    .asciz "?\n"
23:
    .asciz "/"
24:
    .asciz "/hello.txt"
25:
    .asciz "no file\n"
    .balign 8
user_shell_end:

/* ---- M4 init：EL0 的第一号进程 ----
 * 11=fork 12=exec 13=wait 9=exit
 * 孩子 exec(1) 进 shell；父亲 wait，shell 退出后打印并 exit。 */
    .balign 8
    .global user_m4_init_start
    .global user_m4_init_end
user_m4_init_start:
    adr x0, 1f
    mov x8, #1
    svc #0x80                   /* write("init: up...") */
    mov x8, #11
    svc #0x80
    cbnz x0, 2f
    adr x0, 4f                  /* "/bin/sh"：从 ramfs 加载 shell */
    mov x8, #19                 /* exec_path */
    svc #0x80
    b .
2:
    mov x8, #13
    svc #0x80                   /* wait(child) -> code */
    adr x0, 3f
    mov x8, #1
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
1:
    .asciz "init: up, starting shell\n"
3:
    .asciz "init: shell exited\n"
4:
    .asciz "/bin/sh"
    .balign 8
user_m4_init_end:

/* 第五个 EL0 靶子：**通过 VFS 读文件**，而不是读链接进镜像的字符串。
 * 路径 /bin/hello 由 vfs_init 建在 ramfs 里，内容是一行文本。
 * 读出多少写多少；open/read 失败直接 exit 1。 */
    .balign 8
    .global user_file_start
    .global user_file_end
user_file_start:
    adr x0, 3f
    mov x1, #0
    mov x8, #5              /* open(path, 0) */
    svc #0x80
    cmp x0, #0
    b.lt 9f
    mov x19, x0             /* fd */

    sub sp, sp, #128        /* 用户栈上 128B 缓冲（代码页是 RX，不能写） */
    mov x0, x19
    mov x1, sp
    mov x2, #96
    mov x8, #6              /* read(fd, buf, 96) */
    svc #0x80
    cmp x0, #0
    b.le 9f
    mov x20, x0             /* n */

    mov x0, #1
    mov x1, sp
    mov x2, x20
    mov x8, #4              /* write(1, buf, n) */
    svc #0x80

    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
9:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
3:
    .asciz "/bin/hello"
    .balign 8
user_file_end:

/* fork 后孩子 exec_path("/bin/hello.bin")，父亲 wait 并打印标记。
 * 验证的是「从文件系统加载程序」这条路径，而不是内置 blob。 */
    .balign 8
    .global user_ep_start
    .global user_ep_end
user_ep_start:
    mov x0, #0
    mov x8, #11             /* fork */
    svc #0x80
    cmp x0, #0
    b.gt 51f
    /* 孩子：换掉自己的镜像 */
    adr x0, 53f
    mov x8, #19             /* exec_path */
    svc #0x80
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
51:
    mvn x0, xzr             /* wait(-1) */
    mov x8, #13
    svc #0x80
    mov x19, x0
    mov x0, #1
    adr x1, 54f
    mov x2, #20
    mov x8, #4              /* write(1, "...", 20) */
    svc #0x80
    mov x0, x19
    mov x8, #9
    svc #0x80
    b .
53:
    .asciz "/bin/hello.bin"
54:
    .ascii "exec-file: child ok\n"
    .balign 8
user_ep_end:

/* 目录操作：mkdir → O_CREAT 建文件 → write → close → unlink 文件 → unlink 目录 */
    .balign 8
    .global user_dir_start
    .global user_dir_end
user_dir_start:
    adr x0, 61f
    mov x8, #20             /* mkdir("/tmp/d") */
    svc #0x80
    cmp x0, #0
    b.ne 69f

    adr x0, 62f
    mov x1, #1              /* O_CREAT */
    mov x8, #5              /* open */
    svc #0x80
    cmp x0, #0
    b.lt 69f
    mov x19, x0

    mov x0, x19
    adr x1, 64f
    mov x2, #7
    mov x8, #4              /* write(fd, "dir ok\n", 7) */
    svc #0x80

    mov x0, x19
    mov x8, #7              /* close */
    svc #0x80

    adr x0, 62f
    mov x8, #21             /* unlink(file) */
    svc #0x80

    adr x0, 61f
    mov x8, #21             /* unlink(dir) */
    svc #0x80

    mov x0, #1
    adr x1, 63f
    mov x2, #12
    mov x8, #4
    svc #0x80

    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
69:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
61:
    .asciz "/tmp/d"
62:
    .asciz "/tmp/d/f"
63:
    .ascii "dir-ops: ok\n"
64:
    .ascii "dir ok\n"
    .balign 8
user_dir_end:

/* 取文件大小：stat("/hello.txt") 应返回 >0，否则 exit 1 */
    .balign 8
    .global user_stat_start
    .global user_stat_end
user_stat_start:
    adr x0, 71f
    mov x8, #22             /* stat */
    svc #0x80
    cmp x0, #0
    b.le 79f
    mov x0, #1
    adr x1, 72f
    mov x2, #9
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
79:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
71:
    .asciz "/hello.txt"
72:
    .ascii "stat: ok\n"
    .balign 8
user_stat_end:

/* 取内核 tick：两次调用应单调不减，且至少一次 > 0 */
    .balign 8
    .global user_time_start
    .global user_time_end
user_time_start:
    mov x8, #23
    svc #0x80
    mov x19, x0
    mov x8, #23
    svc #0x80
    cmp x0, x19
    b.lt 89f
    cbz x19, 89f
    mov x0, #1
    adr x1, 82f
    mov x2, #9
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
89:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
82:
    .ascii "time: ok\n"
    .balign 8
user_time_end:

/* 睡 4 个 tick：醒来后时间必须前进 >= 4 */
    .balign 8
    .global user_sleep_start
    .global user_sleep_end
user_sleep_start:
    mov x8, #23             /* t0 */
    svc #0x80
    mov x19, x0
    mov x0, #4
    mov x8, #24             /* sleep(4) */
    svc #0x80
    mov x8, #23             /* t1 */
    svc #0x80
    sub x1, x0, x19
    cmp x1, #4
    b.lt 99f
    mov x0, #1
    adr x1, 92f
    mov x2, #10
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
99:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
92:
    .ascii "sleep: ok\n"
    .balign 8
user_sleep_end:

/* fork/wait 压力：连开 8 个孩子，每个 exit 0，父亲逐个收尸 */
    .balign 8
    .global user_fork_start
    .global user_fork_end
user_fork_start:
    mov x20, #8
91:
    mov x0, #0
    mov x8, #11             /* fork */
    svc #0x80
    cbnz x0, 93f
    mov x0, #0
    mov x8, #9              /* 孩子退出 */
    svc #0x80
    b .
93:
    mvn x0, xzr
    mov x8, #13             /* wait(-1) */
    svc #0x80
    cmp x0, #0
    b.ne 98f                /* 退出码不是 0 就判失败 */
    subs x20, x20, #1
    b.ne 91b
    mov x0, #1
    adr x1, 94f
    mov x2, #11
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
98:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
94:
    .ascii "stress: ok\n"
    .balign 8
user_fork_end:

/* O_TRUNC：写 10 字节 -> 以 O_TRUNC 重开 -> stat 必须为 0 */
    .balign 8
    .global user_trunc_start
    .global user_trunc_end
user_trunc_start:
    adr x0, 101f
    mov x1, #3              /* O_CREAT|O_TRUNC */
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 108f
    mov x19, x0

    mov x0, x19
    adr x1, 104f
    mov x2, #10
    mov x8, #4
    svc #0x80
    mov x0, x19
    mov x8, #7
    svc #0x80

    adr x0, 101f
    mov x1, #2              /* O_TRUNC */
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 108f
    mov x8, #7
    svc #0x80

    adr x0, 101f
    mov x8, #22             /* stat -> 0 */
    svc #0x80
    cbnz x0, 108f

    mov x0, #1
    adr x1, 102f
    mov x2, #10
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
108:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
101:
    .asciz "/tmp/tr"
102:
    .ascii "trunc: ok\n"
104:
    .ascii "abcdefghij"
    .balign 8
user_trunc_end:

/* O_APPEND：写 3 字节 -> 追加 3 字节 -> stat 必须是 6 */
    .balign 8
    .global user_append_start
    .global user_append_end
user_append_start:
    adr x0, 111f
    mov x1, #3              /* O_CREAT|O_TRUNC */
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 118f
    mov x19, x0
    mov x0, x19
    adr x1, 114f
    mov x2, #3
    mov x8, #4
    svc #0x80
    mov x0, x19
    mov x8, #7
    svc #0x80

    adr x0, 111f
    mov x1, #4              /* O_APPEND */
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 118f
    mov x19, x0
    mov x0, x19
    adr x1, 115f
    mov x2, #3
    mov x8, #4
    svc #0x80
    mov x0, x19
    mov x8, #7
    svc #0x80

    adr x0, 111f
    mov x8, #22
    svc #0x80
    cmp x0, #6
    b.ne 118f

    mov x0, #1
    adr x1, 112f
    mov x2, #11
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
118:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
111:
    .asciz "/tmp/ap"
112:
    .ascii "append: ok\n"
114:
    .ascii "abc"
115:
    .ascii "def"
    .balign 8
user_append_end:

/* SEEK：写 "abcdef" -> seek(5) -> read 1 字节应得 'f' */
    .balign 8
    .global user_seek_start
    .global user_seek_end
user_seek_start:
    adr x0, 121f
    mov x1, #3
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 128f
    mov x19, x0
    mov x0, x19
    adr x1, 124f
    mov x2, #6
    mov x8, #4
    svc #0x80
    mov x0, x19
    mov x1, #5
    mov x8, #25             /* seek(5) */
    svc #0x80
    cmp x0, #5
    b.ne 128f
    mov x0, x19
    sub x1, sp, #16
    mov x2, #1
    mov x8, #6              /* read 1 字节 */
    svc #0x80
    cmp x0, #1
    b.ne 128f
    ldrb w9, [sp, #-16]
    mov w10, #'f'
    cmp w9, w10
    b.ne 128f
    mov x0, #1
    adr x1, 122f
    mov x2, #9
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
128:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
121:
    .asciz "/tmp/sk"
122:
    .ascii "seek: ok\n"
124:
    .ascii "abcdef"
    .balign 8
user_seek_end:

/* RENAME：/tmp/ra -> /tmp/rb，旧路径失效、新路径可 stat */
    .balign 8
    .global user_rename_start
    .global user_rename_end
user_rename_start:
    adr x0, 131f
    mov x1, #3
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 138f
    mov x19, x0
    mov x0, x19
    adr x1, 134f
    mov x2, #1
    mov x8, #4
    svc #0x80
    mov x0, x19
    mov x8, #7
    svc #0x80

    adr x0, 131f
    adr x1, 135f
    mov x8, #26             /* rename */
    svc #0x80
    cbnz x0, 138f

    adr x0, 135f
    mov x8, #22
    svc #0x80
    cmp x0, #1
    b.ne 138f

    adr x0, 131f
    mov x8, #22
    svc #0x80
    cmp x0, #0
    b.ge 138f               /* 旧路径仍存在 → 失败 */

    mov x0, #1
    adr x1, 132f
    mov x2, #11
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
138:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
131:
    .asciz "/tmp/ra"
132:
    .ascii "rename: ok\n"
134:
    .ascii "x"
135:
    .asciz "/tmp/rb"
    .balign 8
user_rename_end:

/* DUP：复制 fd 后从副本读，仍应读到 "abc" 的 'a' */
    .balign 8
    .global user_dup_start
    .global user_dup_end
user_dup_start:
    adr x0, 141f
    mov x1, #3
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 148f
    mov x19, x0
    mov x0, x19
    adr x1, 144f
    mov x2, #3
    mov x8, #4
    svc #0x80
    mov x0, x19
    mov x8, #7
    svc #0x80

    adr x0, 141f
    mov x1, #0
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 148f
    mov x19, x0
    mov x0, x19
    mov x8, #27             /* dup */
    svc #0x80
    cmp x0, #0
    b.lt 148f
    mov x20, x0

    mov x0, x20
    sub x1, sp, #16
    mov x2, #1
    mov x8, #6
    svc #0x80
    cmp x0, #1
    b.ne 148f
    ldrb w9, [sp, #-16]
    mov w10, #'a'
    cmp w9, w10
    b.ne 148f

    mov x0, #1
    adr x1, 142f
    mov x2, #8
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
148:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
141:
    .asciz "/tmp/du"
142:
    .ascii "dup: ok\n"
144:
    .ascii "abc"
    .balign 8
user_dup_end:

/* 打开着 unlink：名字立刻失效，但已打开的 fd 仍可读，关闭后才释放 */
    .balign 8
    .global user_ulo_start
    .global user_ulo_end
user_ulo_start:
    adr x0, 151f
    mov x1, #3
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 158f
    mov x19, x0
    mov x0, x19
    adr x1, 154f
    mov x2, #3
    mov x8, #4
    svc #0x80
    mov x0, x19
    mov x8, #7
    svc #0x80

    adr x0, 151f
    mov x1, #0
    mov x8, #5              /* 重新打开 */
    svc #0x80
    cmp x0, #0
    b.lt 158f
    mov x19, x0

    adr x0, 151f
    mov x8, #21             /* unlink（文件还开着） */
    svc #0x80
    cbnz x0, 158f

    mov x0, x19
    sub x1, sp, #16
    mov x2, #1
    mov x8, #6              /* fd 仍能读 */
    svc #0x80
    cmp x0, #1
    b.ne 158f
    ldrb w9, [sp, #-16]
    mov w10, #'x'
    cmp w9, w10
    b.ne 158f

    mov x0, x19
    mov x8, #7              /* 关掉最后一个 fd，此时才真正释放 */
    svc #0x80

    adr x0, 151f
    mov x8, #22             /* 名字已不存在 */
    svc #0x80
    cmp x0, #0
    b.ge 158f

    mov x0, #1
    adr x1, 152f
    mov x2, #16
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
158:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
151:
    .asciz "/tmp/uo"
152:
    .ascii "unlink-open: ok\n"
154:
    .ascii "xyz"
    .balign 8
user_ulo_end:

/* 管道：写 2 字节再从读端取回，首字节应为 'h' */
    .balign 8
    .global user_pipe_start
    .global user_pipe_end
user_pipe_start:
    sub sp, sp, #32
    mov x0, sp
    mov x8, #28             /* pipe */
    svc #0x80
    cbnz x0, 168f
    ldr w19, [sp]           /* read fd */
    ldr w20, [sp, #4]       /* write fd */

    mov x0, x20
    adr x1, 164f
    mov x2, #2
    mov x8, #4
    svc #0x80
    cmp x0, #2
    b.ne 168f

    mov x0, x19
    add x1, sp, #8
    mov x2, #2
    mov x8, #6
    svc #0x80
    cmp x0, #2
    b.ne 168f
    ldrb w9, [sp, #8]
    mov w10, #'h'
    cmp w9, w10
    b.ne 168f

    mov x0, #1
    adr x1, 162f
    mov x2, #9
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
168:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
162:
    .ascii "pipe: ok\n"
164:
    .ascii "hi"
    .balign 8
user_pipe_end:

/* ACCESS：存在的路径返回 0，不存在的返回 -1 */
    .balign 8
    .global user_access_start
    .global user_access_end
user_access_start:
    adr x0, 171f
    mov x8, #29
    svc #0x80
    cbnz x0, 178f

    adr x0, 174f
    mov x8, #29
    svc #0x80
    cmp x0, #0
    b.ge 178f

    mov x0, #1
    adr x1, 172f
    mov x2, #11
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
178:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
171:
    .asciz "/hello.txt"
172:
    .ascii "access: ok\n"
174:
    .asciz "/no/such/file"
    .balign 8
user_access_end:

/* ELF 加载：fork 后孩子 exec_path("/bin/elfhello")，父亲 wait 后打印标记 */
    .balign 8
    .global user_elfrun_start
    .global user_elfrun_end
user_elfrun_start:
    mov x0, #0
    mov x8, #11
    svc #0x80
    cbnz x0, 181f
    adr x0, 183f
    mov x8, #19
    svc #0x80
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
181:
    mvn x0, xzr
    mov x8, #13
    svc #0x80
    cmp x0, #0
    b.ne 188f
    mov x0, #1
    adr x1, 182f
    mov x2, #11
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
188:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
182:
    .ascii "elf-run: ok\n"
183:
    .asciz "/bin/elfhello"
    .balign 8
user_elfrun_end:

/* 大文件：30 次 × 100 字节 = 3000 字节，stat 必须等于 3000 */
    .balign 8
    .global user_big_start
    .global user_big_end
user_big_start:
    adr x0, 191f
    mov x1, #3
    mov x8, #5
    svc #0x80
    cmp x0, #0
    b.lt 198f
    mov x19, x0
    mov x20, #30
192:
    mov x0, x19
    adr x1, 194f
    mov x2, #100
    mov x8, #4
    svc #0x80
    cmp x0, #100
    b.ne 198f
    subs x20, x20, #1
    b.ne 192b
    mov x0, x19
    mov x8, #7
    svc #0x80

    adr x0, 191f
    mov x8, #22
    svc #0x80
    cmp x0, #3000
    b.ne 198f

    mov x0, #1
    adr x1, 192f
    mov x2, #12
    mov x8, #4
    svc #0x80
    mov x0, #0
    mov x8, #9
    svc #0x80
    b .
198:
    mov x0, #1
    mov x8, #9
    svc #0x80
    b .
191:
    .asciz "/tmp/big"
192:
    .ascii "bigfile: ok\n"
194:
    .fill 100, 1, 0x41      /* 'A' × 100 */
    .balign 8
user_big_end:
