/* vectors.S —— 16 个异常入口。
 * 每组 0x80，顺序固定：同 EL/SP0、同 EL/SPx、低 EL AArch64、低 EL AArch32。
 * 统一先 SAVE_ALL 建 trap frame，再交给 C。
 */

    .section .text.vectors, "ax"
    .balign 2048
    .global vectors
vectors:

    /* ---- 当前 EL，SP_EL0 ---- */
    .balign 0x80
    b sync_entry
    .balign 0x80
    b irq_entry
    .balign 0x80
    b fault_entry
    .balign 0x80
    b fault_entry

    /* ---- 当前 EL，SP_ELx（我们用这一组） ---- */
    .balign 0x80
    b sync_entry
    .balign 0x80
    b irq_entry
    .balign 0x80
    b fault_entry
    .balign 0x80
    b fault_entry

    /* ---- 低 EL，AArch64 ---- */
    .balign 0x80
    b sync_entry
    .balign 0x80
    b irq_entry
    .balign 0x80
    b fault_entry
    .balign 0x80
    b fault_entry

    /* ---- 低 EL，AArch32 ---- */
    .balign 0x80
    b sync_entry
    .balign 0x80
    b irq_entry
    .balign 0x80
    b fault_entry
    .balign 0x80
    b fault_entry

/* trap frame: x0..x29 (0..232), x30/LR(240), elr(256), spsr(264), 共 288 字节
 * x30 必须一起存：硬件只把 PC 放进 ELR，LR 是普通寄存器，异常入口
 * 不保存它，返回后被打断的函数一 ret 就会跳回向量表。 */
.macro SAVE_ALL
    sub sp, sp, #288
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30,      [sp, #240]
    mrs x9, elr_el1
    str x9, [sp, #256]
    mrs x9, spsr_el1
    str x9, [sp, #264]
.endm

.macro RESTORE_ALL
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
.endm

sync_entry:
    SAVE_ALL
    mov x0, sp
    bl  do_sync
    RESTORE_ALL

irq_entry:
    SAVE_ALL
    bl  do_irq
    RESTORE_ALL

fault_entry:
    SAVE_ALL
    mov x0, sp
    bl  do_fault
    RESTORE_ALL
