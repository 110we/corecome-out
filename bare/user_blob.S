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
