.section .text.vectors, "ax"
.align 11
.global exception_vectors
.type exception_vectors, %function
exception_vectors:
    b exception_sync
    .balign 128
    b exception_irq
    .balign 128
    b exception_fiq
    .balign 128
    b exception_serror
    .balign 128
    b exception_sync
    .balign 128
    b exception_irq
    .balign 128
    b exception_fiq
    .balign 128
    b exception_serror
    .balign 128
    b exception_sync
    .balign 128
    b exception_irq
    .balign 128
    b exception_fiq
    .balign 128
    b exception_serror
    .balign 128
    b exception_sync
    .balign 128
    b exception_irq
    .balign 128
    b exception_fiq
    .balign 128
    b exception_serror

.type exception_sync, %function
exception_sync:
    mov x0, #0
    b exception_common
.type exception_irq, %function
exception_irq:
    mov x0, #1
    b exception_common
.type exception_fiq, %function
exception_fiq:
    mov x0, #2
    b exception_common
.type exception_serror, %function
exception_serror:
    mov x0, #3

exception_common:
    mrs x1, esr_el1
    mrs x2, far_el1
    mrs x3, elr_el1
    bl exception_handler
1:  wfe
    b 1b

.global vectors_install
.type vectors_install, %function
vectors_install:
    adrp x0, exception_vectors
    add x0, x0, :lo12:exception_vectors
    msr vbar_el1, x0
    isb
    ret

.section .note.GNU-stack,"",%progbits