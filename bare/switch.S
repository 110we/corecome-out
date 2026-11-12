/* switch.S —— 大内核的上下文切换点。
 *
 * tcb 前 13 个字固定：x19..x28、x29、x30、sp（偏移 0..96）。
 * 切换总是发生在异常返回路径上，或普通内核调用里；两种情况都只靠
 * “换 sp + 换 x30”就能接上对方的返回路径。
 */

    .text
    .global switch_to
switch_to:                       /* x0 = prev, x1 = next */
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    str x29,      [x0, #80]
    str x30,      [x0, #88]
    mov x9, sp
    str x9,       [x0, #96]
    /* DAIF 是线程私有状态：EL0 线程被抢占时 PRIMASK 是干净的，
     * 内核线程则可能是屏蔽的。不存它，抢占一次就会把某个线程的中断
     * 状态永久写死。 */
    mrs x10, daif
    str x10,      [x0, #144]

    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldr x29,      [x1, #80]
    ldr x30,      [x1, #88]
    ldr x9,       [x1, #96]
    mov sp, x9
    ldr x10,      [x1, #144]
    msr daif, x10
    ret

/* 新线程第一次被切到：sp 指向伪造的 trap frame，这里把它 eret 出去。 */
    .global thread_boot
thread_boot:
    ldr x9, [sp, #256]
    msr elr_el1, x9
    ldr x9, [sp, #264]
    msr spsr_el1, x9
    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30,      [sp, #240]
    add sp, sp, #288
    eret