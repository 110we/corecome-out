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
    adr x0, 1f
    mov x8, #1
    svc #0x80

    mov x0, #0
    mov x8, #9
    svc #0x80

    b .
1:
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
