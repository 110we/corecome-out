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
 * Each entry records its architectural slot before entering the common saver.
 */
.section .text.vectors, "ax"
.balign 2048
.global __exception_vectors
.type __exception_vectors, %function
__exception_vectors:
    b exception_vector_0
    .balign 128
    b exception_vector_1
    .balign 128
    b exception_vector_2
    .balign 128
    b exception_vector_3
    .balign 128
    b exception_vector_4
    .balign 128
    b exception_vector_5
    .balign 128
    b exception_vector_6
    .balign 128
    b exception_vector_7
    .balign 128
    b exception_vector_8
    .balign 128
    b exception_vector_9
    .balign 128
    b exception_vector_10
    .balign 128
    b exception_vector_11
    .balign 128
    b exception_vector_12
    .balign 128
    b exception_vector_13
    .balign 128
    b exception_vector_14
    .balign 128
    b exception_vector_15
.size __exception_vectors, . - __exception_vectors

/*
 * struct exception_frame:
 * x[0..30] at 0..240, ELR/SPSR/ESR/FAR at 248..272, vector at 280.
 * 304-byte allocation preserves the 16-byte stack alignment at the C ABI.
 */
exception_vector_0:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #0
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_1:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #1
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_2:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #2
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_3:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #3
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_4:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #4
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_5:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #5
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_6:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #6
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_7:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #7
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_8:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #8
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_9:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #9
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_10:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #10
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_11:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #11
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_12:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #12
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_13:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #13
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_14:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #14
    str     x0, [sp, #280]
    b       exception_save_rest

exception_vector_15:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    mov     x0, #15
    str     x0, [sp, #280]
    b       exception_save_rest

exception_save_rest:
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

    /* Dispatcher returns the frame that must be restored. */
    mov     x0, sp
    bl      exception_dispatch
    cbz     x0, exception_bad_return_frame
    mov     sp, x0

    /* Return state is taken from the selected saved frame. */
    ldr     x1, [sp, #248]
    msr     elr_el1, x1
    ldr     x1, [sp, #256]
    msr     spsr_el1, x1
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #304
    eret

exception_bad_return_frame:
    /* A null frame is a fatal dispatcher contract violation. */
    mov     x0, sp
    bl      exception_panic
    b       .

