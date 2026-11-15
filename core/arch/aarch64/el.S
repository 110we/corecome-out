.section .text.arch, "ax"
.global arch_enter_el1
.type arch_enter_el1, %function
/* Return 1 at EL1. Transition EL2 -> EL1h; reject EL3 until SCR_EL3
 * security state, exception routing and firmware handoff are specified.
 */
arch_enter_el1:
    mrs     x0, CurrentEL
    cmp     x0, #0x4
    b.eq    1f
    cmp     x0, #0x8
    b.eq    2f
    mov     x0, #0
    ret
2:
    mov     x0, #0x80000000
    msr     hcr_el2, x0
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    adr     x0, 3f
    msr     elr_el2, x0
    eret
3:
1:
    mov     x0, #1
    ret
.size arch_enter_el1, . - arch_enter_el1
.section .note.GNU-stack,"",%progbits