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
