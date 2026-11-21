.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    bl      arch_enter_el1
    cbz     x0, 3f

    // EL1 entry: establish stack and clear the complete BSS region.
    ldr     x0, =__stack_top
    mov     sp, x0
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
1:  cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:  bl      vectors_install
    bl      kernel_main
4:  wfe
    b       4b

3:  // Unsupported entry level (notably EL3): fail closed.
    wfe
    b       3b

.size _start, . - _start

.section .bss.stack, "aw", %nobits
.align 12
.skip 16384
.global __stack_top
__stack_top: