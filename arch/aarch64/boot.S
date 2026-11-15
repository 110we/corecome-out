.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    msr     daifset, #0xf
    mov     x19, x0

    /* Install a real EL1 vector table before entering C code. */
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
 * Until a diagnostic panic path exists, every exception is contained here
 * instead of being vectored through address zero.
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

exception_unhandled:
    msr     daifset, #0xf
4:
    wfe
    b       4b
