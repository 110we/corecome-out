.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    msr     daifset, #0xf
    mov     x19, x0

    /* Install the EL1 vector table before entering C code. */
    adrp    x0, __exception_vectors
    add     x0, x0, :lo12:__exception_vectors
    msr     VBAR_EL1, x0
    isb

    ldr     x0, =__stack_top
    mov     sp, x0

    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
1:
    cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:
    mov     x0, x19
    bl      kernel_main
3:
    wfe
    b       3b

.size _start, . - _start

/*
 * AArch64 vector table: 16 entries, each 128 bytes; base is 2 KiB aligned.
 * All entries enter the same register-preserving diagnostic path.
 */
.section .text.vectors, "ax"
.balign 2048
.global __exception_vectors
.type __exception_vectors, %function
__exception_vectors:
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
    .balign 128
    b exception_unhandled
.size __exception_vectors, . - __exception_vectors

/*
 * struct exception_frame layout:
 * x[0..30] at offsets 0..240, then ELR/SPSR/ESR/FAR at 248..272.
 * 288 bytes keeps SP 16-byte aligned at the C ABI boundary.
 */
exception_unhandled:
    msr     daifset, #0xf
    sub     sp, sp, #288
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    mrs     x1, elr_el1
    str     x1, [sp, #248]
    mrs     x1, spsr_el1
    str     x1, [sp, #256]
    mrs     x1, esr_el1
    str     x1, [sp, #264]
    mrs     x1, far_el1
    str     x1, [sp, #272]
    mov     x0, sp
    bl      exception_panic
4:
    wfe
    b       4b
