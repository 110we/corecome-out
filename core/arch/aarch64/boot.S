.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    // QEMU virt normally enters at EL2; EL1 entry is also supported.
    mrs     x0, CurrentEL
    cmp     x0, #0x8
    b.eq    1f
    cmp     x0, #0x4
    b.eq    2f
    b       3f

1:  // EL2 -> EL1h, with all exception classes masked.
    mov     x0, #0x80000000
    msr     hcr_el2, x0
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    adr     x0, 2f
    msr     elr_el2, x0
    eret

2:  // EL1 entry: establish stack and clear the complete BSS region.
    ldr     x0, =__stack_top
    mov     sp, x0
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
4:  cmp     x0, x1
    b.hs    5f
    str     xzr, [x0], #8
    b       4b
5:  bl      vectors_install
    bl      kernel_main
6:  wfe
    b       6b

3:  // Unsupported entry level (including EL3): stop rather than execute at an
    // unknown privilege state. EL3-to-EL1 transition needs SCR_EL3 policy.
    wfe
    b       3b

.size _start, . - _start

.section .bss.stack, "aw", %nobits
.align 12
.skip 16384
.global __stack_top
__stack_top: